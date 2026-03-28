# anolis-provider-ezo

EZO sensor hardware provider for the Anolis runtime.

## Current status
1. Runnable ADPP provider with config parsing, framed stdio transport, and unit tests.
2. Serialized I2C execution model with Linux and mock session support.
3. Full EZO family coverage (`ph`, `orp`, `ec`, `do`, `rtd`, `hum`) with fixed signal surfaces.
4. Safe control functions across families (`find`, `set_led`, `sleep`).
5. Startup identity verification with active/excluded inventory diagnostics.
6. Mixed-bus validation assets for Windows mock and Linux real hardware.
7. Runtime duplicate ownership validation integration landed in `anolis`.

## Build
Requires local access to:
1. `anolis-protocol` (default: `../anolis-protocol` or `external/anolis-protocol`)
2. `ezo-driver` (default: `../ezo-driver` or `external/ezo-driver`)

```bash
cmake -S . -B build/dev -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dev
```

## Test
```bash
ctest --test-dir build/dev --output-on-failure
```

## Run
```bash
./build/dev/anolis-provider-ezo --check-config config/example.local.yaml
./build/dev/anolis-provider-ezo --config config/example.local.yaml
```

## Validation Assets
1. Config pack: `config/mixed-bus/`
2. Command runbook: `config/mixed-bus/COMMANDS.md`
3. HTTP validation script (Linux hardware path): `scripts/mixed-bus/check_mixed_bus_http.sh`
4. Validation summary: `docs/mixed-bus-validation.md`

## Docs
1. [docs/README.md](docs/README.md)
2. Planning notes: `working/` (gitignored)
