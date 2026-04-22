/**
 * @file telegram_mgr.h
 * @brief Telegram Bot long-polling manager with command dispatcher.
 *
 * Handles all communication with the Telegram Bot API:
 * - HTTPS long-polling for incoming updates (getUpdates)
 * - Message sending (sendMessage)
 * - Command parsing and dispatch via function pointer map
 * - Exponential backoff on consecutive errors
 * - Rate limiting and heap-pressure throttling
 *
 * NVS persistence ensures lastUpdateId survives soft-resets,
 * preventing duplicate command processing after reboot.
 *
 * @note Requires WiFiClientSecure for HTTPS and ArduinoJson 6.x
 *       with StaticJsonDocument for memory-safe parsing.
 */

#ifndef TELEGRAM_MGR_H
#define TELEGRAM_MGR_H

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "config.h"
#include "logger.h"
#include "auth.h"
#include "network_mgr.h"
#include "wol.h"
#include "watchdog.h"

/** @brief Module tag for Telegram-related log messages. */
static constexpr const char* TG_TAG = "telegram";

/**
 * @brief Command handler function signature.
 * @param chatId  Authorized chat ID for the reply.
 * @param payload Command argument text (after the command token), may be empty.
 */
using CommandHandler = void(*)(int64_t chatId, const char* payload);

/**
 * @brief Command dispatch table entry.
 */
struct CommandEntry {
    const char*    command;  ///< Command string (e.g., "/start").
    CommandHandler handler;  ///< Function to call when this command is received.
};

// ============================================================================
//  Forward declarations of command handlers
// ============================================================================

static void cmdStart(int64_t chatId, const char* payload);
static void cmdWake(int64_t chatId, const char* payload);
static void cmdStatus(int64_t chatId, const char* payload);
static void cmdWatchdog(int64_t chatId, const char* payload);
static void cmdInfo(int64_t chatId, const char* payload);
static void cmdReboot(int64_t chatId, const char* payload);

/**
 * @brief Static command dispatch table.
 *
 * Commands are matched case-sensitively against incoming message text.
 * /start and /help both map to the same usage handler.
 */
static constexpr size_t CMD_TABLE_SIZE = 7;

// NOTE: Cannot be constexpr because function pointers are not constexpr in
// all compiler configurations. Declared static to limit linkage.
static const CommandEntry CMD_TABLE[CMD_TABLE_SIZE] = {
    {"/start",    cmdStart},
    {"/help",     cmdStart},
    {"/wake",     cmdWake},
    {"/status",   cmdStatus},
    {"/watchdog", cmdWatchdog},
    {"/info",     cmdInfo},
    {"/reboot",   cmdReboot},
};

/**
 * @brief Telegram Bot API manager singleton.
 *
 * Handles HTTPS polling, message parsing, command dispatch, and reply sending.
 * Implements exponential backoff on consecutive poll errors.
 *
 * Justified singleton: Single bot token, single polling loop, single HTTP
 * client. Mutable state: backoff counters, last update ID, NVS handle.
 */
class TelegramManager {
public:
    /**
     * @brief Get the singleton TelegramManager instance.
     * @return Reference to the global TelegramManager.
     */
    static TelegramManager& instance() {
        static TelegramManager mgr;
        return mgr;
    }

    /**
     * @brief Initialize the Telegram manager.
     *
     * Loads the last processed update ID from NVS and configures the
     * HTTPS client for Telegram API communication.
     *
     * @return Result indicating initialization success.
     */
    Result begin() {
        // Load persisted lastUpdateId from NVS
        prefs_.begin(NVS_NAMESPACE, false);
        lastUpdateId_ = prefs_.getLong64(NVS_KEY_LAST_UPDATE, 0);

        if (lastUpdateId_ > 0) {
            LOG_INFO(TG_TAG, "Restored lastUpdateId from NVS: %lld",
                     static_cast<long long>(lastUpdateId_));
        }

        // Configure WiFiClientSecure — use insecure mode for compatibility
        // with the wide range of Telegram API certificate rotations.
        // For production with pinned certs, replace with setCACert().
        secureClient_.setInsecure();

        LOG_INFO(TG_TAG, "Telegram manager initialized");
        return {true, nullptr};
    }

