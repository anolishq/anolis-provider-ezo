# Mixed-Bus Config Pack

This directory contains canonical runtime/provider config sets for running `bread0` + `ezo0` together.

## Profiles

1. `anolis-runtime.mixed-baseline.windows.mock.yaml`
   - Windows local validation without EZO hardware.
   - Uses `provider-ezo.baseline.mock.yaml`.
   - HTTP port `18080`.
2. `anolis-runtime.mixed-baseline.yaml`
   - Linux real-hardware baseline validation.
   - Uses `provider-bread.baseline.yaml` + `provider-ezo.baseline.yaml`.
   - HTTP port `8080`.
3. `anolis-runtime.mixed-lab.yaml`
   - Linux real-hardware lab profile for current topology.
   - Uses `provider-bread.lab.yaml` + `provider-ezo.lab.yaml`.
   - HTTP port `8080`.

## Provider configs

1. `provider-bread.baseline.yaml` (BREAD baseline: `0x08`, `0x09`, `0x0A`)
2. `provider-ezo.baseline.yaml` (EZO baseline: `pH`, `DO`, `EC`)
3. `provider-ezo.baseline.mock.yaml` (EZO mock, bus path `mock://mixed-bus`)
4. `provider-bread.lab.yaml` (BREAD lab: RLHT `0x0A`, DCMT `0x14`, DCMT `0x15`)
5. `provider-ezo.lab.yaml` (EZO lab: pH `0x63`, DO `0x61`)

## Run commands

Use [`COMMANDS.md`](./COMMANDS.md) for exact commands, including preset-based build prerequisites for `anolis`, `anolis-provider-bread`, and `anolis-provider-ezo`.
