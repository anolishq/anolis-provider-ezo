#include <gtest/gtest.h>

#include "config/provider_config.hpp"
#include "core/handlers.hpp"
#include "core/runtime_state.hpp"
#include "protocol.pb.h"

namespace {

anolis_provider_ezo::ProviderConfig make_full_config() {
    anolis_provider_ezo::ProviderConfig config;
    config.provider_name = "ezo-lab";
    config.bus_path = "mock://unit-test-i2c";
    config.query_delay_us = 300000;
    config.timeout_ms = 300;
    config.retry_count = 2;
    config.devices = {
        anolis_provider_ezo::DeviceSpec{"ph0", anolis_provider_ezo::EzoDeviceType::Ph, "Tank pH", 0x63},
        anolis_provider_ezo::DeviceSpec{"orp0", anolis_provider_ezo::EzoDeviceType::Orp, "Tank ORP", 0x62},
        anolis_provider_ezo::DeviceSpec{"ec0", anolis_provider_ezo::EzoDeviceType::Ec, "Tank EC", 0x64},
        anolis_provider_ezo::DeviceSpec{"do0", anolis_provider_ezo::EzoDeviceType::Do, "Tank DO", 0x61},
        anolis_provider_ezo::DeviceSpec{"rtd0", anolis_provider_ezo::EzoDeviceType::Rtd, "Tank RTD", 0x66},
        anolis_provider_ezo::DeviceSpec{"hum0", anolis_provider_ezo::EzoDeviceType::Hum, "Tank HUM", 0x6F},
    };
    return config;
}

anolis_provider_ezo::ProviderConfig make_mismatch_config() {
    anolis_provider_ezo::ProviderConfig config;
    config.provider_name = "ezo-lab";
    config.bus_path = "mock://unit-test-i2c";
    config.query_delay_us = 300000;
    config.timeout_ms = 300;
    config.retry_count = 2;
    config.devices = {
        anolis_provider_ezo::DeviceSpec{"ph0", anolis_provider_ezo::EzoDeviceType::Ph, "Tank pH", 0x63},
        anolis_provider_ezo::DeviceSpec{"wrong0", anolis_provider_ezo::EzoDeviceType::Ph, "Wrong", 0x61},
    };
    return config;
}

TEST(HandlersTest, HelloReturnsPhaseFourMetadata) {
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(make_full_config());

    anolis::deviceprovider::v1::HelloRequest request;
    request.set_protocol_version("v1");
    request.set_client_name("test-client");

    anolis::deviceprovider::v1::Response response;
    anolis_provider_ezo::handlers::handle_hello(request, response);

    EXPECT_EQ(response.status().code(), anolis::deviceprovider::v1::Status::CODE_OK);
    EXPECT_EQ(response.hello().provider_name(), "anolis-provider-ezo");
    EXPECT_EQ(response.hello().metadata().at("phase"), "4");
    EXPECT_EQ(response.hello().metadata().at("coverage"), "all_families");
}

TEST(HandlersTest, HelloRejectsWrongProtocolVersion) {
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(make_full_config());

    anolis::deviceprovider::v1::HelloRequest request;
    request.set_protocol_version("v0");

    anolis::deviceprovider::v1::Response response;
    anolis_provider_ezo::handlers::handle_hello(request, response);

    EXPECT_EQ(response.status().code(), anolis::deviceprovider::v1::Status::CODE_FAILED_PRECONDITION);
}

TEST(HandlersTest, WaitReadyAndGetHealthReflectActiveAndExcludedDevices) {
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(make_mismatch_config());

    anolis::deviceprovider::v1::Response wait_response;
    anolis_provider_ezo::handlers::handle_wait_ready(
        anolis::deviceprovider::v1::WaitReadyRequest{},
        wait_response);
    EXPECT_EQ(wait_response.status().code(), anolis::deviceprovider::v1::Status::CODE_OK);
    EXPECT_EQ(wait_response.wait_ready().diagnostics().at("ready"), "true");
    EXPECT_EQ(wait_response.wait_ready().diagnostics().at("active_device_count"), "1");
    EXPECT_EQ(wait_response.wait_ready().diagnostics().at("excluded_device_count"), "1");

    anolis::deviceprovider::v1::Response health_response;
    anolis_provider_ezo::handlers::handle_get_health(
        anolis::deviceprovider::v1::GetHealthRequest{},
        health_response);
    EXPECT_EQ(health_response.status().code(), anolis::deviceprovider::v1::Status::CODE_OK);
    EXPECT_EQ(health_response.get_health().devices_size(), 2);
}

TEST(HandlersTest, ListDevicesReturnsOnlyActiveInventoryAcrossFamilies) {
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(make_full_config());

    anolis::deviceprovider::v1::ListDevicesRequest request;
    request.set_include_health(true);

    anolis::deviceprovider::v1::Response response;
    anolis_provider_ezo::handlers::handle_list_devices(request, response);

    EXPECT_EQ(response.status().code(), anolis::deviceprovider::v1::Status::CODE_OK);
    ASSERT_EQ(response.list_devices().devices_size(), 6);
    EXPECT_EQ(response.list_devices().devices(0).type_id(), "sensor.ezo.ph");
    EXPECT_EQ(response.list_devices().devices(3).type_id(), "sensor.ezo.do");
    EXPECT_EQ(response.list_devices().devices(0).tags().at("hw.bus_path"), "mock://unit-test-i2c");
    EXPECT_EQ(response.list_devices().devices(0).tags().at("hw.i2c_address"), "0x63");
    ASSERT_EQ(response.list_devices().device_health_size(), 6);
}

TEST(HandlersTest, DescribeDeviceReturnsDoCapabilities) {
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(make_full_config());

    anolis::deviceprovider::v1::DescribeDeviceRequest request;
    request.set_device_id("do0");

    anolis::deviceprovider::v1::Response response;
    anolis_provider_ezo::handlers::handle_describe_device(request, response);

    EXPECT_EQ(response.status().code(), anolis::deviceprovider::v1::Status::CODE_OK);
    EXPECT_EQ(response.describe_device().device().device_id(), "do0");
    ASSERT_EQ(response.describe_device().capabilities().signals_size(), 2);
    EXPECT_EQ(response.describe_device().capabilities().signals(0).signal_id(), "do.mg_l");
    EXPECT_EQ(response.describe_device().capabilities().signals(1).signal_id(), "do.saturation_pct");
}

TEST(HandlersTest, ReadSignalsReturnsDoFixedSignalSurfaceWithUnavailableMetadata) {
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(make_full_config());

    anolis::deviceprovider::v1::ReadSignalsRequest request;
    request.set_device_id("do0");

    anolis::deviceprovider::v1::Response response;
    anolis_provider_ezo::handlers::handle_read_signals(request, response);

    EXPECT_EQ(response.status().code(), anolis::deviceprovider::v1::Status::CODE_OK);
    EXPECT_EQ(response.read_signals().device_id(), "do0");
    ASSERT_EQ(response.read_signals().values_size(), 2);
    EXPECT_EQ(response.read_signals().values(0).signal_id(), "do.mg_l");
    EXPECT_EQ(response.read_signals().values(0).quality(),
              anolis::deviceprovider::v1::SignalValue::QUALITY_OK);
    EXPECT_EQ(response.read_signals().values(1).signal_id(), "do.saturation_pct");
    EXPECT_EQ(response.read_signals().values(1).quality(),
              anolis::deviceprovider::v1::SignalValue::QUALITY_UNKNOWN);
    EXPECT_EQ(response.read_signals().values(1).metadata().at("unavailable"), "true");
}

TEST(HandlersTest, ReadSignalsReturnsEcFixedSignalSurfaceWithUnavailableMetadata) {
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(make_full_config());

    anolis::deviceprovider::v1::ReadSignalsRequest request;
    request.set_device_id("ec0");

    anolis::deviceprovider::v1::Response response;
    anolis_provider_ezo::handlers::handle_read_signals(request, response);

    EXPECT_EQ(response.status().code(), anolis::deviceprovider::v1::Status::CODE_OK);
    ASSERT_EQ(response.read_signals().values_size(), 4);
    EXPECT_EQ(response.read_signals().values(0).signal_id(), "ec.conductivity_us_cm");
    EXPECT_EQ(response.read_signals().values(2).signal_id(), "ec.salinity_psu");
    EXPECT_EQ(response.read_signals().values(2).quality(),
              anolis::deviceprovider::v1::SignalValue::QUALITY_UNKNOWN);
    EXPECT_EQ(response.read_signals().values(2).metadata().at("unavailable"), "true");
}

TEST(HandlersTest, ReadSignalsReturnsOrpScalarSignal) {
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(make_full_config());

    anolis::deviceprovider::v1::ReadSignalsRequest request;
    request.set_device_id("orp0");
    request.add_signal_ids("orp.millivolts");

    anolis::deviceprovider::v1::Response response;
    anolis_provider_ezo::handlers::handle_read_signals(request, response);

    EXPECT_EQ(response.status().code(), anolis::deviceprovider::v1::Status::CODE_OK);
    ASSERT_EQ(response.read_signals().values_size(), 1);
    EXPECT_EQ(response.read_signals().values(0).signal_id(), "orp.millivolts");
    EXPECT_EQ(response.read_signals().values(0).quality(),
              anolis::deviceprovider::v1::SignalValue::QUALITY_OK);
}

TEST(HandlersTest, ReadSignalsReturnsRtdScalarSignal) {
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(make_full_config());

    anolis::deviceprovider::v1::ReadSignalsRequest request;
    request.set_device_id("rtd0");
    request.add_signal_ids("rtd.temperature_c");

    anolis::deviceprovider::v1::Response response;
    anolis_provider_ezo::handlers::handle_read_signals(request, response);

    EXPECT_EQ(response.status().code(), anolis::deviceprovider::v1::Status::CODE_OK);
    ASSERT_EQ(response.read_signals().values_size(), 1);
    EXPECT_EQ(response.read_signals().values(0).signal_id(), "rtd.temperature_c");
    EXPECT_EQ(response.read_signals().values(0).quality(),
              anolis::deviceprovider::v1::SignalValue::QUALITY_OK);
}

TEST(HandlersTest, ReadSignalsReturnsHumFixedSignalSurfaceWithUnavailableMetadata) {
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(make_full_config());

    anolis::deviceprovider::v1::ReadSignalsRequest request;
    request.set_device_id("hum0");

    anolis::deviceprovider::v1::Response response;
    anolis_provider_ezo::handlers::handle_read_signals(request, response);

    EXPECT_EQ(response.status().code(), anolis::deviceprovider::v1::Status::CODE_OK);
    ASSERT_EQ(response.read_signals().values_size(), 3);
    EXPECT_EQ(response.read_signals().values(0).signal_id(), "hum.relative_humidity_pct");
    EXPECT_EQ(response.read_signals().values(0).quality(),
              anolis::deviceprovider::v1::SignalValue::QUALITY_OK);
    EXPECT_EQ(response.read_signals().values(2).signal_id(), "hum.dew_point_c");
    EXPECT_EQ(response.read_signals().values(2).quality(),
              anolis::deviceprovider::v1::SignalValue::QUALITY_UNKNOWN);
    EXPECT_EQ(response.read_signals().values(2).metadata().at("unavailable"), "true");
}

TEST(HandlersTest, ReadSignalsRejectsUnknownSignalId) {
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(make_full_config());

    anolis::deviceprovider::v1::ReadSignalsRequest request;
    request.set_device_id("ph0");
    request.add_signal_ids("ph.bad");

    anolis::deviceprovider::v1::Response response;
    anolis_provider_ezo::handlers::handle_read_signals(request, response);

    EXPECT_EQ(response.status().code(), anolis::deviceprovider::v1::Status::CODE_NOT_FOUND);
}

} // namespace