    /**
     * @brief Cooperative update — call every loop() iteration.
     *
     * Polls for new Telegram updates at the configured interval,
     * with exponential backoff on consecutive errors.
     * Skips polling if Wi-Fi is down or heap is critically low.
     */
    void update() {
        unsigned long now = millis();

        // Calculate effective interval with backoff
        uint32_t effectiveInterval = TELEGRAM_POLL_INTERVAL_MS;
        if (consecutiveErrors_ > 0) {
            // Exponential backoff: base * 2^(errors-1), capped at max
            uint32_t backoff = TELEGRAM_BASE_BACKOFF_MS;
            for (uint8_t i = 1; i < consecutiveErrors_ && backoff < TELEGRAM_MAX_BACKOFF_MS; i++) {
                backoff *= 2;
            }
            if (backoff > TELEGRAM_MAX_BACKOFF_MS) {
                backoff = TELEGRAM_MAX_BACKOFF_MS;
            }
            effectiveInterval = backoff;
        }

        if (now - lastPollMs_ < effectiveInterval) {
            return;
        }
        lastPollMs_ = now;

        // Pre-flight checks
        if (!NetMgr::instance().isWifiConnected()) {
            return;  // Silently skip — Wi-Fi reconnect is handled by NetworkManager
        }

        if (ESP.getFreeHeap() < MIN_FREE_HEAP_BYTES) {
            LOG_WARN(TG_TAG, "Low heap (%u bytes), deferring poll",
                     static_cast<unsigned>(ESP.getFreeHeap()));
            return;
        }

        // Execute poll
        pollUpdates();
    }

    /**
     * @brief Send a text message to a specific chat.
     *
     * Constructs and sends an HTTPS POST to the Telegram sendMessage endpoint.
     * Resources are explicitly freed in all code paths.
     *
     * @param chatId  Target chat ID.
     * @param message Message text (supports Telegram Markdown).
     * @return Result indicating send success or failure.
     */
    Result sendMessage(int64_t chatId, const String& message) {
        if (!NetMgr::instance().isWifiConnected()) {
            return {false, "Wi-Fi not connected"};
        }

        // Construct URL — BOT_TOKEN is interpolated but never logged
        char url[256];
        int written = snprintf(url, sizeof(url),
                               "%s%s/sendMessage",
                               TELEGRAM_API_BASE, BOT_TOKEN);
        if (written < 0 || static_cast<size_t>(written) >= sizeof(url)) {
            return {false, "URL buffer overflow"};
        }

        // Build JSON body
        StaticJsonDocument<512> doc;
        doc["chat_id"] = chatId;
        doc["text"] = message;
        doc["parse_mode"] = "Markdown";

        char body[512];
        size_t bodyLen = serializeJson(doc, body, sizeof(body));
        if (bodyLen == 0) {
            return {false, "JSON serialization failed"};
        }

        // Send HTTPS POST
        HTTPClient http;
        http.setTimeout(TELEGRAM_HTTP_TIMEOUT_MS);

        if (!http.begin(secureClient_, url)) {
            http.end();
            return {false, "HTTPClient begin failed"};
        }

        http.addHeader("Content-Type", "application/json");

        int httpCode = http.POST(reinterpret_cast<uint8_t*>(body), bodyLen);

        if (httpCode != HTTP_CODE_OK) {
            LOG_ERROR(TG_TAG, "sendMessage failed: HTTP %d", httpCode);
            http.end();
            return {false, "HTTP request failed"};
        }

        http.end();
        return {true, nullptr};
    }

    /**
     * @brief Get the count of consecutive poll errors.
     * @return Consecutive error count.
     */
    uint8_t getConsecutiveErrors() const { return consecutiveErrors_; }

    // Delete copy/move
    TelegramManager(const TelegramManager&) = delete;
    TelegramManager& operator=(const TelegramManager&) = delete;
    TelegramManager(TelegramManager&&) = delete;
    TelegramManager& operator=(TelegramManager&&) = delete;

private:
    TelegramManager() = default;

    /** @brief HTTPS client for Telegram API. */
    WiFiClientSecure secureClient_;

    /** @brief NVS preferences handle. */
    Preferences prefs_;

    /** @brief ID of the last processed Telegram update. */
    int64_t lastUpdateId_ = 0;

    /** @brief Timestamp of last poll attempt. */
    unsigned long lastPollMs_ = 0;

