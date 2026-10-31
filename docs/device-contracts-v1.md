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
`reference_ph` outside 0 to 14 is `OUT_OF_RANGE`. The call then waits the
driver's calibration time (pH 900 ms, DO 1300 ms) and reads the device's
response code, as every control call but `sleep` does: a rejection (code 2)
fails the call.

Calibrating pH at `mid` clears the low and high points, so a full calibration
is mid, then low, then high. For a two-point DO calibration, do `zero` first.

## Function IDs (per-type, contiguous from 1)

Every EZO family exposes the same three control functions, numbered `{1..3}` per
the Anolis executable profile (§5):

1. `1` -> `find`
2. `2` -> `set_led`
3. `3` -> `sleep`

pH and DO continue the numbering with `4` -> `calibrate` and
`5` -> `clear_calibration`.

Adding functions is additive, so `type_version` stays `1`.

## Signal table

| Family | Type ID          | Signal IDs                                                                      | Notes                                                       |
| ------ | ---------------- | ------------------------------------------------------------------------------- | ----------------------------------------------------------- |
| pH     | `sensor.ezo.ph`  | `ph_value`                                                                      | scalar                                                      |
| ORP    | `sensor.ezo.orp` | `orp_millivolts`                                                                | scalar                                                      |
| EC     | `sensor.ezo.ec`  | `ec_conductivity_us_cm`, `ec_tds_ppm`, `ec_salinity_psu`, `ec_specific_gravity` | fixed signal set; unavailable outputs return non-OK quality |
| DO     | `sensor.ezo.do`  | `do_mg_l`, `do_saturation_pct`                                                  | fixed signal set; unavailable outputs return non-OK quality |
| RTD    | `sensor.ezo.rtd` | `rtd_temperature_c`                                                             | scalar                                                      |
| HUM    | `sensor.ezo.hum` | `hum_relative_humidity_pct`, `hum_temperature_c`, `hum_dew_point_c`             | fixed signal set; unavailable outputs return non-OK quality |

## Quality policy

1. Do not remove configured signal IDs from the contract surface at runtime.
2. When an output is unavailable on-device, return the signal with non-OK quality and explanatory metadata.
3. Use explicit timestamps for sampled values.

## Notes

1. HUM/EC/DO field-level mappings are fixed for v1; unavailable outputs are represented with non-OK quality and metadata, not by removing signals.
2. Signal IDs are flat lower_snake_case (`^[a-z][a-z0-9_]*$`), per the Anolis executable profile §5.
