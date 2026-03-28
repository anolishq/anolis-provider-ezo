# Mixed-Bus Validation Evidence Template

## Run Metadata

1. Date/time:
2. Operator:
3. Runtime commit (`anolis`):
4. EZO provider commit:
5. BREAD provider commit:

## A) Windows Mock Validation

1. Runtime config: `config/mixed-bus/anolis-runtime.mixed-baseline.windows.mock.yaml`
2. Commands used: (from `config/mixed-bus/COMMANDS.md`)
3. `/v0/runtime/status` summary:
4. `/v0/providers/health` summary:
5. `/v0/devices` summary:
6. `/v0/state` summary:
7. Result: PASS/FAIL
8. Notes:

## B) Linux Hardware Validation

1. Runtime config: `config/mixed-bus/anolis-runtime.mixed-baseline.yaml`
2. Provider configs:
   - `config/mixed-bus/provider-bread.baseline.yaml`
   - `config/mixed-bus/provider-ezo.baseline.yaml`
3. Hardware topology summary:
4. `check_mixed_bus_http.sh` command used:
5. Script output summary:
6. Captured artifacts directory:
7. Result: PASS/FAIL
8. Notes:

## Final Assessment

1. Overall mixed-bus validation status: PASS/FAIL
2. Open issues:
3. Go/no-go recommendation for release:
