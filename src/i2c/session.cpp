/**
 * @file session.cpp
 * @brief Session implementations for mock and Linux I2C transports.
 */

#include "i2c/session.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace anolis_provider_ezo::i2c {
namespace {

Status make_status(StatusCode code, const std::string &message) { return Status{code, message}; }

}  // namespace

Status Status::ok() { return Status{}; }

bool Status::is_ok() const { return code == StatusCode::Ok; }

void IoStatsMap::record(uint8_t address, bool ok, int attempts) {
    std::lock_guard<std::mutex> lock(mutex_);
    IoStats &stats = stats_[address];
    if (ok) {
        ++stats.ok;
    } else {
        ++stats.failed;
    }
    if (attempts > 1) {
        stats.retried_attempts += static_cast<uint64_t>(attempts - 1);
    }
}

IoStats IoStatsMap::stats_for(uint8_t address) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = stats_.find(address);
    return it == stats_.end() ? IoStats{} : it->second;
}

NoopSession::NoopSession(std::string bus_path) : bus_path_(std::move(bus_path)) {}

Status NoopSession::open() {
    opened_ = true;
    return Status::ok();
}

void NoopSession::close() { opened_ = false; }

bool NoopSession::is_open() const { return opened_; }

const std::string &NoopSession::bus_path() const { return bus_path_; }

Status NoopSession::write_then_read(uint8_t, const uint8_t *, size_t, uint8_t *, size_t, size_t *rx_received) {
    if (!opened_) {
        return make_status(StatusCode::Unavailable, "session not open");
    }
    if (rx_received != nullptr) {
        *rx_received = 0;
    }
    return make_status(StatusCode::Unavailable, "hardware integration disabled in this build");
}

LinuxSession::LinuxSession(std::string bus_path, int timeout_ms, int retry_count)
    : bus_path_(std::move(bus_path)), timeout_ms_(std::max(timeout_ms, 1)), retry_count_(std::max(retry_count, 0)) {}

LinuxSession::~LinuxSession() { close(); }

Status LinuxSession::open() {
    if (opened_) {
        return Status::ok();
    }
    if (bus_path_.empty()) {
        return make_status(StatusCode::InvalidArgument, "bus_path is empty");
    }

#if defined(__linux__)
    fd_ = ::open(bus_path_.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        return make_status(StatusCode::Unavailable, "failed to open " + bus_path_ + ": " + std::strerror(errno));
    }

    // I2C_TIMEOUT unit is 10ms.
    const int timeout_10ms = std::max(1, timeout_ms_ / 10);
    (void)::ioctl(fd_, I2C_TIMEOUT, timeout_10ms);
    // Kernel retries are disabled so every attempt is performed (and counted)
    // by this session (ezo#100): i2c-core's I2C_RETRIES loop re-issues a
    // transfer only when the adapter returns -EAGAIN (arbitration lost), which
    // would be invisible to the io counters — and would multiply with the
    // userspace attempt loop below. hardware.retry_count remains the single
    // retry budget, applied in write_then_read(). NOTE: this setting is
    // ADAPTER-GLOBAL (i2c-dev writes adapter->retries, shared by every user of
    // the bus and persistent across process exits) — writing 0 also clears any
    // stale nonzero value a previous run left behind; 0 matches the
    // zero-initialized default of adapters that don't set their own.
    (void)::ioctl(fd_, I2C_RETRIES, 0);

    opened_ = true;
    return Status::ok();
#else
    return make_status(StatusCode::Unavailable, "Linux I2C session is only available on Linux builds");
#endif
}

void LinuxSession::close() {
#if defined(__linux__)
    if (fd_ >= 0) {
        (void)::close(fd_);
        fd_ = -1;
    }
#endif
    opened_ = false;
}

bool LinuxSession::is_open() const { return opened_; }

const std::string &LinuxSession::bus_path() const { return bus_path_; }

IoStats LinuxSession::io_stats_for(uint8_t address) const { return io_stats_.stats_for(address); }

Status LinuxSession::write_then_read(uint8_t address, const uint8_t *tx_data, size_t tx_len, uint8_t *rx_data,
                                     size_t rx_len, size_t *rx_received) {
    if (!opened_) {
        return make_status(StatusCode::Unavailable, "session not open");
    }
    if (tx_len == 0 && rx_len == 0) {
        return make_status(StatusCode::InvalidArgument, "write_then_read requires tx_len>0 or rx_len>0");
    }

#if defined(__linux__)
    // Use a single I2C_RDWR ioctl so write+read sequences preserve the kernel's
    // repeated-start semantics expected by the EZO command protocol.
    struct i2c_msg msgs[2];
    int msg_count = 0;

    if (tx_len > 0) {
        msgs[msg_count].addr = address;
        msgs[msg_count].flags = 0;
        msgs[msg_count].len = static_cast<__u16>(tx_len);
        msgs[msg_count].buf = const_cast<__u8 *>(reinterpret_cast<const __u8 *>(tx_data));
        ++msg_count;
    }

    if (rx_len > 0) {
        msgs[msg_count].addr = address;
        msgs[msg_count].flags = I2C_M_RD;
        msgs[msg_count].len = static_cast<__u16>(rx_len);
        msgs[msg_count].buf = reinterpret_cast<__u8 *>(rx_data);
        ++msg_count;
    }

    struct i2c_rdwr_ioctl_data ioctl_data;
    ioctl_data.msgs = msgs;
    ioctl_data.nmsgs = static_cast<__u32>(msg_count);

    const int max_attempts = std::max(retry_count_ + 1, 1);
    int attempts_made = 0;
    int last_errno = 0;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        ++attempts_made;
        if (::ioctl(fd_, I2C_RDWR, &ioctl_data) >= 0) {
            if (rx_received != nullptr) {
                *rx_received = rx_len;
            }
            io_stats_.record(address, true, attempts_made);
            return Status::ok();
        }

        // Capture before record(): the stats mutex/map-insert may clobber errno.
        last_errno = errno;
        if (last_errno != EINTR && last_errno != EAGAIN) {
            break;
        }
    }

    io_stats_.record(address, false, attempts_made);
    return make_status(StatusCode::Unavailable, "I2C_RDWR failed on " + bus_path_ + ": " + std::strerror(last_errno));
#else
    (void)address;
    (void)tx_data;
    (void)tx_len;
    (void)rx_data;
    (void)rx_len;
    (void)rx_received;
    return make_status(StatusCode::Unavailable, "Linux I2C session is only available on Linux builds");
#endif
}

}  // namespace anolis_provider_ezo::i2c
