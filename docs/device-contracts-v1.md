# EZO v1 Contracts

This document locks the v1 contract surface.

## Type IDs

1. `sensor.ezo.ph`
2. `sensor.ezo.orp`
3. `sensor.ezo.ec`
4. `sensor.ezo.do`
5. `sensor.ezo.rtd`
6. `sensor.ezo.hum`

## Shared safe functions (all families)

1. `find`
2. `set_led`
3. `sleep`

## Calibration functions (pH and DO)

`sensor.ezo.ph` and `sensor.ezo.do` also declare calibration. Both functions are
`CATEGORY_CONFIG`, not idempotent, and declare `allowed_modes: [MANUAL]`, so the
runtime refuses them in any other mode (runtime 0.6.0 or later enforces this).

| Function            | Family | Args                                                                                           | Device command                         |
| ------------------- | ------ | ---------------------------------------------------------------------------------------------- | -------------------------------------- |
| `calibrate`         | pH     | `point` (string: `mid`, `low`, `high`), `reference_ph` (double, 0 to 14, sent with 2 decimals) | `Cal,mid,n`, `Cal,low,n`, `Cal,high,n` |
| `calibrate`         | DO     | `point` (string: `atmospheric`, `zero`)                                                        | `Cal`, `Cal,0`                         |
| `clear_calibration` | pH, DO | none                                                                                           | `Cal,clear`                            |

Arguments are checked before any bus work: an unknown point, a wrong type, a
missing or extra arg, or a non-finite `reference_ph` is `INVALID_ARGUMENT`, and a
`reference_ph` outside 0 to 14 is `OUT_OF_RANGE`. `calibrate` then waits the
driver's calibration time (pH 900 ms, DO 1300 ms); `clear_calibration` waits a
generic command's 300 ms. Both then read the device's response code, as every
control call but `sleep` does: a rejection (code 2) fails the call.

Calibrating pH at `mid` clears the low and high points, so a full calibration
is mid, then low, then high. For a two-point DO calibration, do `zero` first.

### Calibration level

`ph_calibration_level` and `do_calibration_level` report the device's answer to
`Cal,?`: pH 0 (none), 1 (mid), 2 (mid and low) or 3 (mid, low and high); DO 0,
1 or 2 points. They are doubles, with no unit, outside the default signal set.

The level is read at startup and again after each successful `calibrate` or
`clear_calibration`, not on every poll; each sample carries the last value read.
A level that has not been read, or whose last read failed, is unavailable with
non-OK quality, never 0, so a failed read after a calibration cannot show the
level from before it.

## Compensation (pH and DO)

pH and DO readings depend on the solution's temperature, and DO also on its
salinity and the air pressure. A probe compensates for the values it holds:
pH 25 C; DO 20 C, salinity 0 and 101.3 kPa, until they are set. The probes
lose them when they lose power, and keep their calibration.

`set_compensation` (function 6) sets them. It is `CATEGORY_CONFIG` and
idempotent, and declares no `allowed_modes`, so an automation can set it from a
run's parameters as well as an operator in MANUAL.

| Family | Args (doubles; an optional one is sent only when given)                                          | Device commands                             |
| ------ | ------------------------------------------------------------------------------------------------ | ------------------------------------------- |
| pH     | `temperature_c` (0 to 100, required)                                                             | `T,n` (2 decimals)                          |
| DO     | `temperature_c` (0 to 100, required), `salinity_us_cm` (0 to 100000), `pressure_kpa` (10 to 300) | `T,n`, `S,n,uS` (1 decimal), `P,n` (2 dec.) |

As for calibration, arguments are checked before any bus work: an unknown,
missing or non-finite arg is `INVALID_ARGUMENT` and a value out of range is
`OUT_OF_RANGE`. Each command's response code is read, and the first one the
probe rejects fails the call and stops the rest.

### Compensation signals

`ph_temperature_compensation_c`, `do_temperature_compensation_c`,
`do_salinity_compensation_us_cm` and `do_pressure_compensation_kpa` report what
the probe answers to `T,?`, `S,?` and `P,?`: what it holds, not what was asked
for. They are outside the default signal set. They are read at startup, after
each `set_compensation` and DO `calibrate`, and every 24th sample (about once a
minute at the default 2500 ms interval); each sample carries the last values
read. A value not read yet, or whose last read failed, is unavailable, never a
default. A DO probe that reports salinity in ppt shows the salinity signal
unavailable, since ppt does not convert to uS/cm without the solution's makeup;
setting salinity switches the probe to uS/cm.

### After a power cut

When a check finds the probe no longer holds a value set through
`set_compensation`, the provider sends the set values again, logs a warning and
counts it in the device health metric `compensation_reapplied_count`. The set
values live in the provider's memory only. After a provider restart the signals
show what the probe holds, which it keeps unless it lost power too, and nothing
is re-sent until `set_compensation` is called again.

### DO calibration

The DO datasheet calibrates at the default compensation. DO `calibrate` sets
20 C, salinity 0 and 101.3 kPa, calibrates, then sends back the values set
through `set_compensation`, or else those the probe held before. If the probe
refuses the defaults, the calibration is not sent and the call fails. pH
`calibrate` is unchanged.

## Function IDs (per-type, contiguous from 1)

Every EZO family exposes the same three control functions, numbered `{1..3}` per
the Anolis executable profile (§5):

1. `1` -> `find`
2. `2` -> `set_led`
3. `3` -> `sleep`

pH and DO continue the numbering with `4` -> `calibrate`,
`5` -> `clear_calibration` and `6` -> `set_compensation`.

Adding functions is additive, so `type_version` stays `1`.

## Signal table

| Family | Type ID          | Signal IDs                                                                                                                                                | Notes                                                       |
| ------ | ---------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------- |
| pH     | `sensor.ezo.ph`  | `ph_value`, `ph_calibration_level`, `ph_temperature_compensation_c`                                                                                       | see calibration level and compensation above                |
| ORP    | `sensor.ezo.orp` | `orp_millivolts`                                                                                                                                          | scalar                                                      |
| EC     | `sensor.ezo.ec`  | `ec_conductivity_us_cm`, `ec_tds_ppm`, `ec_salinity_psu`, `ec_specific_gravity`                                                                           | fixed signal set; unavailable outputs return non-OK quality |
| DO     | `sensor.ezo.do`  | `do_mg_l`, `do_saturation_pct`, `do_calibration_level`, `do_temperature_compensation_c`, `do_salinity_compensation_us_cm`, `do_pressure_compensation_kpa` | fixed signal set; unavailable outputs return non-OK quality |
| RTD    | `sensor.ezo.rtd` | `rtd_temperature_c`                                                                                                                                       | scalar                                                      |
| HUM    | `sensor.ezo.hum` | `hum_relative_humidity_pct`, `hum_temperature_c`, `hum_dew_point_c`                                                                                       | fixed signal set; unavailable outputs return non-OK quality |

## Quality policy

1. Do not remove configured signal IDs from the contract surface at runtime.
2. When an output is unavailable on-device, return the signal with non-OK quality and explanatory metadata.
3. Use explicit timestamps for sampled values.

## Notes

1. HUM/EC/DO field-level mappings are fixed for v1; unavailable outputs are represented with non-OK quality and metadata, not by removing signals.
2. Signal IDs are flat lower_snake_case (`^[a-z][a-z0-9_]*$`), per the Anolis executable profile §5.
