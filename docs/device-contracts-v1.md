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

## Function IDs (per-type, contiguous from 1)

Every EZO family exposes the same three control functions, numbered `{1..3}` per
the Anolis executable profile (§4):

1. `1` -> `find`
2. `2` -> `set_led`
3. `3` -> `sleep`

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
2. Signal IDs are flat lower_snake_case (`^[a-z][a-z0-9_]*$`), per the Anolis executable profile §4.
