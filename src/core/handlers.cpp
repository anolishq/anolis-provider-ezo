#include "core/handlers.hpp"

#include <string>

#include "core/health.hpp"
#include "core/runtime_state.hpp"
#include "core/transport/framed_stdio.hpp"

namespace anolis_provider_ezo::handlers {
namespace {

using Status = anolis::deviceprovider::v1::Status;

void set_status_ok(Response &response) {
    response.mutable_status()->set_code(Status::CODE_OK);
    response.mutable_status()->set_message("ok");
}

void set_status(Response &response, Status::Code code, const std::string &message) {
    response.mutable_status()->set_code(code);
    response.mutable_status()->set_message(message);
}

} // namespace

void handle_hello(const HelloRequest &request, Response &response) {
    if(request.protocol_version() != "v1") {
        set_status(response, Status::CODE_FAILED_PRECONDITION,
                   "unsupported protocol_version; expected v1");
        return;
    }

    auto *hello = response.mutable_hello();
    hello->set_protocol_version("v1");
    hello->set_provider_name("anolis-provider-ezo");
    hello->set_provider_version(ANOLIS_PROVIDER_EZO_VERSION);
    (*hello->mutable_metadata())["transport"] = "stdio+uint32_le";
    (*hello->mutable_metadata())["max_frame_bytes"] = std::to_string(transport::kMaxFrameBytes);
    (*hello->mutable_metadata())["supports_wait_ready"] = "true";
    (*hello->mutable_metadata())["discovery_mode"] = "manual";
    (*hello->mutable_metadata())["phase"] = "2";
    (*hello->mutable_metadata())["i2c_execution_model"] = "single_executor";
    set_status_ok(response);
}

void handle_wait_ready(const WaitReadyRequest &, Response &response) {
    auto *out = response.mutable_wait_ready();
    health::populate_wait_ready(runtime::snapshot(), *out);
    set_status_ok(response);
}

void handle_list_devices(const ListDevicesRequest &, Response &response) {
    // Phase 2 still intentionally returns empty inventory.
    (void)response.mutable_list_devices();
    set_status_ok(response);
}

void handle_get_health(const GetHealthRequest &, Response &response) {
    const runtime::RuntimeState state = runtime::snapshot();
    auto *out = response.mutable_get_health();
    *out->mutable_provider() = health::make_provider_health(state);
    for(const auto &device_health : health::make_device_health(state)) {
        *out->add_devices() = device_health;
    }
    set_status_ok(response);
}

void handle_unimplemented(Response &response, const std::string &message) {
    set_status(response, Status::CODE_UNIMPLEMENTED, message);
}

} // namespace anolis_provider_ezo::handlers