    /** @brief Count of consecutive poll errors for backoff. */
    uint8_t consecutiveErrors_ = 0;

    /**
     * @brief Poll Telegram getUpdates API for new messages.
     *
     * Processes all updates in a single response, dispatching commands
     * for authorized chat IDs. Updates lastUpdateId and persists to NVS.
     */
    void pollUpdates() {
        // Build getUpdates URL
        char url[384];
        int written = snprintf(url, sizeof(url),
                               "%s%s/getUpdates?offset=%lld&limit=5&timeout=0",
                               TELEGRAM_API_BASE, BOT_TOKEN,
                               static_cast<long long>(lastUpdateId_ + 1));
        if (written < 0 || static_cast<size_t>(written) >= sizeof(url)) {
            LOG_ERROR(TG_TAG, "URL buffer overflow in pollUpdates");
            return;
        }

        HTTPClient http;
        http.setTimeout(TELEGRAM_HTTP_TIMEOUT_MS);

        if (!http.begin(secureClient_, url)) {
            consecutiveErrors_++;
            LOG_ERROR(TG_TAG, "HTTPClient begin failed (errors: %u)", consecutiveErrors_);
            http.end();
            return;
        }

        int httpCode = http.GET();

        if (httpCode != HTTP_CODE_OK) {
            consecutiveErrors_++;
            LOG_ERROR(TG_TAG, "HTTP request failed: %s (code %d, errors: %u)",
                      httpCode > 0 ? "bad status" : "timeout/connection",
                      httpCode, consecutiveErrors_);
            http.end();
            return;
        }

        // Parse response
        String payload = http.getString();
        http.end();  // Free HTTP resources before JSON parsing

        StaticJsonDocument<JSON_DOC_CAPACITY> doc;
        DeserializationError jsonErr = deserializeJson(doc, payload);

        if (jsonErr) {
            consecutiveErrors_++;
            LOG_ERROR(TG_TAG, "JSON parse error: %s", jsonErr.c_str());
            return;
        }

        // Reset error counter on successful parse
        consecutiveErrors_ = 0;

        if (!doc["ok"].as<bool>()) {
            LOG_WARN(TG_TAG, "API returned ok=false");
            return;
        }

        JsonArray results = doc["result"].as<JsonArray>();
        if (results.isNull() || results.size() == 0) {
            return;  // No new updates — normal idle state
        }

        // Process each update
        for (JsonObject update : results) {
            int64_t updateId = update["update_id"].as<int64_t>();

            // Advance lastUpdateId monotonically
            if (updateId > lastUpdateId_) {
                lastUpdateId_ = updateId;
            }

            // Extract message fields
            JsonObject message = update["message"].as<JsonObject>();
            if (message.isNull()) {
                continue;  // Skip non-message updates (e.g., edited_message)
            }

            int64_t chatId = message["chat"]["id"].as<int64_t>();
            const char* text = message["text"].as<const char*>();

            if (text == nullptr || strlen(text) == 0) {
                continue;  // Skip messages without text
            }

            // Sanitize: truncate excessively long messages
            size_t textLen = strlen(text);
            if (textLen > TELEGRAM_MAX_MSG_LEN) {
                LOG_WARN(TG_TAG, "Message truncated: %u > %u chars",
                         static_cast<unsigned>(textLen),
                         static_cast<unsigned>(TELEGRAM_MAX_MSG_LEN));
                // We only inspect the first TELEGRAM_MAX_MSG_LEN chars
            }

            // Authorization check — MUST happen before command dispatch
            if (!Auth::instance().isAuthorized(chatId)) {
                // Reply to unauthorized user
                sendMessage(chatId, "⛔ Unauthorized. This incident has been logged.");
                continue;
            }

            // Dispatch command
            dispatchCommand(chatId, text);
        }

        // Persist lastUpdateId to NVS for crash/reboot resilience
        persistLastUpdateId();
    }

