#include "i2c/ezo_i2c_bridge.hpp"

#include <gtest/gtest.h>

#include "i2c/noop_i2c_bus.hpp"

namespace {

using anolis_provider_ezo::i2c::bind_ezo_i2c_device;
using anolis_provider_ezo::i2c::EzoDeviceBinding;
using anolis_provider_ezo::i2c::NoopI2cBus;

TEST(EzoI2cBridgeTest, BindsDeviceThroughBusTransport) {
    NoopI2cBus bus("mock://bridge");
    ASSERT_TRUE(static_cast<bool>(bus.open()));

    EzoDeviceBinding binding;
    const auto status = bind_ezo_i2c_device(bus, 0x63, binding);
    ASSERT_TRUE(status.is_ok()) << status.message;

    EXPECT_TRUE(binding.initialized);
    EXPECT_EQ(ezo_device_get_address(&binding.device), static_cast<uint8_t>(0x63));
}

}  // namespace
