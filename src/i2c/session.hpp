#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace anolis_provider_ezo::i2c {

enum class StatusCode {
    Ok = 0,
    InvalidArgument,
    Unavailable,
    DeadlineExceeded,
    Cancelled,
    Internal,
};

struct Status {
    StatusCode code = StatusCode::Ok;
    std::string message = "ok";

    static Status ok();
    bool is_ok() const;
};

class ISession {
public:
    virtual ~ISession() = default;

    virtual Status open() = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    virtual const std::string &bus_path() const = 0;

    virtual Status write_then_read(uint8_t address,
                                   const uint8_t *tx_data,
                                   size_t tx_len,
                                   uint8_t *rx_data,
                                   size_t rx_len,
                                   size_t *rx_received) = 0;
};

class NoopSession final : public ISession {
public:
    explicit NoopSession(std::string bus_path);

    Status open() override;
    void close() override;
    bool is_open() const override;
    const std::string &bus_path() const override;

    Status write_then_read(uint8_t address,
                           const uint8_t *tx_data,
                           size_t tx_len,
                           uint8_t *rx_data,
                           size_t rx_len,
                           size_t *rx_received) override;

private:
    std::string bus_path_;
    bool opened_ = false;
};

class LinuxSession final : public ISession {
public:
    LinuxSession(std::string bus_path, int timeout_ms, int retry_count);
    ~LinuxSession() override;

    Status open() override;
    void close() override;
    bool is_open() const override;
    const std::string &bus_path() const override;

    Status write_then_read(uint8_t address,
                           const uint8_t *tx_data,
                           size_t tx_len,
                           uint8_t *rx_data,
                           size_t rx_len,
                           size_t *rx_received) override;

private:
    std::string bus_path_;
    int timeout_ms_ = 300;
    int retry_count_ = 2;
    int fd_ = -1;
    bool opened_ = false;
};

} // namespace anolis_provider_ezo::i2c