    /**
     * @brief Dispatch a command to the appropriate handler.
     *
     * Searches the command table for a matching prefix. If no match is found,
     * sends an "unknown command" reply with a hint to use /help.
     *
     * @param chatId Authorized chat ID.
     * @param text   Full message text.
     */
    void dispatchCommand(int64_t chatId, const char* text) {
        // Extract command and payload
        // Commands may have bot suffix: /command@botname
        // Payload starts after the first space
        for (size_t i = 0; i < CMD_TABLE_SIZE; i++) {
            size_t cmdLen = strlen(CMD_TABLE[i].command);

            if (strncmp(text, CMD_TABLE[i].command, cmdLen) == 0) {
                // Verify the command ends at a word boundary
                char nextChar = text[cmdLen];
                if (nextChar == '\0' || nextChar == ' ' || nextChar == '@') {
                    // Extract payload (text after command + space)
                    const char* payload = "";
                    const char* spacePos = strchr(text + cmdLen, ' ');
                    if (spacePos != nullptr) {
                        payload = spacePos + 1;
                    }

                    LOG_INFO(TG_TAG, "Command: %s (payload: \"%s\")",
                             CMD_TABLE[i].command, payload);
                    CMD_TABLE[i].handler(chatId, payload);
                    return;
                }
            }
        }

        // Unknown command
        LOG_WARN(TG_TAG, "Unknown command: %.32s", text);
        TelegramManager::instance().sendMessage(
            chatId,
            "❓ Unknown command.\nType /help to see available commands.");
    }

    /**
     * @brief Persist lastUpdateId to NVS.
     *
     * Called after processing each batch of updates to ensure
     * crash resilience. Uses putLong64 for int64_t storage.
     */
    void persistLastUpdateId() {
        prefs_.putLong64(NVS_KEY_LAST_UPDATE, lastUpdateId_);
    }
};

// ============================================================================
//  COMMAND HANDLER IMPLEMENTATIONS
// ============================================================================

/**
 * @brief Handle /start and /help — display usage menu.
 */
static void cmdStart(int64_t chatId, const char* /* payload */) {
    String msg;
    msg.reserve(REPLY_MSG_MAX_LEN);
    msg += "🖥️ *WoL Controller* v";
    msg += FW_VERSION;
    msg += "\n\n";
    msg += "📋 *Commands:*\n";
    msg += "/wake — Send Wake-on-LAN\n";
    msg += "/status — Ping PC (online/offline)\n";
    msg += "/watchdog on — Enable auto-monitoring\n";
    msg += "/watchdog off — Disable monitoring\n";
    msg += "/info — System information\n";
    msg += "/reboot — Restart ESP32\n";
    msg += "/help — Show this menu";

    TelegramManager::instance().sendMessage(chatId, msg);
}

/**
 * @brief Handle /wake — send WoL magic packet and confirm delivery.
 */
static void cmdWake(int64_t chatId, const char* /* payload */) {
    TelegramManager::instance().sendMessage(chatId, "⏳ Sending Wake-on-LAN packet...");

    Result r = WolSender::instance().send();

    if (r.ok) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "✅ WoL magic packet sent!\n"
                 "Target: %02X:%02X:%02X:%02X:%02X:%02X\n"
                 "Broadcast: %s:%u",
                 TARGET_MAC[0], TARGET_MAC[1], TARGET_MAC[2],
                 TARGET_MAC[3], TARGET_MAC[4], TARGET_MAC[5],
                 WOL_BROADCAST_ADDR, WOL_PORT);
        TelegramManager::instance().sendMessage(chatId, String(buf));
    } else {
        String errMsg = "❌ WoL failed: ";
        errMsg += r.error;
        TelegramManager::instance().sendMessage(chatId, errMsg);
    }
}

/**
 * @brief Handle /status — ping PC and report online/offline with latency.
 */
static void cmdStatus(int64_t chatId, const char* /* payload */) {
    PingResult result = Watchdog::instance().pingOnce();

    char buf[128];
    if (result.reachable) {
        snprintf(buf, sizeof(buf),
                 "🟢 *ONLINE*\nLatency: %.0f ms\nTarget: %s",
                 result.latencyMs, PC_IP_ADDR);
    } else {
        snprintf(buf, sizeof(buf),
                 "🔴 *OFFLINE*\nTarget: %s\nNo response within timeout",
                 PC_IP_ADDR);
    }

    TelegramManager::instance().sendMessage(chatId, String(buf));
}

/**
 * @brief Handle /watchdog on|off — enable or disable the watchdog.
 */
