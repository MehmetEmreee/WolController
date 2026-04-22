/**
 * @file config.h
 * @brief Compile-time configuration constants for the WoL Controller firmware.
 *
 * All magic numbers, network parameters, timing intervals, and hardware pin
 * assignments are centralized here. No literal constants should appear in
 * application code outside of this file.
 *
 * Sensitive credentials (Wi-Fi, Telegram, MAC) are in credentials.h which
 * is excluded from version control via .gitignore.
 *
 * @see credentials.example.h — copy to credentials.h and fill in your values.
 *
 * @author WolController Firmware
 * @version 1.0.0
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>
#include "credentials.h"  // WIFI_SSID, WIFI_PASS, BOT_TOKEN, ALLOWED_CHAT_ID, TARGET_MAC

// ============================================================================
//  TARGET PC — WAKE-ON-LAN (non-secret network config)
// ============================================================================

/** @brief WoL magic packet destination port (standard). */
static constexpr uint16_t WOL_PORT = 9;

/** @brief WoL broadcast address — must egress via Ethernet only. */
static constexpr const char* WOL_BROADCAST_ADDR = "10.0.0.255";

// ============================================================================
//  NETWORK — ETHERNET (Static)
// ============================================================================

/** @brief Static IP for Ethernet interface on isolated LAN. */
static constexpr const char* ETH_STATIC_IP   = "10.0.0.50";

/** @brief Subnet mask for Ethernet. */
static constexpr const char* ETH_SUBNET_MASK = "255.255.255.0";

/** @brief Gateway for Ethernet (not used for internet, but required). */
static constexpr const char* ETH_GATEWAY     = "10.0.0.1";

/** @brief IP of the target PC on the Ethernet LAN for ping checks. */
static constexpr const char* PC_IP_ADDR      = "10.0.0.100";

// ============================================================================
//  NETWORK — WI-FI
// ============================================================================

/**
 * @brief Maximum number of Wi-Fi reconnect attempts before resetting.
 * After this many consecutive failures the ESP32 will reboot.
 */
static constexpr uint8_t WIFI_MAX_RETRIES = 20;

/** @brief Delay between Wi-Fi reconnect attempts (ms). */
static constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 5000;

// ============================================================================
//  OTA (Over-the-Air Updates)
// ============================================================================

/** @brief OTA service port. */
static constexpr uint16_t OTA_PORT = 3232;

/** @brief OTA mDNS hostname. */
static constexpr const char* OTA_HOSTNAME = "wol-controller";

// ============================================================================
//  REMOTE LOG (Telnet Serial Bridge)
// ============================================================================

/** @brief Telnet server port for remote serial access. */
static constexpr uint16_t TELNET_PORT = 23;

// ============================================================================
//  TIMING — COOPERATIVE SCHEDULER INTERVALS
// ============================================================================

/** @brief Telegram long-poll interval when idle (ms). */
static constexpr uint32_t TELEGRAM_POLL_INTERVAL_MS = 1000;

/** @brief Maximum Telegram poll backoff after consecutive errors (ms). */
static constexpr uint32_t TELEGRAM_MAX_BACKOFF_MS = 30000;

/** @brief Base backoff time for exponential backoff (ms). */
static constexpr uint32_t TELEGRAM_BASE_BACKOFF_MS = 1000;

/** @brief HTTP timeout for Telegram API calls (ms). */
static constexpr uint32_t TELEGRAM_HTTP_TIMEOUT_MS = 10000;

/** @brief Watchdog ping interval while monitoring (ms). */
static constexpr uint32_t WATCHDOG_PING_INTERVAL_MS = 15000;

/** @brief Ping timeout for each individual ICMP probe (ms). */
static constexpr uint32_t PING_TIMEOUT_MS = 2000;

/** @brief Number of consecutive ping failures before triggering WoL. */
static constexpr uint8_t WATCHDOG_FAIL_THRESHOLD = 3;

/** @brief Cooldown after WoL send before resuming ping checks (ms). */
static constexpr uint32_t WATCHDOG_RECOVERY_COOLDOWN_MS = 60000;

/** @brief Maximum number of WoL retries before giving up in recovery. */
static constexpr uint8_t WATCHDOG_MAX_WOL_RETRIES = 5;

/** @brief Network status check interval (ms). */
static constexpr uint32_t NETWORK_CHECK_INTERVAL_MS = 10000;

// ============================================================================
//  HARDWARE WATCHDOG (esp_task_wdt)
// ============================================================================

/** @brief Hardware watchdog timeout in seconds. Resets ESP32 if loop stalls. */
static constexpr uint32_t HW_WATCHDOG_TIMEOUT_S = 10;

// ============================================================================
//  TELEGRAM API
// ============================================================================

/** @brief Telegram Bot API host. */
static constexpr const char* TELEGRAM_API_HOST = "api.telegram.org";

/** @brief Telegram Bot API base URL prefix (without token). */
static constexpr const char* TELEGRAM_API_BASE = "https://api.telegram.org/bot";

/** @brief Maximum length of a Telegram message we will process. */
static constexpr size_t TELEGRAM_MAX_MSG_LEN = 256;

/** @brief ArduinoJson document capacity for Telegram responses. */
static constexpr size_t JSON_DOC_CAPACITY = 4096;

// ============================================================================
//  MEMORY SAFETY
// ============================================================================

/** @brief Minimum free heap threshold (bytes). Below this, non-critical ops pause. */
static constexpr size_t MIN_FREE_HEAP_BYTES = 30720;  // 30 KB

/** @brief Maximum length for formatted log messages. */
static constexpr size_t LOG_MSG_MAX_LEN = 256;

/** @brief Maximum length for formatted Telegram reply messages. */
static constexpr size_t REPLY_MSG_MAX_LEN = 512;

// ============================================================================
//  NVS PERSISTENCE
// ============================================================================

/** @brief NVS namespace for persistent state. */
static constexpr const char* NVS_NAMESPACE = "wolctrl";

/** @brief NVS key for the last processed Telegram update ID. */
static constexpr const char* NVS_KEY_LAST_UPDATE = "lastUpdId";

// ============================================================================
//  FIRMWARE METADATA
// ============================================================================

/** @brief Firmware version string. */
static constexpr const char* FW_VERSION = "1.1.0";

/** @brief Firmware build identifier. */
static constexpr const char* FW_BUILD   = __DATE__ " " __TIME__;

#endif // CONFIG_H
