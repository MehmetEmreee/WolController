/**
 * @file watchdog.h
 * @brief Async ping-based watchdog with explicit state machine, WoL trigger,
 *        and NVS-persisted enabled/disabled state.
 *
 * Implements a cooperative, non-blocking watchdog that monitors PC availability
 * via ICMP ping over the Ethernet interface. When consecutive ping failures
 * exceed the configured threshold, a Wake-on-LAN packet is automatically sent.
 *
 * The watchdog enabled/disabled state is persisted to NVS so that it survives
 * power cycles and soft-resets. On first boot, WATCHDOG_DEFAULT_ENABLED
 * (config.h) determines the initial state.
 *
 * State Machine:
 *   IDLE → MONITORING → PINGING → FAILED → RECOVERING → MONITORING
 *                ↑                                            |
 *                └────────────────────────────────────────────┘
 *   Any state → IDLE (on disable)
 *
 * All timing uses millis()-based cooperative scheduling — zero blocking calls.
 */

#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <ESP32Ping.h>
#include <Preferences.h>
#include "config.h"
#include "logger.h"
#include "network_mgr.h"
#include "wol.h"

/** @brief Module tag for watchdog-related log messages. */
static constexpr const char* WD_TAG = "watchdog";

/**
 * @brief Watchdog state machine states.
 */
enum class WatchdogState : uint8_t {
    IDLE,        ///< Watchdog disabled, not monitoring.
    MONITORING,  ///< Active, waiting for next ping interval.
    PINGING,     ///< Ping in progress (transitions immediately).
    FAILED,      ///< Failure threshold exceeded, preparing WoL.
    RECOVERING   ///< WoL sent, waiting for cooldown before re-monitoring.
};

/**
 * @brief Convert WatchdogState enum to human-readable string.
 * @param state The watchdog state.
 * @return Static string representation.
 */
inline const char* watchdogStateToStr(WatchdogState state) {
    switch (state) {
        case WatchdogState::IDLE:       return "IDLE";
        case WatchdogState::MONITORING: return "MONITORING";
        case WatchdogState::PINGING:    return "PINGING";
        case WatchdogState::FAILED:     return "FAILED";
        case WatchdogState::RECOVERING: return "RECOVERING";
        default:                        return "UNKNOWN";
    }
}

/**
 * @brief Ping result structure.
 */
struct PingResult {
    bool reachable;   ///< true if target responded to ICMP echo.
    float latencyMs;  ///< Round-trip time in milliseconds (0 if unreachable).
};

/**
 * @brief Async ping-based PC watchdog with WoL auto-recovery.
 *
 * Justified singleton: Single watchdog monitoring a single target PC.
 * Mutable state: state machine, fail counters, timing, NVS persistence.
 */
class Watchdog {
public:
    /**
     * @brief Get the singleton Watchdog instance.
     * @return Reference to the global Watchdog.
     */
    static Watchdog& instance() {
        static Watchdog wd;
        return wd;
    }

    /**
     * @brief Initialize watchdog and restore persisted state from NVS.
     *
     * Reads the saved enabled/disabled flag from NVS. If the watchdog was
     * enabled before the last reboot (or if this is the first boot and
     * WATCHDOG_DEFAULT_ENABLED is true), it will be auto-enabled.
     *
     * Auto-enable is deferred — the watchdog state is set to PENDING so
     * that update() can wait for Ethernet to connect before activating.
     *
     * @return Result indicating initialization success.
     */
    Result begin() {
        prefs_.begin(NVS_NAMESPACE, false);

        // Read persisted state. On first boot, key won't exist → use default.
        bool shouldEnable = prefs_.getBool(NVS_KEY_WATCHDOG, WATCHDOG_DEFAULT_ENABLED);

        if (shouldEnable) {
            pendingAutoEnable_ = true;
            LOG_INFO(WD_TAG, "Watchdog will auto-enable once Ethernet is ready "
                     "(persisted state: ON)");
        } else {
            LOG_INFO(WD_TAG, "Watchdog starts disabled (persisted state: OFF)");
        }

        return {true, nullptr};
    }

    /**
     * @brief Enable the watchdog — transition from IDLE to MONITORING.
     *
     * Resets all counters and begins the ping/monitor cycle.
     * Persists the enabled state to NVS.
     *
     * @return Result indicating if watchdog was successfully enabled.
     */
    Result enable() {
        if (state_ != WatchdogState::IDLE) {
            LOG_WARN(WD_TAG, "Watchdog already active (state: %s)",
                     watchdogStateToStr(state_));
            return {false, "Watchdog already active"};
        }

        if (!NetMgr::instance().isEthConnected()) {
            LOG_ERROR(WD_TAG, "Cannot enable watchdog — Ethernet not connected");
            return {false, "Ethernet not connected"};
        }

        failCount_ = 0;
        wolRetries_ = 0;
        lastPingMs_ = millis();
        transitionTo(WatchdogState::MONITORING);

        // Persist enabled state to NVS
        persistState(true);

        LOG_INFO(WD_TAG, "Watchdog enabled — monitoring %s every %ums, threshold: %u",
                 PC_IP_ADDR,
                 WATCHDOG_PING_INTERVAL_MS,
                 WATCHDOG_FAIL_THRESHOLD);

        return {true, nullptr};
    }

    /**
     * @brief Disable the watchdog — transition any state to IDLE.
     *
     * Persists the disabled state to NVS.
     *
     * @return Result (always succeeds).
     */
    Result disable() {
        WatchdogState prevState = state_;
        failCount_ = 0;
        wolRetries_ = 0;
        pendingAutoEnable_ = false;
        transitionTo(WatchdogState::IDLE);

        // Persist disabled state to NVS
        persistState(false);

        LOG_INFO(WD_TAG, "Watchdog disabled (was %s)", watchdogStateToStr(prevState));
        return {true, nullptr};
    }

    /**
     * @brief Cooperative update — call every loop() iteration.
     *
     * Drives the state machine forward based on elapsed time.
     * Also handles deferred auto-enable when Ethernet becomes available.
     * All operations are non-blocking.
     */
    void update() {
        unsigned long now = millis();

        // Handle deferred auto-enable: wait for Ethernet before starting
        if (pendingAutoEnable_ && state_ == WatchdogState::IDLE) {
            if (NetMgr::instance().isEthConnected()) {
                pendingAutoEnable_ = false;
                LOG_INFO(WD_TAG, "Ethernet connected — auto-enabling watchdog");
                enable();
            }
            return;  // Don't run state machine while waiting
        }

        switch (state_) {
            case WatchdogState::IDLE:
                // Nothing to do
                break;

            case WatchdogState::MONITORING:
                if (now - lastPingMs_ >= WATCHDOG_PING_INTERVAL_MS) {
                    transitionTo(WatchdogState::PINGING);
                    executePing(now);
                }
                break;

            case WatchdogState::PINGING:
                // Ping is synchronous but fast (~2s max via PING_TIMEOUT_MS)
                // This state is transient — handled in executePing()
                break;

            case WatchdogState::FAILED:
                executeWolRecovery(now);
                break;

            case WatchdogState::RECOVERING:
                if (now - recoveryStartMs_ >= WATCHDOG_RECOVERY_COOLDOWN_MS) {
                    LOG_INFO(WD_TAG, "Recovery cooldown elapsed, resuming monitoring");
                    failCount_ = 0;
                    transitionTo(WatchdogState::MONITORING);
                    lastPingMs_ = now;
                }
                break;
        }
    }

    /**
     * @brief Perform a one-shot ping to the target PC.
     *
     * Can be called independently of the watchdog state machine
     * (e.g., by the /status command).
     *
     * @return PingResult with reachability and latency.
     */
    PingResult pingOnce() const {
        if (!NetMgr::instance().isEthConnected()) {
            LOG_WARN(WD_TAG, "Ping skipped — Ethernet not connected");
            return {false, 0.0f};
        }

        IPAddress target;
        if (!target.fromString(PC_IP_ADDR)) {
            LOG_ERROR(WD_TAG, "Invalid target IP: %s", PC_IP_ADDR);
            return {false, 0.0f};
        }

        unsigned long start = millis();
        bool success = Ping.ping(target, 1);  // Single ping, uses PING_TIMEOUT_MS
        float latency = static_cast<float>(millis() - start);

        if (success) {
            latency = Ping.averageTime();
        }

        return {success, latency};
    }

    /**
     * @brief Get the current watchdog state.
     * @return Current WatchdogState enum value.
     */
    WatchdogState getState() const { return state_; }

    /**
     * @brief Get the current consecutive failure count.
     * @return Number of consecutive ping failures.
     */
    uint8_t getFailCount() const { return failCount_; }

    /**
     * @brief Get the number of WoL retries attempted in current recovery.
     * @return WoL retry count.
     */
    uint8_t getWolRetries() const { return wolRetries_; }

