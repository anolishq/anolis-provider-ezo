#pragma once

#include "devices/common/device_adapter.hpp"

// Namespace `do_` (trailing underscore) because `do` is a C++ keyword; the
// device family / type_id is still "do" (sensor.ezo.do).
namespace anolis_provider_ezo::devices::do_ {

extern const DeviceAdapter kAdapter;

}  // namespace anolis_provider_ezo::devices::do_
