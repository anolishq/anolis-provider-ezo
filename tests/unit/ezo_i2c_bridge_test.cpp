#include <gtest/gtest.h>

#include "i2c/ezo_i2c_bridge.hpp"
#include "i2c/session.hpp"

namespace {

using anolis_provider_ezo::i2c::EzoDeviceBinding;
using anolis_provider_ezo::i2c::NoopSession;
using anolis_provider_ezo::i2c::bind_ezo_i2c_device;

TEST(EzoI2cBridgeTest, BindsDeviceThroughSessionTransport) {
    NoopSession session("mock://bridge");
    ASSERT_TRUE(session.open().is_ok());

    EzoDeviceBinding binding;
    const auto status = bind_ezo_i2c_device(session, 0x63, binding);
    ASSERT_TRUE(status.is_ok()) << status.message;

    EXPECT_TRUE(binding.initialized);
    EXPECT_EQ(ezo_device_get_address(&binding.device), static_cast<uint8_t>(0x63));
}

} // namespace
