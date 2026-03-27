#include <gtest/gtest.h>

#include "config/provider_config.hpp"
#include "core/handlers.hpp"
#include "core/runtime_state.hpp"
#include "protocol.pb.h"

namespace {

anolis_provider_ezo::ProviderConfig make_stub_config() {
    anolis_provider_ezo::ProviderConfig config;
    config.provider_name = "ezo-lab";
    config.bus_path = "mock://unit-test-i2c";
    config.query_delay_us = 300000;
    config.timeout_ms = 300;
    config.retry_count = 2;
    config.devices = {
        anolis_provider_ezo::DeviceSpec{"ph0", anolis_provider_ezo::EzoDeviceType::Ph, "Tank pH", 0x63},
        anolis_provider_ezo::DeviceSpec{"do0", anolis_provider_ezo::EzoDeviceType::Do, "Tank DO", 0x61},
    };
    return config;
}

TEST(HandlersTest, HelloReturnsProviderMetadata) {
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(make_stub_config());

    anolis::deviceprovider::v1::HelloRequest request;
    request.set_protocol_version("v1");
    request.set_client_name("test-client");

    anolis::deviceprovider::v1::Response response;
    anolis_provider_ezo::handlers::handle_hello(request, response);

    EXPECT_EQ(response.status().code(), anolis::deviceprovider::v1::Status::CODE_OK);
    EXPECT_EQ(response.hello().provider_name(), "anolis-provider-ezo");
    EXPECT_EQ(response.hello().metadata().at("discovery_mode"), "manual");
    EXPECT_EQ(response.hello().metadata().at("i2c_execution_model"), "single_executor");
}

TEST(HandlersTest, HelloRejectsWrongProtocolVersion) {
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(make_stub_config());

    anolis::deviceprovider::v1::HelloRequest request;
    request.set_protocol_version("v0");

    anolis::deviceprovider::v1::Response response;
    anolis_provider_ezo::handlers::handle_hello(request, response);

    EXPECT_EQ(response.status().code(), anolis::deviceprovider::v1::Status::CODE_FAILED_PRECONDITION);
}

TEST(HandlersTest, WaitReadyAndGetHealthReturnOk) {
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(make_stub_config());

    anolis::deviceprovider::v1::Response wait_response;
    anolis_provider_ezo::handlers::handle_wait_ready(
        anolis::deviceprovider::v1::WaitReadyRequest{},
        wait_response);
    EXPECT_EQ(wait_response.status().code(), anolis::deviceprovider::v1::Status::CODE_OK);
    EXPECT_EQ(wait_response.wait_ready().diagnostics().at("ready"), "true");

    anolis::deviceprovider::v1::Response health_response;
    anolis_provider_ezo::handlers::handle_get_health(
        anolis::deviceprovider::v1::GetHealthRequest{},
        health_response);
    EXPECT_EQ(health_response.status().code(), anolis::deviceprovider::v1::Status::CODE_OK);
    EXPECT_EQ(health_response.get_health().provider().state(),
              anolis::deviceprovider::v1::ProviderHealth::STATE_OK);
}

TEST(HandlersTest, ListDevicesIsEmptyInPhaseTwoSkeleton) {
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(make_stub_config());

    anolis::deviceprovider::v1::Response response;
    anolis_provider_ezo::handlers::handle_list_devices(
        anolis::deviceprovider::v1::ListDevicesRequest{},
        response);

    EXPECT_EQ(response.status().code(), anolis::deviceprovider::v1::Status::CODE_OK);
    EXPECT_EQ(response.list_devices().devices_size(), 0);
}

} // namespace
