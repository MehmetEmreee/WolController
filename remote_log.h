/**
 * @file remote_log.h
 * @brief Telnet-style remote serial monitor with log history replay.
 *
 * Provides a lightweight TCP server (default port 23) that streams all
 * log output to connected clients in real-time. When a new client connects,
 * the recent log history is replayed so the user has immediate context.
 *
 * Usage:
 *   - Connect: telnet <ESP_WIFI_IP> 23
 *   - Or:      nc <ESP_WIFI_IP> 23
 *   - Or:      PuTTY → Raw connection to port 23
 *
 * Architecture:
 *   Logger calls RemoteLog::write() via a registered callback.
 *   RemoteLog stores lines in a circular buffer and broadcasts to clients.
 *   No dependency on Logger.h (avoids circular include).
 *
 * @note Supports up to 2 simultaneous clients. No authentication
 *       (intended for trusted local network use only).
 */

#ifndef REMOTE_LOG_H
#define REMOTE_LOG_H

#include <WiFiServer.h>
#include <WiFiClient.h>
#include "config.h"

/**
 * @brief Remote serial monitor via TCP (telnet-style).
 *
 * Justified singleton: Single telnet server, single log stream.
 * Mutable state: client connections, circular history buffer.
 */
class RemoteLog {
public:
    /**
     * @brief Get the singleton RemoteLog instance.
     * @return Reference to the global RemoteLog.
     */
    static RemoteLog& instance() {
        static RemoteLog rl;
        return rl;
    }

    /**
     * @brief Start the TCP server for remote log access.
     *
     * Begins listening on the configured telnet port.
     * Safe to call before Wi-Fi/ETH is connected — clients simply
     * won't be able to connect until the network is up.
     */
    void begin() {
        server_.begin(TELNET_PORT);
        server_.setNoDelay(true);
        started_ = true;

        // Use Serial directly — Logger may not be fully configured yet
        Serial.printf("[INFO ] [telnet] Remote log server started on port %u\n",
                      TELNET_PORT);
    }

    /**
     * @brief Accept new clients and clean disconnected ones.
     *
     * Must be called every loop() iteration. Non-blocking.
     */
    void update() {
        if (!started_) return;

        // Accept new clients
        WiFiClient newClient = server_.available();
        if (newClient) {
            bool accepted = false;
            for (size_t i = 0; i < MAX_CLIENTS; i++) {
                if (!clients_[i] || !clients_[i].connected()) {
                    clients_[i] = newClient;
                    accepted = true;

                    // Send welcome banner
                    clients_[i].printf(
                        "\r\n"
                        "╔══════════════════════════════════════╗\r\n"
                        "║  WoL Controller v%-19s ║\r\n"
                        "║  Remote Serial Monitor               ║\r\n"
                        "╚══════════════════════════════════════╝\r\n"
                        "\r\n",
                        FW_VERSION);

                    // Replay log history
                    sendHistory(clients_[i]);

                    Serial.printf("[INFO ] [telnet] Client %u connected from %s\n",
                                  static_cast<unsigned>(i),
                                  clients_[i].remoteIP().toString().c_str());
                    break;
                }
            }

            if (!accepted) {
                newClient.println("Maximum clients reached. Try again later.");
                newClient.stop();
            }
        }

        // Clean disconnected clients
        for (size_t i = 0; i < MAX_CLIENTS; i++) {
            if (clients_[i] && !clients_[i].connected()) {
                Serial.printf("[INFO ] [telnet] Client %u disconnected\n",
                              static_cast<unsigned>(i));
                clients_[i].stop();
            }
        }
    }

    /**
     * @brief Write a log line to all connected clients and the history buffer.
     *
     * Called by the Logger via callback. Must be fast and non-blocking.
     * If a client write fails, the client is silently dropped (cleaned in update).
     *
     * @param line Formatted log line (without trailing newline).
     */
    void write(const char* line) {
        if (!started_) return;

        size_t len = strlen(line);

        // Store in circular history buffer
        for (size_t i = 0; i < len; i++) {
            history_[writePos_] = line[i];
            writePos_ = (writePos_ + 1) % HISTORY_SIZE;
        }
        // Store \r\n delimiter
        history_[writePos_] = '\r';
        writePos_ = (writePos_ + 1) % HISTORY_SIZE;
        history_[writePos_] = '\n';
        writePos_ = (writePos_ + 1) % HISTORY_SIZE;

        // Track if buffer has wrapped
        if (historyLen_ + len + 2 >= HISTORY_SIZE) {
            wrapped_ = true;
        }
        historyLen_ += len + 2;

        // Broadcast to connected clients
        for (size_t i = 0; i < MAX_CLIENTS; i++) {
            if (clients_[i] && clients_[i].connected()) {
                clients_[i].print(line);
                clients_[i].print("\r\n");
            }
        }
    }

    /**
     * @brief Check if any remote client is connected.
     * @return true if at least one client is connected.
     */
    bool hasClients() {
        for (size_t i = 0; i < MAX_CLIENTS; i++) {
            if (clients_[i] && clients_[i].connected()) return true;
        }
        return false;
    }

    /**
     * @brief Get the number of connected clients.
     * @return Count of active clients.
     */
    uint8_t clientCount() {
        uint8_t count = 0;
        for (size_t i = 0; i < MAX_CLIENTS; i++) {
            if (clients_[i] && clients_[i].connected()) count++;
        }
        return count;
    }

    // Delete copy/move
    RemoteLog(const RemoteLog&) = delete;
    RemoteLog& operator=(const RemoteLog&) = delete;
    RemoteLog(RemoteLog&&) = delete;
    RemoteLog& operator=(RemoteLog&&) = delete;

private:
    RemoteLog() = default;

    /** @brief Maximum simultaneous telnet clients. */
    static constexpr size_t MAX_CLIENTS = 2;

    /** @brief Circular history buffer size (bytes). */
    static constexpr size_t HISTORY_SIZE = 4096;

    /** @brief TCP server instance. */
    WiFiServer server_{TELNET_PORT};

    /** @brief Connected client slots. */
    WiFiClient clients_[MAX_CLIENTS];

    /** @brief Circular log history buffer. */
    char history_[HISTORY_SIZE] = {};

    /** @brief Current write position in history buffer. */
    size_t writePos_ = 0;

    /** @brief Total bytes written (for wrap detection). */
    size_t historyLen_ = 0;

    /** @brief Whether history buffer has wrapped around. */
    bool wrapped_ = false;

    /** @brief Whether server has been started. */
    bool started_ = false;

    /**
     * @brief Replay log history to a newly connected client.
     *
     * Sends the circular buffer contents in chronological order.
     *
     * @param client The newly connected WiFiClient.
     */
    void sendHistory(WiFiClient& client) {
        if (wrapped_) {
            // Buffer has wrapped: data is [writePos_..end] then [0..writePos_)
            size_t tailLen = HISTORY_SIZE - writePos_;
            if (tailLen > 0) {
                client.write(
                    reinterpret_cast<const uint8_t*>(history_ + writePos_),
                    tailLen);
            }
            if (writePos_ > 0) {
                client.write(
                    reinterpret_cast<const uint8_t*>(history_),
                    writePos_);
            }
        } else if (writePos_ > 0) {
            // Buffer hasn't wrapped: data is [0..writePos_)
            client.write(
                reinterpret_cast<const uint8_t*>(history_),
                writePos_);
        }

        client.print("--- End of history ---\r\n\r\n");
    }
};

#endif // REMOTE_LOG_H
