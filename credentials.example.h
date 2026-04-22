/**
 * @file credentials.example.h
 * @brief Template for credentials.h — copy and fill in your values.
 *
 * SETUP INSTRUCTIONS:
 *   1. Copy this file:  cp credentials.example.h credentials.h
 *   2. Edit credentials.h with your actual values
 *   3. credentials.h is already in .gitignore — it will NOT be committed
 *
 * @warning NEVER put real credentials in this example file.
 */

#ifndef CREDENTIALS_H
#define CREDENTIALS_H

#include <cstdint>

// ============================================================================
//  WI-FI CREDENTIALS
// ============================================================================

/** @brief Wi-Fi SSID for internet access (Telegram API). */
static constexpr const char* WIFI_SSID = "YOUR_WIFI_SSID";

/** @brief Wi-Fi password. */
static constexpr const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// ============================================================================
//  TELEGRAM BOT
// ============================================================================

/** @brief Telegram Bot API token (from @BotFather). Never log this value. */
static constexpr const char* BOT_TOKEN = "123456789:ABCdefGHIjklMNOpqrsTUVwxyz";

/** @brief Authorized Telegram chat ID. Only this chat may issue commands. */
static constexpr int64_t ALLOWED_CHAT_ID = 123456789;

// ============================================================================
//  OTA (Over-the-Air Updates)
// ============================================================================

/** @brief Password for OTA firmware updates. */
static constexpr const char* OTA_PASSWORD = "your_ota_password";

// ============================================================================
//  TARGET PC — WAKE-ON-LAN
// ============================================================================

/**
 * @brief MAC address of the target PC for Wake-on-LAN magic packets.
 * Format: six octets in network order.
 * Find with: Windows → ipconfig /all | Linux → ip link show
 */
static constexpr uint8_t TARGET_MAC[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

#endif // CREDENTIALS_H
