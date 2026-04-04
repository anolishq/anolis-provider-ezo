#pragma once

/**
 * @file handlers.hpp
 * @brief ADPP request handlers backed by the provider runtime state.
 */

#include <string>

#include "protocol.pb.h"

namespace anolis_provider_ezo::handlers {

using CallRequest = anolis::deviceprovider::v1::CallRequest;
using DescribeDeviceRequest = anolis::deviceprovider::v1::DescribeDeviceRequest;
using GetHealthRequest = anolis::deviceprovider::v1::GetHealthRequest;
using HelloRequest = anolis::deviceprovider::v1::HelloRequest;
using ListDevicesRequest = anolis::deviceprovider::v1::ListDevicesRequest;
using ReadSignalsRequest = anolis::deviceprovider::v1::ReadSignalsRequest;
using Response = anolis::deviceprovider::v1::Response;
using WaitReadyRequest = anolis::deviceprovider::v1::WaitReadyRequest;

/** @brief Handle the ADPP `Hello` handshake and advertise provider capabilities. */
void handle_hello(const HelloRequest &request, Response &response);

/** @brief Report provider readiness and startup diagnostics. */
void handle_wait_ready(const WaitReadyRequest &request, Response &response);

/** @brief List active devices and optional per-device health snapshots. */
void handle_list_devices(const ListDevicesRequest &request, Response &response);

/** @brief Describe one active device and its fixed capability surface. */
void handle_describe_device(const DescribeDeviceRequest &request, Response &response);

/**
 * @brief Return signal values for one device, refreshing the cached sample when needed.
 */
void handle_read_signals(const ReadSignalsRequest &request, Response &response);

/**
 * @brief Execute one safe control function through the serialized I2C executor.
 */
void handle_call(const CallRequest &request, Response &response);

/** @brief Return provider and device health summaries. */
void handle_get_health(const GetHealthRequest &request, Response &response);

/** @brief Return a standard unimplemented response for unsupported operations. */
void handle_unimplemented(Response &response, const std::string &message = "operation not implemented");

} // namespace anolis_provider_ezo::handlers
