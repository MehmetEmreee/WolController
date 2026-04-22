/**
 * @file wol.h
 * @brief IEEE 802.3 Wake-on-LAN magic packet generator, bound to Ethernet.
 *
 * Generates and sends standard WoL magic packets consisting of:
 *   - 6 bytes of 0xFF (synchronization stream)
 *   - 16 repetitions of the target MAC address (96 bytes)
 *   - Total payload: 102 bytes
 *
 * The packet is sent as a UDP broadcast on the Ethernet interface only,
 * ensuring it reaches the target PC on the isolated LAN segment.
 *
 * Reference: IEEE 802.3 Wake-on-LAN specification (Magic Packet Technology).
 */

#ifndef WOL_H
#define WOL_H

#include <WiFiUdp.h>
#include "config.h"
#include "logger.h"
#include "network_mgr.h"

/** @brief Module tag for WoL-related log messages. */
static constexpr const char* WOL_TAG = "wol";

/** @brief Size of the WoL magic packet payload (6 + 16*6 = 102). */
static constexpr size_t WOL_PACKET_SIZE = 102;

/**
 * @brief Wake-on-LAN magic packet sender, interface-bound to Ethernet.
 *
 * Constructs an IEEE 802.3 compliant magic packet and sends it as a UDP
 * broadcast through the Ethernet interface. Wi-Fi is never used for WoL.
 *
 * Justified singleton: Single Ethernet interface, single target MAC.
 */
class WolSender {
public:
    /**
     * @brief Get the singleton WolSender instance.
     * @return Reference to the global WolSender.
     */
    static WolSender& instance() {
        static WolSender sender;
        return sender;
    }

    /**
     * @brief Build and send a WoL magic packet via Ethernet broadcast.
     *
     * The magic packet is constructed on the stack (no heap allocation)
     * and sent as a single UDP datagram to the broadcast address.
     *
     * @return Result indicating send success or failure reason.
     */
    Result send() {
        // Verify Ethernet connectivity before attempting send
        if (!NetMgr::instance().isEthConnected()) {
            LOG_ERROR(WOL_TAG, "Cannot send WoL — Ethernet not connected");
            return {false, "Ethernet not connected"};
        }

        // Build magic packet on stack — 102 bytes, zero heap allocation
        uint8_t packet[WOL_PACKET_SIZE];
        buildMagicPacket(packet, TARGET_MAC);

        // Parse broadcast address
        IPAddress broadcastAddr;
        if (!broadcastAddr.fromString(WOL_BROADCAST_ADDR)) {
            LOG_ERROR(WOL_TAG, "Invalid broadcast address: %s", WOL_BROADCAST_ADDR);
            return {false, "Invalid broadcast address configuration"};
        }

        // Send UDP broadcast via Ethernet
        WiFiUDP udp;
        if (udp.begin(WOL_PORT) == 0) {
            LOG_ERROR(WOL_TAG, "Failed to bind UDP socket on port %u", WOL_PORT);
            return {false, "UDP socket bind failed"};
        }

        if (udp.beginPacket(broadcastAddr, WOL_PORT) == 0) {
            udp.stop();
            LOG_ERROR(WOL_TAG, "Failed to begin UDP packet to %s:%u",
                      WOL_BROADCAST_ADDR, WOL_PORT);
            return {false, "UDP beginPacket failed"};
        }

        size_t written = udp.write(packet, WOL_PACKET_SIZE);
        if (written != WOL_PACKET_SIZE) {
            udp.stop();
            LOG_ERROR(WOL_TAG, "UDP write incomplete: %u/%u bytes",
                      static_cast<unsigned>(written),
                      static_cast<unsigned>(WOL_PACKET_SIZE));
            return {false, "UDP write incomplete"};
        }

        if (udp.endPacket() == 0) {
            udp.stop();
            LOG_ERROR(WOL_TAG, "Failed to send UDP packet");
            return {false, "UDP endPacket failed"};
        }

        udp.stop();

        LOG_INFO(WOL_TAG,
                 "Magic packet sent to %s:%u (MAC %02X:%02X:%02X:%02X:%02X:%02X)",
                 WOL_BROADCAST_ADDR, WOL_PORT,
                 TARGET_MAC[0], TARGET_MAC[1], TARGET_MAC[2],
                 TARGET_MAC[3], TARGET_MAC[4], TARGET_MAC[5]);

        return {true, nullptr};
    }

    // Delete copy/move
    WolSender(const WolSender&) = delete;
    WolSender& operator=(const WolSender&) = delete;
    WolSender(WolSender&&) = delete;
    WolSender& operator=(WolSender&&) = delete;

private:
    WolSender() = default;

    /**
     * @brief Construct an IEEE 802.3 compliant WoL magic packet.
     *
     * Layout:
     *   Bytes  0–5:   0xFF 0xFF 0xFF 0xFF 0xFF 0xFF  (sync stream)
     *   Bytes  6–101: Target MAC repeated 16 times    (96 bytes)
     *
     * @param[out] packet Output buffer, must be at least WOL_PACKET_SIZE bytes.
     * @param[in]  mac    Target MAC address (6 bytes).
     */
    static void buildMagicPacket(uint8_t* packet, const uint8_t* mac) {
        // Synchronization stream: 6 bytes of 0xFF
        for (size_t i = 0; i < 6; i++) {
            packet[i] = 0xFF;
        }

        // Target MAC address repeated 16 times
        for (size_t rep = 0; rep < 16; rep++) {
            for (size_t b = 0; b < 6; b++) {
                packet[6 + (rep * 6) + b] = mac[b];
            }
        }
    }
};

#endif // WOL_H
