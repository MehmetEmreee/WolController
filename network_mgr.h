/**
 * @file network_mgr.h
 * @brief Dual-stack network manager for Wi-Fi and Ethernet on WT32-ETH01.
 *
 * Manages the full lifecycle of both network interfaces:
 * - Ethernet (LAN8720A): Static IP on isolated LAN for PC communication
 * - Wi-Fi (STA): DHCP for internet access (Telegram Bot API)
 *
 * Uses ESP32 event-driven architecture — no polling for connection state.
 * Automatic reconnection is handled independently for each interface.
 *
 * @note ETH.h must be included before WiFi.h to avoid redefinition conflicts
 *       on some ESP32 Arduino core versions.
 */

#ifndef NETWORK_MGR_H
#define NETWORK_MGR_H

#include <ETH.h>
#include <WiFi.h>
#include "config.h"
#include "logger.h"

/** @brief Module tag for network-related log messages. */
static constexpr const char* NET_TAG = "network";

/**
 * @brief Typed result for operations that may fail.
 */
struct Result {
    bool ok;              ///< true if the operation succeeded.
    const char* error;    ///< Human-readable error description (nullptr if ok).
};

/**
 * @brief Dual-stack network manager singleton.
 *
 * Handles initialization, event processing, and reconnection for both
 * Wi-Fi (internet/Telegram) and Ethernet (local PC link) interfaces.
 *
 * Named NetMgr to avoid collision with ESP32 core's NetworkManager class.
 *
 * Justified singleton: Hardware network interfaces are physical singletons.
 * Mutable state tracks connection status and reconnect timing.
 */
class NetMgr {
public:
    /**
     * @brief Get the singleton NetMgr instance.
     * @return Reference to the global NetMgr.
     */
    static NetMgr& instance() {
        static NetMgr mgr;
        return mgr;
    }

    /**
     * @brief Initialize both network interfaces.
     *
     * Sets up Ethernet with static IP and Wi-Fi in STA mode.
     * Registers event handlers for connection/disconnection events.
     * This function is non-blocking — actual connection happens asynchronously.
     *
     * @return Result indicating initialization success or failure.
     */
    Result begin() {
        LOG_INFO(NET_TAG, "Initializing dual-stack network (FW %s)", FW_VERSION);

        // Register unified WiFi event handler
        WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
            NetMgr::instance().handleEvent(event, info);
        });

        // --- Ethernet Setup (Static IP) ---
        LOG_INFO(NET_TAG, "Starting Ethernet (LAN8720A)...");

        // Configure static IP before ETH.begin()
        IPAddress ethIp, ethSubnet, ethGw;
        if (!ethIp.fromString(ETH_STATIC_IP) ||
            !ethSubnet.fromString(ETH_SUBNET_MASK) ||
            !ethGw.fromString(ETH_GATEWAY)) {
            return {false, "Failed to parse Ethernet static IP configuration"};
        }

        // Start Ethernet PHY — WT32-ETH01 board variant provides pin defaults
        if (!ETH.begin()) {
            return {false, "ETH.begin() failed — check PHY wiring"};
        }

        ETH.config(ethIp, ethGw, ethSubnet);
        LOG_INFO(NET_TAG, "Ethernet PHY started, awaiting link...");

        // --- Wi-Fi Setup (STA / Static IP) ---
        LOG_INFO(NET_TAG, "Starting Wi-Fi STA (static IP: %s)...", WIFI_STATIC_IP);
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);

        // Configure static IP before connecting
        IPAddress wifiIp, wifiSubnet, wifiGw, wifiDns1, wifiDns2;
        if (!wifiIp.fromString(WIFI_STATIC_IP) ||
            !wifiSubnet.fromString(WIFI_SUBNET_MASK) ||
            !wifiGw.fromString(WIFI_GATEWAY) ||
            !wifiDns1.fromString(WIFI_DNS1) ||
            !wifiDns2.fromString(WIFI_DNS2)) {
            return {false, "Failed to parse Wi-Fi static IP configuration"};
        }
        WiFi.config(wifiIp, wifiGw, wifiSubnet, wifiDns1, wifiDns2);

        WiFi.begin(WIFI_SSID, WIFI_PASS);
        LOG_INFO(NET_TAG, "Wi-Fi connecting to SSID: %s", WIFI_SSID);

        return {true, nullptr};
    }

    /**
     * @brief Periodic network health check — call from loop().
     *
     * Checks connection status and triggers reconnection if needed.
     * Uses millis()-based timing — fully non-blocking.
     */
    void update() {
        unsigned long now = millis();

        if (now - lastCheckMs_ < NETWORK_CHECK_INTERVAL_MS) {
            return;
        }
        lastCheckMs_ = now;

        // Wi-Fi reconnection logic
        if (!wifiConnected_) {
            if (now - lastWifiReconnectMs_ >= WIFI_RECONNECT_INTERVAL_MS) {
                lastWifiReconnectMs_ = now;
                wifiReconnectAttempts_++;

                if (wifiReconnectAttempts_ > WIFI_MAX_RETRIES) {
                    LOG_ERROR(NET_TAG,
                              "Wi-Fi reconnect exhausted (%u attempts), rebooting...",
                              wifiReconnectAttempts_);
                    delay(100);
                    ESP.restart();
                }

                LOG_WARN(NET_TAG, "Wi-Fi disconnected, reconnecting (attempt %u/%u)...",
                         wifiReconnectAttempts_, WIFI_MAX_RETRIES);
                WiFi.disconnect();
                WiFi.begin(WIFI_SSID, WIFI_PASS);
            }
        }

        // Ethernet status is event-driven via ETH events — no polling needed.
        // If Ethernet link drops, the event handler sets ethConnected_ = false.
        // ETH auto-reconnects at the PHY level when the cable is re-attached.
    }

    /**
     * @brief Check if Wi-Fi is connected and has an IP.
     * @return true if Wi-Fi STA is connected.
     */
    bool isWifiConnected() const { return wifiConnected_; }

    /**
     * @brief Check if Ethernet link is up and has an IP.
     * @return true if Ethernet is connected.
     */
    bool isEthConnected() const { return ethConnected_; }

    /**
     * @brief Get the current Wi-Fi IP address.
     * @return IPAddress of the Wi-Fi interface.
     */
    IPAddress getWifiIP() const { return WiFi.localIP(); }

    /**
     * @brief Get the current Ethernet IP address.
     * @return IPAddress of the Ethernet interface.
     */
    IPAddress getEthIP() const { return ETH.localIP(); }

    /**
     * @brief Get Wi-Fi RSSI (signal strength).
     * @return RSSI in dBm, or 0 if not connected.
     */
    int32_t getWifiRSSI() const {
        return wifiConnected_ ? WiFi.RSSI() : 0;
    }

    // Delete copy/move
    NetMgr(const NetMgr&) = delete;
    NetMgr& operator=(const NetMgr&) = delete;
    NetMgr(NetMgr&&) = delete;
    NetMgr& operator=(NetMgr&&) = delete;

