# Phase 07 Mixed-Bus Validation (BREAD + EZO)

This runbook defines the canonical mixed-bus system validation flow after Phase 06 ownership validation landed in `anolis`.

## Scope

1. Baseline mixed deployment stability (`anolis-provider-bread` + `anolis-provider-ezo` on one bus).
2. Fault-isolation checks (single device failures do not collapse unrelated devices/providers).
3. Ownership-conflict startup rejection checks.

## Inputs

1. Config pack: [`config/phase7/`](../../config/phase7/README.md)
2. HTTP validation script: [`scripts/phase7/check_mixed_bus_http.sh`](../../scripts/phase7/check_mixed_bus_http.sh)
3. Conflict assertion script: [`scripts/phase7/assert_runtime_conflict_rejected.sh`](../../scripts/phase7/assert_runtime_conflict_rejected.sh)
4. Evidence template: [`phase-07-validation-evidence-template.md`](phase-07-validation-evidence-template.md)

## Preconditions

1. Runtime ownership validation commit is present in `anolis`.
2. Provider binaries are built and reachable by paths in runtime config.
3. I2C hardware wiring and addresses are already validated at provider-local level.

## 1) Baseline Mixed Deployment

From `anolis` repo root:

```bash
./build/dev-release/anolis-runtime --config ../anolis-provider-ezo/config/phase7/anolis-runtime.mixed-baseline.yaml
```

In a second terminal, run the HTTP check:

```bash
cd ../anolis-provider-ezo
./scripts/phase7/check_mixed_bus_http.sh \
  --base-url http://127.0.0.1:8080 \
  --expect-providers bread0,ezo0 \
  --min-device-count 2 \
  --capture-dir artifacts/phase7/baseline
```

Expected baseline outcome:

1. Script exits `0`.
2. Both providers are `AVAILABLE`.
3. Device count meets or exceeds minimum.
4. Artifact JSON files are captured.

## 2) Fault-Isolation Scenarios

Run each scenario one at a time while baseline runtime is running.

### Scenario A: One EZO device offline

1. Power down or disconnect one EZO device.
2. Re-run:

```bash
./scripts/phase7/check_mixed_bus_http.sh \
  --base-url http://127.0.0.1:8080 \
  --expect-providers bread0,ezo0 \
  --min-device-count 1 \
  --capture-dir artifacts/phase7/fault-ezo-offline
```

Expected:

1. Runtime remains responsive.
2. Unrelated provider/device paths continue serving.
3. Health/state endpoints reflect degraded quality for affected signals/devices.

### Scenario B: One BREAD device offline/degraded

1. Remove one BREAD device from the bus or force it into degraded behavior.
2. Re-run with capture:

```bash
./scripts/phase7/check_mixed_bus_http.sh \
  --base-url http://127.0.0.1:8080 \
  --expect-providers bread0,ezo0 \
  --min-device-count 1 \
  --capture-dir artifacts/phase7/fault-bread-offline
```

Expected:

1. Runtime and other devices remain operational.
2. Failure stays localized to affected device path(s).

### Scenario C: Provider restart behavior

1. Kill one provider process.
2. Wait for runtime restart policy to rehydrate provider.
3. Re-run the HTTP check with capture.

Expected:

1. Provider transitions through restart lifecycle states.
2. Runtime recovers without full process restart.

## 3) Ownership-Conflict Rejection

Use the conflict runtime config (adds `ezo_shadow` claiming same bus/address as `ezo0`).

From `anolis-provider-ezo` repo root:

```bash
./scripts/phase7/assert_runtime_conflict_rejected.sh \
  --runtime-cmd ../anolis/build/dev-release/anolis-runtime \
  --config ../anolis-provider-ezo/config/phase7/anolis-runtime.mixed-conflict.yaml \
  --log-file artifacts/phase7/conflict/runtime-conflict.log
```

Expected:

1. Runtime exits non-zero.
2. Log contains `I2C ownership validation failed`.
3. Log includes duplicate ownership details with provider/device handles.

## Pass Criteria

1. Baseline mixed deployment is stable.
2. Fault scenarios demonstrate isolation (no cascade failure).
3. Conflict scenario is rejected deterministically at startup.
4. Evidence artifacts are complete and archived.

## Notes

1. This phase is hardware-in-the-loop by design; do not gate CI on these checks.
2. Keep config overrides local if your binary paths differ from examples.
