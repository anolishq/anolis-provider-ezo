#pragma once

/**
 * @file session.hpp
 * @brief I2C session abstractions used by the provider's serialized bus
 * executor.
 */

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace anolis_provider_ezo::i2c {

/**
 * @brief Provider-local status codes for I2C/session operations.
 */
enum class StatusCode {
    Ok = 0,
    InvalidArgument,
    NotFound,
    Unavailable,
    DeadlineExceeded,
    Cancelled,
    Internal,
};

/**
 * @brief Lightweight status object returned by session and executor APIs.
 */
struct Status {
    StatusCode code = StatusCode::Ok;
    std::string message = "ok";

    /** @brief Construct the canonical success status. */
    static Status ok();

    /** @brief Report whether the status represents success. */
    bool is_ok() const;
};

/**
 * @brief Cumulative per-address transport I/O statistics (ezo#100).
 *
 * These counters measure BUS-LEVEL health only: a transaction that completes
 * electrically but carries a garbage or not-ready EZO payload counts as `ok`
 * here — protocol-level failures surface in the per-device `sample_*` /
 * `call_*` counters instead. `retried_attempts` counts every attempt beyond
 * an operation's first, so failures masked by an eventually-successful retry
 * stay visible. Operations that never reach the transport (validation or
 * not-open errors) are not I/O and are not counted.
 */
struct IoStats {
    uint64_t ok = 0;
    uint64_t failed = 0;
    uint64_t retried_attempts = 0;
};

/**
 * @brief Mutex-guarded per-address accumulator for `IoStats`.
 *
 * Owned by the session implementation that performs the attempts (the only
 * layer where attempt counts are honest). Written from the bus worker thread,
 * read from the health path.
 */
class IoStatsMap {
public:
    /** @brief Record one completed transport operation of `attempts` tries. */
    void record(uint8_t address, bool ok, int attempts);

    /** @brief Return a copy of the stats for one address (zeros if unseen). */
    IoStats stats_for(uint8_t address) const;

private:
    mutable std::mutex mutex_;
    std::map<uint8_t, IoStats> stats_;
};

/**
 * @brief Abstract blocking I2C session interface.
 *
 * Implementations are expected to provide a single-session view of one bus
 * path. Higher-level serialization is handled by `BusExecutor`.
 */
class ISession {
public:
    virtual ~ISession() = default;

    /** @brief Open the underlying bus/session resources. */
    virtual Status open() = 0;

    /** @brief Close the underlying bus/session resources. */
    virtual void close() = 0;

    /** @brief Report whether the session is currently open. */
    virtual bool is_open() const = 0;

    /** @brief Return the configured bus path associated with this session. */
    virtual const std::string &bus_path() const = 0;

    /**
     * @brief Execute one combined write/read transaction against a device
     * address.
     *
     * Implementations block until the transfer completes or fails.
     *
     * @param address 7-bit I2C device address
     * @param tx_data Bytes to write before the read phase
     * @param tx_len Number of write bytes
     * @param rx_data Output buffer for read bytes
     * @param rx_len Requested read length
     * @param rx_received Output count of bytes actually read, when available
     * @return Operation status
     */
    virtual Status write_then_read(uint8_t address, const uint8_t *tx_data, size_t tx_len, uint8_t *rx_data,
                                   size_t rx_len, size_t *rx_received) = 0;

    /**
     * @brief Return per-address transport I/O statistics.
     *
     * Default: zeros. Only sessions that perform real transport attempts
     * (`LinuxSession`) accumulate; `NoopSession` carries no traffic.
     */
    virtual IoStats io_stats_for(uint8_t address) const {
        (void)address;
        return IoStats{};
    }
};

/**
 * @brief No-hardware session used for mock or unsupported builds.
 */
class NoopSession final : public ISession {
public:
    explicit NoopSession(std::string bus_path);

    Status open() override;
    void close() override;
    bool is_open() const override;
    const std::string &bus_path() const override;

    Status write_then_read(uint8_t address, const uint8_t *tx_data, size_t tx_len, uint8_t *rx_data, size_t rx_len,
                           size_t *rx_received) override;

private:
    std::string bus_path_;
    bool opened_ = false;
};

/**
 * @brief Linux `I2C_RDWR` session implementation.
 *
 * Uses one open file descriptor for a bus path and applies transport timeout
 * and retry settings during open.
 */
class LinuxSession final : public ISession {
public:
    LinuxSession(std::string bus_path, int timeout_ms, int retry_count);
    ~LinuxSession() override;

    Status open() override;
    void close() override;
    bool is_open() const override;
    const std::string &bus_path() const override;

    Status write_then_read(uint8_t address, const uint8_t *tx_data, size_t tx_len, uint8_t *rx_data, size_t rx_len,
                           size_t *rx_received) override;

    IoStats io_stats_for(uint8_t address) const override;

private:
    std::string bus_path_;
    int timeout_ms_ = 300;
    int retry_count_ = 2;
    int fd_ = -1;
    bool opened_ = false;
    IoStatsMap io_stats_;
};

}  // namespace anolis_provider_ezo::i2c