    /**
     * @brief Check if watchdog is actively monitoring.
     * @return true if state is not IDLE.
     */
    bool isActive() const { return state_ != WatchdogState::IDLE; }

    /**
     * @brief Check if auto-enable is pending (waiting for Ethernet).
     * @return true if watchdog is waiting to auto-enable.
     */
    bool isPendingAutoEnable() const { return pendingAutoEnable_; }

    // Delete copy/move
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;
    Watchdog(Watchdog&&) = delete;
    Watchdog& operator=(Watchdog&&) = delete;

private:
    Watchdog() = default;

    /** @brief Current state machine state. */
    WatchdogState state_ = WatchdogState::IDLE;

    /** @brief Consecutive ping failure counter. */
    uint8_t failCount_ = 0;

    /** @brief WoL retry counter during RECOVERING state. */
    uint8_t wolRetries_ = 0;

    /** @brief Timestamp of last ping attempt. */
    unsigned long lastPingMs_ = 0;

    /** @brief Timestamp of recovery phase start. */
    unsigned long recoveryStartMs_ = 0;

    /** @brief Flag for deferred auto-enable (waiting for Ethernet). */
    bool pendingAutoEnable_ = false;

    /** @brief NVS preferences handle for watchdog persistence. */
    Preferences prefs_;

    /**
     * @brief Persist the watchdog enabled/disabled state to NVS.
     * @param enabled true to persist as enabled, false as disabled.
     */
    void persistState(bool enabled) {
        prefs_.putBool(NVS_KEY_WATCHDOG, enabled);
        LOG_INFO(WD_TAG, "Persisted watchdog state: %s", enabled ? "ON" : "OFF");
    }

    /**
     * @brief Transition the state machine and log the change.
     * @param newState Target state.
     */
    void transitionTo(WatchdogState newState) {
        if (state_ != newState) {
            LOG_INFO(WD_TAG, "State: %s → %s",
                     watchdogStateToStr(state_),
                     watchdogStateToStr(newState));
            state_ = newState;
        }
    }

    /**
     * @brief Execute a ping and handle the result.
     *
     * On success: reset fail counter, return to MONITORING.
     * On failure: increment fail counter, check threshold.
     *
     * @param now Current millis() timestamp.
     */
    void executePing(unsigned long now) {
        PingResult result = pingOnce();
        lastPingMs_ = now;

        if (result.reachable) {
            if (failCount_ > 0) {
                LOG_INFO(WD_TAG, "Ping recovered after %u failures (%.0fms)",
                         failCount_, result.latencyMs);
            }
            failCount_ = 0;
            transitionTo(WatchdogState::MONITORING);
        } else {
            failCount_++;
            LOG_WARN(WD_TAG, "Ping failed (attempt %u/%u)",
                     failCount_, WATCHDOG_FAIL_THRESHOLD);

            if (failCount_ >= WATCHDOG_FAIL_THRESHOLD) {
                LOG_ERROR(WD_TAG, "Failure threshold reached (%u/%u) — triggering WoL",
                          failCount_, WATCHDOG_FAIL_THRESHOLD);
                transitionTo(WatchdogState::FAILED);
            } else {
                transitionTo(WatchdogState::MONITORING);
            }
        }
    }

    /**
     * @brief Execute WoL recovery sequence.
     *
     * Sends a magic packet and transitions to RECOVERING with cooldown.
     * If max WoL retries exhausted, disables watchdog.
     *
     * @param now Current millis() timestamp.
     */
    void executeWolRecovery(unsigned long now) {
        if (wolRetries_ >= WATCHDOG_MAX_WOL_RETRIES) {
            LOG_ERROR(WD_TAG, "Max WoL retries (%u) exhausted — disabling watchdog",
                      WATCHDOG_MAX_WOL_RETRIES);
            transitionTo(WatchdogState::IDLE);
            failCount_ = 0;
            wolRetries_ = 0;
            // Note: don't persist OFF here — user likely wants it to
            // auto-enable again after next reboot
            return;
        }

        wolRetries_++;
        LOG_INFO(WD_TAG, "Sending WoL (retry %u/%u)...",
                 wolRetries_, WATCHDOG_MAX_WOL_RETRIES);

        Result wolResult = WolSender::instance().send();
        if (!wolResult.ok) {
            LOG_ERROR(WD_TAG, "WoL send failed: %s", wolResult.error);
        }

        recoveryStartMs_ = now;
        transitionTo(WatchdogState::RECOVERING);
    }
};

#endif // WATCHDOG_H
