# Mixed-Bus Validation Evidence Template

## Run Metadata

1. Date/time:
2. Operator:
3. Runtime commit (`anolis`):
4. EZO provider commit:
5. BREAD provider commit:

## A) Windows Mock Validation

1. Runtime config: `anolis/config/mixed-bus-providers/anolis-runtime.mixed.win.mock.yaml`
2. Provider configs:
   - `anolis/config/mixed-bus-providers/provider-bread.mock.yaml`
   - `anolis/config/mixed-bus-providers/provider-ezo.mock.yaml`
3. Commands used: (from `anolis/config/mixed-bus-providers/README.md`)
4. `/v0/runtime/status` summary:
5. `/v0/providers/health` summary:
6. `/v0/devices` summary:
7. `/v0/state` summary:
8. Inventory count (expected 6):
9. Result: PASS/FAIL
10. Notes:

## B) Linux Hardware Validation

1. Runtime config: `anolis/config/mixed-bus-providers/anolis-runtime.mixed.yaml`
2. Provider configs:
   - `anolis/config/mixed-bus-providers/provider-bread.yaml`
   - `anolis/config/mixed-bus-providers/provider-ezo.yaml`
3. Hardware topology summary:
4. `anolis/config/mixed-bus-providers/check_mixed_bus_http.sh` command used:
5. Script output summary:
6. Captured artifacts directory:
7. Inventory count (expected 5):
8. Result: PASS/FAIL
9. Notes:

## Final Assessment

1. Overall mixed-bus validation status: PASS/FAIL
2. Open issues:
3. Go/no-go recommendation for release:
