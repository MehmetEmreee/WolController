/**
 * @file WolController.ino
 * @brief Main entry point for the WoL Controller firmware.
 *
 * Implements a cooperative scheduler with independent timers for:
 * - Network health monitoring and reconnection
 * - Telegram Bot API polling with exponential backoff
 * - Watchdog ping loop and WoL state machine
 * - Hardware watchdog (esp_task_wdt) supervision
 *
 * === Architecture ===
 *
 * loop() is the cooperative scheduler. It calls update() on each subsystem
 * every iteration. Each subsystem internally tracks its own millis()-based
 * timer and returns immediately if its interval has not elapsed.
 *
 * loop() MUST complete in under 50ms. No blocking calls are permitted
 * except the intentional 2s delay before ESP.restart() in /reboot.
 *
 * === Boot Sequence ===
 *
 * setup():
 *   1. Initialize Serial (logging)
 *   2. Register hardware watchdog (10s timeout)
 *   3. Initialize dual-stack network (ETH + Wi-Fi)
 *   4. Initialize Telegram manager (load NVS state)
 *   5. Print boot summary
 *
 * @note Compile with Arduino ESP32 core 3.x, -Wall -Wextra
 */

#include <esp_task_wdt.h>

#include "config.h"
#include "logger.h"
#include "auth.h"
#include "network_mgr.h"
#include "wol.h"
#include "watchdog.h"
#include "telegram_mgr.h"

// ============================================================================
//  SETUP
// ============================================================================

/**
 * @brief One-time initialization — called by the Arduino framework at boot.
 *
 * Initializes all subsystems in dependency order and registers the
 * hardware watchdog timer for loop stall detection.
 */
void setup() {
    // --- 1. Serial / Logging ---
    Logger::instance().begin(115200);
    LOG_INFO("main", "======================================");
    LOG_INFO("main", " WoL Controller v%s", FW_VERSION);
    LOG_INFO("main", " Build: %s", FW_BUILD);
    LOG_INFO("main", "======================================");

    // --- 2. Hardware Watchdog (ESP-IDF 5.x / Arduino core 3.x API) ---
    LOG_INFO("main", "Configuring hardware watchdog (%us timeout)...",
             HW_WATCHDOG_TIMEOUT_S);

    esp_task_wdt_config_t wdtConfig = {
        .timeout_ms         = HW_WATCHDOG_TIMEOUT_S * 1000,
        .idle_core_mask     = 0,     // Don't watch idle tasks
        .trigger_panic      = true   // Reboot on timeout
    };
    esp_task_wdt_deinit();               // Remove default WDT config if any
    esp_task_wdt_init(&wdtConfig);       // Apply our config
    esp_task_wdt_add(NULL);              // Subscribe current task (loopTask)
    LOG_INFO("main", "Hardware watchdog registered");

    // --- 3. Network ---
    Result netResult = NetMgr::instance().begin();
    if (!netResult.ok) {
        LOG_ERROR("main", "Network init FAILED: %s", netResult.error);
        LOG_ERROR("main", "Rebooting in 5s...");
        delay(5000);
        ESP.restart();
    }
    LOG_INFO("main", "Network subsystem initialized (awaiting connections)");

    // Brief wait for initial connections (non-critical, just for cleaner logs)
    unsigned long waitStart = millis();
    while (millis() - waitStart < 5000) {
        if (NetMgr::instance().isWifiConnected() &&
            NetMgr::instance().isEthConnected()) {
            break;
        }
        delay(100);
        esp_task_wdt_reset();  // Feed watchdog during wait
    }

    if (NetMgr::instance().isEthConnected()) {
        LOG_INFO("main", "Ethernet: CONNECTED (%s)",
                 NetMgr::instance().getEthIP().toString().c_str());
    } else {
        LOG_WARN("main", "Ethernet: NOT CONNECTED (will retry)");
    }

    if (NetMgr::instance().isWifiConnected()) {
        LOG_INFO("main", "Wi-Fi: CONNECTED (%s, %d dBm)",
                 NetMgr::instance().getWifiIP().toString().c_str(),
                 NetMgr::instance().getWifiRSSI());
    } else {
        LOG_WARN("main", "Wi-Fi: NOT CONNECTED (will retry)");
    }

    // --- 4. Telegram ---
    Result tgResult = TelegramManager::instance().begin();
    if (!tgResult.ok) {
        LOG_ERROR("main", "Telegram init FAILED: %s", tgResult.error);
        // Non-fatal: continue without Telegram, will retry on Wi-Fi connect
    } else {
        LOG_INFO("main", "Telegram manager initialized");
    }

    // --- 5. Boot Summary ---
    LOG_INFO("main", "--------------------------------------");
    LOG_INFO("main", "Free heap: %u bytes", static_cast<unsigned>(ESP.getFreeHeap()));
    LOG_INFO("main", "CPU freq:  %u MHz", static_cast<unsigned>(ESP.getCpuFreqMHz()));
    LOG_INFO("main", "Flash:     %u KB", static_cast<unsigned>(ESP.getFlashChipSize() / 1024));
    LOG_INFO("main", "--------------------------------------");
    LOG_INFO("main", "Setup complete — entering main loop");
}

// ============================================================================
//  MAIN LOOP — COOPERATIVE SCHEDULER
// ============================================================================

/**
 * @brief Main cooperative scheduler — called repeatedly by the Arduino framework.
 *
 * Each subsystem's update() function checks its own timer internally and
 * returns immediately if its interval has not elapsed. This ensures loop()
 * completes in well under 50ms during normal operation.
 *
 * Order of operations:
 *   1. Feed hardware watchdog (prevents reset)
 *   2. Update network health (reconnects if needed)
 *   3. Update Telegram polling (fetch/dispatch commands)
 *   4. Update watchdog state machine (ping/WoL cycle)
 *   5. Yield to background tasks
 */
void loop() {
    // 1. Feed hardware watchdog — MUST be first
    esp_task_wdt_reset();

    // 2. Network health check and reconnection
    NetMgr::instance().update();

    // 3. Telegram Bot polling and command dispatch
    TelegramManager::instance().update();

    // 4. Watchdog ping loop and state machine
    Watchdog::instance().update();

    // 5. Yield to system tasks (Wi-Fi stack, etc.)
    yield();
}
