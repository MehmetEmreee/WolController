/**
 * @file ota.h
 * @brief ArduinoOTA wrapper for wireless firmware updates.
 *
 * Provides password-protected over-the-air firmware update capability.
 * Progress and status are reported via the Logger. OTA runs cooperatively
 * within the main loop — ArduinoOTA.handle() is non-blocking when idle.
 *
 * @note OTA uses Wi-Fi only. Ensure Wi-Fi is connected before calling begin().
 */

#ifndef OTA_H
#define OTA_H

#include <ArduinoOTA.h>
#include "config.h"
#include "logger.h"

/** @brief Module tag for OTA-related log messages. */
static constexpr const char* OTA_TAG = "ota";

/**
 * @brief ArduinoOTA lifecycle manager singleton.
 *
 * Wraps ArduinoOTA setup and event callbacks with structured logging.
 * Password is sourced from credentials.h via config.h.
 *
 * Justified singleton: ArduinoOTA is itself a global singleton.
 */
class OtaManager {
public:
    /**
     * @brief Get the singleton OtaManager instance.
     * @return Reference to the global OtaManager.
     */
    static OtaManager& instance() {
        static OtaManager mgr;
        return mgr;
    }

    /**
     * @brief Initialize ArduinoOTA with hostname, password, and callbacks.
     *
     * Must be called after Wi-Fi is initialized (not necessarily connected).
     * OTA will become available once Wi-Fi connects.
     *
     * @return Result indicating initialization success.
     */
    Result begin() {
        ArduinoOTA.setHostname(OTA_HOSTNAME);
        ArduinoOTA.setPort(OTA_PORT);
        ArduinoOTA.setPassword(OTA_PASSWORD);

        ArduinoOTA.onStart([]() {
            const char* type = (ArduinoOTA.getCommand() == U_FLASH)
                                   ? "firmware"
                                   : "filesystem";
            LOG_INFO(OTA_TAG, "OTA update starting (%s)...", type);
        });

        ArduinoOTA.onEnd([]() {
            LOG_INFO(OTA_TAG, "OTA update complete — rebooting");
        });

        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            static uint8_t lastPercent = 255;
            uint8_t percent = static_cast<uint8_t>((progress * 100) / total);
            // Log every 25% to avoid flooding
            if (percent / 25 != lastPercent / 25) {
                lastPercent = percent;
                LOG_INFO(OTA_TAG, "OTA progress: %u%%", percent);
            }
        });

        ArduinoOTA.onError([](ota_error_t error) {
            const char* errStr = "Unknown";
            switch (error) {
                case OTA_AUTH_ERROR:    errStr = "Auth failed";    break;
                case OTA_BEGIN_ERROR:   errStr = "Begin failed";   break;
                case OTA_CONNECT_ERROR: errStr = "Connect failed"; break;
                case OTA_RECEIVE_ERROR: errStr = "Receive failed"; break;
                case OTA_END_ERROR:     errStr = "End failed";     break;
            }
            LOG_ERROR(OTA_TAG, "OTA error: %s (code %u)", errStr, error);
        });

        ArduinoOTA.begin();
        LOG_INFO(OTA_TAG, "OTA initialized (hostname: %s, port: %u)",
                 OTA_HOSTNAME, OTA_PORT);

        return {true, nullptr};
    }

    /**
     * @brief Handle OTA requests — call every loop() iteration.
     *
     * Non-blocking when no OTA is in progress. During active OTA transfer,
     * this processes incoming data chunks.
     */
    void update() {
        ArduinoOTA.handle();
    }

    // Delete copy/move
    OtaManager(const OtaManager&) = delete;
    OtaManager& operator=(const OtaManager&) = delete;
    OtaManager(OtaManager&&) = delete;
    OtaManager& operator=(OtaManager&&) = delete;

private:
    OtaManager() = default;
};

#endif // OTA_H
