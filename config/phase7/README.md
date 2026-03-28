# Phase 7 Mixed-Bus Config Pack

These configs are the canonical Phase 7 validation inputs for running `anolis-provider-bread` and `anolis-provider-ezo` together.

## Files

1. `provider-bread.baseline.yaml`: baseline BREAD provider config.
2. `provider-ezo.baseline.yaml`: baseline EZO provider config.
3. `provider-ezo.conflict-shadow.yaml`: intentionally overlapping EZO config used to trigger ownership conflict.
4. `anolis-runtime.mixed-baseline.yaml`: baseline mixed runtime config.
5. `anolis-runtime.mixed-conflict.yaml`: mixed runtime config with an intentional duplicate ownership claim.

## Path assumptions

Runtime configs assume:

1. You launch `anolis-runtime` from the `anolis` repo root.
2. Provider binaries exist at:
   - `../anolis-provider-bread/build/dev-release/anolis-provider-bread`
   - `../anolis-provider-ezo/build/dev-release/anolis-provider-ezo`
3. Config files are read from `../anolis-provider-ezo/config/phase7/`.

Adjust `providers[].command` and `providers[].args` paths as needed for your local build layout.
