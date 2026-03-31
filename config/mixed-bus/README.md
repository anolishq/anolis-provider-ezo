# Mixed-Bus Config Pack

This directory contains canonical runtime/provider config sets for running `bread0` + `ezo0` together.

## Profiles

1. `anolis-runtime.mixed-baseline.windows.mock.yaml`
   - Windows local validation without EZO hardware.
   - Uses `provider-bread.baseline.mock.yaml` + `provider-ezo.baseline.mock.yaml`.
   - HTTP port `18080`.
2. `anolis-runtime.mixed-baseline.yaml`
   - Linux real-hardware baseline validation.
   - Uses `provider-bread.baseline.yaml` + `provider-ezo.baseline.yaml`.
   - Requires `anolis-provider-bread` built with `dev-linux-hardware-release`.
   - Requires `anolis-provider-ezo` built with `dev-linux-hardware-release`.
   - In `anolis-provider-ezo`, `dev-linux-hardware-*` presets are naming aliases used for cross-provider operator consistency.
   - `provider-bread.baseline.yaml` sets `hardware.require_live_session: true` to fail fast on non-hardware bread builds.
   - HTTP port `8080`.
3. `anolis-runtime.mixed-lab.yaml`
   - Linux real-hardware lab profile for current topology.
   - Uses `provider-bread.lab.yaml` + `provider-ezo.lab.yaml`.
   - Requires `anolis-provider-bread` built with `dev-linux-hardware-release`.
   - Requires `anolis-provider-ezo` built with `dev-linux-hardware-release`.
   - In `anolis-provider-ezo`, `dev-linux-hardware-*` presets are naming aliases used for cross-provider operator consistency.
   - `provider-bread.lab.yaml` sets `hardware.require_live_session: true` to fail fast on non-hardware bread builds.
   - HTTP port `8080`.

## Provider configs

1. `provider-bread.baseline.yaml` (BREAD baseline Linux hardware profile, requires live session)
2. `provider-bread.baseline.mock.yaml` (BREAD baseline Windows mock profile, config-seeded mode)
3. `provider-ezo.baseline.yaml` (EZO baseline: `pH`, `DO`, `EC`)
4. `provider-ezo.baseline.mock.yaml` (EZO mock, bus path `mock://mixed-bus`)
5. `provider-bread.lab.yaml` (BREAD lab: RLHT `0x0A`, DCMT `0x14`, DCMT `0x15`)
6. `provider-ezo.lab.yaml` (EZO lab: pH `0x63`, DO `0x61`)

## Run commands

Use [`COMMANDS.md`](./COMMANDS.md) for exact commands, including preset-based build prerequisites for `anolis`, `anolis-provider-bread`, and `anolis-provider-ezo`.
