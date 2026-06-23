#pragma once

/**
 * @file framed_stdio.hpp
 * @brief Length-prefixed stdio framing helpers for the provider's ADPP
 * transport loop.
 */

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace anolis_provider_ezo::transport {

/** @brief Upper bound for one framed stdio payload accepted by the transport
 * loop. */
constexpr uint32_t kMaxFrameBytes = 1024u * 1024u;

/** @brief Read exactly `size` bytes or fail on EOF/stream error. */
bool read_exact(std::istream &input, uint8_t *buffer, size_t size);

/**
 * @brief Read one little-endian length-prefixed frame from stdin.
 *
 * `false` with an empty `error` means clean EOF before any new frame started.
 */
bool read_frame(std::istream &input, std::vector<uint8_t> &out, std::string &error, uint32_t max_len = kMaxFrameBytes);

/** @brief Write one little-endian length-prefixed frame to stdout and flush it.
 */
bool write_frame(std::ostream &output, const uint8_t *data, size_t size, std::string &error,
                 uint32_t max_len = kMaxFrameBytes);

}  // namespace anolis_provider_ezo::transport
