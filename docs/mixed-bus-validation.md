# Mixed-Bus Validation

This repository uses a two-profile mixed-bus validation flow:

1. Windows mock validation (`anolis-runtime.mixed.win.mock.yaml`).
2. Linux real-hardware validation (`anolis-runtime.mixed.yaml`).

Canonical inputs:

1. Config pack: `anolis/config/mixed-bus-providers/`
2. Commands: `anolis/config/mixed-bus-providers/README.md`

Linux hardware profile requirements:

1. `anolis-provider-bread` built with `dev-linux-hardware-release`.
2. `anolis-provider-ezo` built with `dev-linux-hardware-release`.
3. Bread config enforces `hardware.require_live_session: true` to fail fast if bread is not hardware-enabled.
4. In `anolis-provider-ezo`, `dev-linux-hardware-*` presets are cross-provider naming aliases.

Pass criteria:

1. Windows mock profile starts and serves runtime endpoints on port `18080`.
2. Windows mock inventory includes 6 devices (`rlht0`, `dcmt0`, `dcmt1`, `ph0`, `do0`, `ec0`).
3. Linux hardware profile starts and `anolis/config/mixed-bus-providers/check_mixed_bus_http.sh` exits `0`.
4. Linux hardware inventory includes 5 devices (`rlht0`, `dcmt0`, `dcmt1`, `ph0`, `do0`).
5. Linux hardware inventory aligns with CRUMBS lab map: `rlht0@0x0A`, `dcmt0@0x14`, `dcmt1@0x15`, `ph0@0x63`, `do0@0x61`.
6. Bosch optional validation from CRUMBS (`0x76`/`0x77`) remains outside provider mixed-bus scope.
