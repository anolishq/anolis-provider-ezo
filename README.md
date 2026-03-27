# anolis-provider-ezo

EZO sensor hardware provider for the Anolis runtime.

## Current status
1. Phase 0 implemented: architecture + contract lock docs in `docs/`.
2. Phase 1 implemented: runnable ADPP provider skeleton with config parsing, framed stdio transport, and baseline unit tests.
3. Phase 2 implemented: serialized I2C executor, Linux/mock session layer, `ezo-driver` I2C bridge, and executor behavior unit tests.

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

## Docs
1. [docs/README.md](docs/README.md)
2. Planning notes: `working/` (gitignored)
