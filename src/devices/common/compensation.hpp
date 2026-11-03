#pragma once

/**
 * @file compensation.hpp
 * @brief Temperature, salinity and pressure compensation values (#146).
 */

#include <optional>
#include <string>

namespace anolis_provider_ezo::devices {

// The values set_compensation accepts. Temperature covers liquid water; the
// salinity bound is twice seawater's conductivity; pressure runs from high
// altitude to a vessel at about 3 bar absolute.
inline constexpr double kMinTemperatureC = 0.0;
inline constexpr double kMaxTemperatureC = 100.0;
inline constexpr double kMinSalinityUsCm = 0.0;
inline constexpr double kMaxSalinityUsCm = 100000.0;
inline constexpr double kMinPressureKpa = 10.0;
inline constexpr double kMaxPressureKpa = 300.0;

// What a DO probe compensates for at power-up, and what it must be calibrated
// at (DO datasheet): 20 C, salinity 0, 101.3 kPa.
inline constexpr double kDoDefaultTemperatureC = 20.0;
inline constexpr double kDoDefaultSalinityUsCm = 0.0;
inline constexpr double kDoDefaultPressureKpa = 101.3;

/** @brief Compensation values; an empty field is one not set, or not known. */
struct Compensation {
    std::optional<double> temperature_c;
    std::optional<double> salinity_us_cm;
    std::optional<double> pressure_kpa;

    bool empty() const { return !temperature_c && !salinity_us_cm && !pressure_kpa; }
};

/** @brief What a probe reported for its compensation (T,?, S,?, P,?). */
struct CompensationReading {
    Compensation values;
    // Set when the probe reports salinity in ppt: the signal is published in
    // uS/cm, and the two units do not convert without the solution's makeup.
    std::string salinity_unavailable;
};

}  // namespace anolis_provider_ezo::devices
