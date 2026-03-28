# Mixed-Bus Validation Commands

Use these exact commands.

## A) Windows Mock Test (PowerShell, no WSL)

### 1) Start runtime (Terminal A)

```powershell
Set-Location D:\repos_feast\anolis
Get-NetTCPConnection -LocalPort 18080 -ErrorAction SilentlyContinue
.\build\dev-windows-release\core\Release\anolis-runtime.exe --config ..\anolis-provider-ezo\config\mixed-bus\anolis-runtime.mixed-baseline.windows.mock.yaml
```

### 2) Validate endpoints (Terminal B)

```powershell
$base = 'http://127.0.0.1:18080'
Invoke-RestMethod "$base/v0/runtime/status" | ConvertTo-Json -Depth 8
Invoke-RestMethod "$base/v0/providers/health" | ConvertTo-Json -Depth 8
Invoke-RestMethod "$base/v0/devices" | ConvertTo-Json -Depth 8
Invoke-RestMethod "$base/v0/state" | ConvertTo-Json -Depth 8
```

Expected:

1. Runtime stays up.
2. `bread0` and `ezo0` are present.
3. `ezo0` exposes `ph0`, `do0`, `ec0`.

Note:

1. On Windows mock path, `bread0` may log `no hardware session`; that is expected for this local mock validation.

## B) Linux Real Hardware Validation

### 1) Start runtime (Terminal A)

```bash
cd /path/to/anolis
./build/dev-release/anolis-runtime --config ../anolis-provider-ezo/config/mixed-bus/anolis-runtime.mixed-baseline.yaml
```

### 2) Validate endpoints (Terminal B)

```bash
cd /path/to/anolis-provider-ezo
./scripts/mixed-bus/check_mixed_bus_http.sh \
  --base-url http://127.0.0.1:8080 \
  --expect-providers bread0,ezo0 \
  --min-device-count 2 \
  --capture-dir artifacts/mixed-bus-validation/baseline
```

Expected:

1. Script exits `0`.
2. Runtime and provider health endpoints return `status.code = OK`.
3. Artifacts are written to `artifacts/mixed-bus-validation/baseline`.
