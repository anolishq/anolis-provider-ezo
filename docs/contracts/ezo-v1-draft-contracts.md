# EZO v1 Contracts

This document locks the v1 contract surface. Signal details were finalized during Phase 04.

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

## Function IDs (provider-local stable IDs)
1. `1001` -> `find`
2. `1002` -> `set_led`
3. `1003` -> `sleep`

## Signal table

| Family | Type ID | Signal IDs | Notes |
|---|---|---|---|
| pH | `sensor.ezo.ph` | `ph.value` | scalar |
| ORP | `sensor.ezo.orp` | `orp.millivolts` | scalar |
| EC | `sensor.ezo.ec` | `ec.conductivity_us_cm`, `ec.tds_ppm`, `ec.salinity_psu`, `ec.specific_gravity` | fixed signal set; unavailable outputs return non-OK quality |
| DO | `sensor.ezo.do` | `do.mg_l`, `do.saturation_pct` | fixed signal set; unavailable outputs return non-OK quality |
| RTD | `sensor.ezo.rtd` | `rtd.temperature_c` | scalar |
| HUM | `sensor.ezo.hum` | `hum.relative_humidity_pct`, `hum.temperature_c`, `hum.dew_point_c` | fixed signal set; unavailable outputs return non-OK quality |

## Quality policy
1. Do not remove configured signal IDs from the contract surface at runtime.
2. When an output is unavailable on-device, return the signal with non-OK quality and explanatory metadata.
3. Use explicit timestamps for sampled values.

## Notes
1. HUM/EC/DO field-level mappings are fixed for v1; unavailable outputs are represented with non-OK quality and metadata, not by removing signals.
2. Signal IDs are lower_snake_case segments with dot-separated namespace prefixes.