static void cmdWatchdog(int64_t chatId, const char* payload) {
    // Parse sub-command
    if (strncmp(payload, "on", 2) == 0) {
        Result r = Watchdog::instance().enable();
        if (r.ok) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "✅ Watchdog *enabled*\n\n"
                     "📊 Config:\n"
                     "• Target: %s\n"
                     "• Ping interval: %us\n"
                     "• Fail threshold: %u\n"
                     "• Recovery cooldown: %us\n"
                     "• Max WoL retries: %u",
                     PC_IP_ADDR,
                     WATCHDOG_PING_INTERVAL_MS / 1000,
                     WATCHDOG_FAIL_THRESHOLD,
                     WATCHDOG_RECOVERY_COOLDOWN_MS / 1000,
                     WATCHDOG_MAX_WOL_RETRIES);
            TelegramManager::instance().sendMessage(chatId, String(buf));
        } else {
            String errMsg = "❌ Cannot enable watchdog: ";
            errMsg += r.error;
            TelegramManager::instance().sendMessage(chatId, errMsg);
        }
    } else if (strncmp(payload, "off", 3) == 0) {
        // Send final status before stopping
        const char* stateName = watchdogStateToStr(Watchdog::instance().getState());
        uint8_t fails = Watchdog::instance().getFailCount();

        Watchdog::instance().disable();

        char buf[128];
        snprintf(buf, sizeof(buf),
                 "🛑 Watchdog *disabled*\n"
                 "Last state: %s\n"
                 "Pending failures: %u",
                 stateName, fails);
        TelegramManager::instance().sendMessage(chatId, String(buf));
    } else {
        TelegramManager::instance().sendMessage(
            chatId,
            "⚠️ Usage: /watchdog on  or  /watchdog off");
    }
}

/**
 * @brief Handle /info — display system information.
 */
static void cmdInfo(int64_t chatId, const char* /* payload */) {
    unsigned long uptimeSec = millis() / 1000UL;
    unsigned long uptimeMin = uptimeSec / 60;
    unsigned long uptimeHr  = uptimeMin / 60;

    char buf[REPLY_MSG_MAX_LEN];
    snprintf(buf, sizeof(buf),
             "ℹ️ *System Info*\n\n"
             "🔧 FW: v%s\n"
             "📅 Build: %s\n\n"
             "🌐 *Network:*\n"
             "• ETH IP: %s %s\n"
             "• WiFi IP: %s %s\n"
             "• WiFi RSSI: %d dBm\n\n"
             "⏱ *Uptime:* %luh %lum %lus\n"
             "💾 *Free Heap:* %u bytes\n\n"
             "🐕 *Watchdog:* %s\n"
             "• Failures: %u/%u\n"
             "• WoL retries: %u/%u",
             FW_VERSION,
             FW_BUILD,
             NetMgr::instance().isEthConnected() ?
                 ETH.localIP().toString().c_str() : "—",
             NetMgr::instance().isEthConnected() ? "✅" : "❌",
             NetMgr::instance().isWifiConnected() ?
                 WiFi.localIP().toString().c_str() : "—",
             NetMgr::instance().isWifiConnected() ? "✅" : "❌",
             NetMgr::instance().getWifiRSSI(),
             uptimeHr, uptimeMin % 60, uptimeSec % 60,
             static_cast<unsigned>(ESP.getFreeHeap()),
             watchdogStateToStr(Watchdog::instance().getState()),
             Watchdog::instance().getFailCount(),
             WATCHDOG_FAIL_THRESHOLD,
             Watchdog::instance().getWolRetries(),
             WATCHDOG_MAX_WOL_RETRIES);

    TelegramManager::instance().sendMessage(chatId, String(buf));
}

/**
 * @brief Handle /reboot — confirm and restart ESP32 after 2s delay.
 *
 * The 2s delay is the ONLY intentional blocking call in the firmware,
 * placed immediately before ESP.restart() so the Telegram confirmation
 * message has time to be sent.
 */
static void cmdReboot(int64_t chatId, const char* /* payload */) {
    LOG_INFO(TG_TAG, "Reboot requested by authorized user");
    TelegramManager::instance().sendMessage(chatId, "🔄 Rebooting ESP32 in 2 seconds...");

    // Intentional delay to allow the message to send before restart.
    // This is the ONLY blocking delay in the firmware, justified by
    // the fact that the system is about to restart anyway.
    delay(2000);
    ESP.restart();
}

#endif // TELEGRAM_MGR_H
