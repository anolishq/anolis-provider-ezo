# Mixed-Bus Config Pack

This directory contains canonical runtime/provider config sets for running `bread0` + `ezo0` together.

## Runtime Profiles

1. `anolis-runtime.mixed.win.mock.yaml`
   - Windows local validation without EZO hardware.
   - Uses `provider-bread.mock.yaml` + `provider-ezo.mock.yaml`.
   - HTTP port `18080`.
   - Expected inventory: 6 devices (`rlht0`, `dcmt0`, `dcmt1`, `ph0`, `do0`, `ec0`).

2. `anolis-runtime.mixed.yaml`
   - Linux real-hardware validation profile.
   - Uses `provider-bread.yaml` + `provider-ezo.yaml`.
   - Address map aligns with CRUMBS `mixed_bus_lab_validation`: RLHT `0x0A`, DCMT `0x14`, DCMT `0x15`, EZO pH `0x63`, EZO DO `0x61`.
   - Bosch `0x76`/`0x77` checks from CRUMBS are optional and intentionally outside provider mixed-bus scope.
   - Requires `anolis-provider-bread` built with `dev-linux-hardware-release`.
   - Requires `anolis-provider-ezo` built with `dev-linux-hardware-release`.
   - `provider-bread.yaml` sets `hardware.require_live_session: true` to fail fast on non-hardware bread builds.
   - Polling interval is tuned to `2500ms` to avoid state-cache overruns with serialized EZO reads.
   - HTTP port `8080`.
   - Expected inventory: 5 devices (`rlht0`, `dcmt0`, `dcmt1`, `ph0`, `do0`).

## Provider Configs

1. `provider-bread.yaml` (BREAD Linux hardware profile, requires live session)
2. `provider-bread.mock.yaml` (BREAD Windows mock profile)
3. `provider-ezo.yaml` (EZO Linux hardware profile: `ph0`, `do0`)
4. `provider-ezo.mock.yaml` (EZO Windows mock profile: `ph0`, `do0`, `ec0`)

## Run Commands

Use [`COMMANDS.md`](./COMMANDS.md) for exact build and run commands.
