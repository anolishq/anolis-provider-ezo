#include "devices/common/device_adapter.hpp"

#include "devices/common/sample_helpers.hpp"
#include "devices/do/do_adapter.hpp"
#include "devices/ec/ec_adapter.hpp"
#include "devices/hum/hum_adapter.hpp"
#include "devices/orp/orp_adapter.hpp"
#include "devices/ph/ph_adapter.hpp"
#include "devices/rtd/rtd_adapter.hpp"

namespace anolis_provider_ezo::devices {
namespace {

i2c::Status unknown_read_sample(ezo_i2c_device_t * /*device*/, std::vector<runtime::SignalSample> & /*out*/) {
    return make_status(i2c::StatusCode::Internal, "unsupported device type in sample read");
}

void unknown_build_mock_sample(int /*address*/, uint64_t /*sequence*/, std::vector<runtime::SignalSample> & /*out*/) {}

const DeviceAdapter kUnknownAdapter{EZO_PRODUCT_UNKNOWN,  "sensor.ezo.unknown",      nullptr, 0,
                                    &unknown_read_sample, &unknown_build_mock_sample};

}  // namespace

const DeviceAdapter &adapter_for(EzoDeviceType type) {
    switch (type) {
        case EzoDeviceType::Ph:
            return ph::kAdapter;
        case EzoDeviceType::Orp:
            return orp::kAdapter;
        case EzoDeviceType::Ec:
            return ec::kAdapter;
        case EzoDeviceType::Do:
            return do_::kAdapter;
        case EzoDeviceType::Rtd:
            return rtd::kAdapter;
        case EzoDeviceType::Hum:
            return hum::kAdapter;
    }
    return kUnknownAdapter;
}

}  // namespace anolis_provider_ezo::devices
