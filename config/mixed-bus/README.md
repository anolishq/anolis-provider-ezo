# Mixed-Bus Config Pack

This directory is intentionally minimal.

## Canonical configs

1. `anolis-runtime.mixed-baseline.windows.mock.yaml`:
   - Windows local validation without EZO hardware.
   - Uses EZO mock provider config.
   - HTTP port `18080`.
2. `anolis-runtime.mixed-baseline.yaml`:
   - Linux real hardware validation.
   - Uses real EZO provider config.
   - HTTP port `8080`.
3. `provider-bread.baseline.yaml`: BREAD provider config.
4. `provider-ezo.baseline.mock.yaml`: EZO mock provider config (`hardware.bus_path: mock://mixed-bus`).
5. `provider-ezo.baseline.yaml`: EZO real hardware provider config (`hardware.bus_path: /dev/i2c-1`).

## Run commands

Use [`COMMANDS.md`](./COMMANDS.md) for exact execution commands.
