/**
 * @file auth.h
 * @brief Whitelist-based Telegram chat ID authorization guard.
 *
 * Provides a simple, single-responsibility authorization check that must
 * be called BEFORE any command processing. Unauthorized access attempts
 * are logged with the offending chat ID and timestamp.
 *
 * Design decision: Single allowed chat ID (not a list) to minimize memory
 * footprint and attack surface. Extend to an array if multi-user is needed.
 */

#ifndef AUTH_H
#define AUTH_H

#include <cstdint>
#include "config.h"
#include "logger.h"

/** @brief Module tag for auth-related log messages. */
static constexpr const char* AUTH_TAG = "auth";

/**
 * @brief Authorization guard for Telegram commands.
 *
 * Validates incoming chat IDs against the compile-time whitelist.
 * Logs all unauthorized access attempts for security auditing.
 *
 * Justified singleton: Stateless validation logic bound to a single
 * configuration constant. No mutable state.
 */
class Auth {
public:
    /**
     * @brief Get the singleton Auth instance.
     * @return Reference to the global Auth instance.
     */
    static Auth& instance() {
        static Auth auth;
        return auth;
    }

    /**
     * @brief Check if a chat ID is authorized to issue commands.
     *
     * This function MUST be called before processing any Telegram command.
     * Unauthorized attempts are logged with severity ERROR.
     *
     * @param chatId The Telegram chat ID to validate.
     * @return true if the chat ID matches the whitelist, false otherwise.
     */
    bool isAuthorized(int64_t chatId) const {
        if (chatId == ALLOWED_CHAT_ID) {
            return true;
        }

        // Log unauthorized access attempt — security audit trail
        unauthorizedAttemptCount_++;
        LOG_ERROR(AUTH_TAG,
                  "Unauthorized access attempt #%u from chat ID: %lld (uptime: %lus)",
                  unauthorizedAttemptCount_,
                  static_cast<long long>(chatId),
                  millis() / 1000UL);

        return false;
    }

    /**
     * @brief Get the total number of unauthorized access attempts since boot.
     * @return Count of rejected authorization checks.
     */
    uint32_t getUnauthorizedCount() const {
        return unauthorizedAttemptCount_;
    }

    // Delete copy/move
    Auth(const Auth&) = delete;
    Auth& operator=(const Auth&) = delete;
    Auth(Auth&&) = delete;
    Auth& operator=(Auth&&) = delete;

private:
    Auth() = default;

    /**
     * @brief Running count of unauthorized access attempts.
     *
     * Mutable to allow incrementing from const isAuthorized().
     * Justified mutable state: monotonic counter for security auditing.
     */
    mutable uint32_t unauthorizedAttemptCount_ = 0;
};

#endif // AUTH_H
