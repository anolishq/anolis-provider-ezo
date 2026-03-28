# Mixed-Bus Validation

This repository uses a minimal two-profile flow:

1. Windows mock validation.
2. Linux real-hardware validation.

Canonical inputs:

1. Config pack: [`config/mixed-bus/`](../config/mixed-bus/README.md)
2. Commands: [`config/mixed-bus/COMMANDS.md`](../config/mixed-bus/COMMANDS.md)

Pass criteria:

1. Windows mock profile starts and serves runtime endpoints.
2. Linux hardware profile starts and `check_mixed_bus_http.sh` exits `0`.
