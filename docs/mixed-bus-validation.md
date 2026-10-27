# Mixed-Bus Validation

The mixed-bus validation flow in `anolis-projects/projects/mixed-bus-dev/` uses three runtime profiles:

1. Windows mock validation (`anolis-runtime.mixed.win.mock.yaml`).
2. Linux real-hardware validation (`anolis-runtime.mixed.yaml`).
3. Windows mock ownership-conflict negative test (`anolis-runtime.mixed.win.mock.conflict.yaml`), expected to fail fast at startup.

Canonical inputs:

1. Config pack: `anolis-projects/projects/mixed-bus-dev/config/`
2. Commands: `anolis-projects/projects/mixed-bus-dev/README.md`

Linux hardware profile requirements:

1. `anolis-provider-bread` built with `dev-release`.
2. `anolis-provider-ezo` built with `dev-linux-hardware-release`.
3. Bread config sets a real `hardware.bus_path` (`/dev/i2c-1`); if bread cannot open the CRUMBS session on that bus it stays up and reports not ready with the reason. Bread rejects a `require_live_session` key as unknown.
4. In `anolis-provider-ezo`, `dev-linux-hardware-*` presets are aliases of `dev-debug` and `dev-release`; bread has no `dev-linux-hardware-*` presets.

Pass criteria:

1. Windows mock profile starts and serves runtime endpoints on port `18080`.
2. Windows mock inventory includes 6 devices (`rlht0`, `dcmt0`, `dcmt1`, `ph0`, `do0`, `ec0`).
3. Linux hardware profile starts and `anolis-projects/projects/mixed-bus-dev/validation/check_mixed_bus_http.sh` exits `0`.
4. Linux hardware inventory includes 5 devices (`rlht0`, `dcmt0`, `dcmt1`, `ph0`, `do0`).
5. Linux hardware inventory aligns with CRUMBS lab map: `rlht0@0x13`, `dcmt0@0x14`, `dcmt1@0x15`, `ph0@0x63`, `do0@0x61`.
6. Bosch optional validation from CRUMBS (`0x76`/`0x77`) remains outside provider mixed-bus scope.
