# Mixed-Bus Validation

This repository uses a minimal profile flow:

1. Windows mock validation.
2. Linux real-hardware baseline validation.
3. Linux real-hardware lab validation.

Canonical inputs:

1. Config pack: [`config/mixed-bus/`](../config/mixed-bus/README.md)
2. Commands: [`config/mixed-bus/COMMANDS.md`](../config/mixed-bus/COMMANDS.md) (includes preset-based build prerequisites)

Pass criteria:

1. Windows mock profile starts and serves runtime endpoints.
2. Linux baseline profile starts and `check_mixed_bus_http.sh` exits `0`.
3. Linux lab profile starts and `check_mixed_bus_http.sh` exits `0` with expected lab inventory.