private:
    NetMgr() = default;

    /** @brief Wi-Fi connection state flag. */
    volatile bool wifiConnected_ = false;

    /** @brief Ethernet connection state flag. */
    volatile bool ethConnected_ = false;

    /** @brief Timestamp of last network health check. */
    unsigned long lastCheckMs_ = 0;

    /** @brief Timestamp of last Wi-Fi reconnect attempt. */
    unsigned long lastWifiReconnectMs_ = 0;

    /** @brief Count of consecutive Wi-Fi reconnect attempts. */
    uint8_t wifiReconnectAttempts_ = 0;

    /**
     * @brief Unified event handler for all Wi-Fi and Ethernet events.
     *
     * Called by the ESP32 event system — must not block.
     *
     * @param event WiFi event type.
     * @param info  Event-specific information union.
     */
    void handleEvent(WiFiEvent_t event, WiFiEventInfo_t /* info */) {
        switch (event) {
            // --- Ethernet Events ---
            case ARDUINO_EVENT_ETH_START:
                LOG_INFO(NET_TAG, "Ethernet PHY started");
                ETH.setHostname("wol-controller");
                break;

            case ARDUINO_EVENT_ETH_CONNECTED:
                LOG_INFO(NET_TAG, "Ethernet link UP (LAN8720A)");
                break;

            case ARDUINO_EVENT_ETH_GOT_IP:
                ethConnected_ = true;
                LOG_INFO(NET_TAG, "ETH connected, IP: %s, speed: %uMbps %s",
                         ETH.localIP().toString().c_str(),
                         ETH.linkSpeed(),
                         ETH.fullDuplex() ? "FULL_DUPLEX" : "HALF_DUPLEX");
                break;

            case ARDUINO_EVENT_ETH_DISCONNECTED:
                ethConnected_ = false;
                LOG_WARN(NET_TAG, "Ethernet link DOWN — check cable");
                break;

            // --- Wi-Fi Events ---
            case ARDUINO_EVENT_WIFI_STA_GOT_IP:
                wifiConnected_ = true;
                wifiReconnectAttempts_ = 0;
                LOG_INFO(NET_TAG, "Wi-Fi connected, IP: %s, RSSI: %d dBm",
                         WiFi.localIP().toString().c_str(),
                         WiFi.RSSI());
                break;

            case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
                wifiConnected_ = false;
                LOG_WARN(NET_TAG, "Wi-Fi disconnected");
                break;

            default:
                break;
        }
    }
};

#endif // NETWORK_MGR_H
