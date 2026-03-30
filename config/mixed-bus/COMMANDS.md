# Mixed-Bus Validation Commands

Use these exact commands.

## 0) Build Prerequisites (Preset-Based)

Build all three repos before validation so executable paths in the runtime configs exist.

### Linux/macOS

```bash
cd /path/to/anolis
cmake --preset dev-release
cmake --build --preset dev-release

cd /path/to/anolis-provider-bread
cmake --preset dev-release
cmake --build --preset dev-release

cd /path/to/anolis-provider-ezo
cmake --preset dev-release
cmake --build --preset dev-release
```

### Windows (PowerShell)

```powershell
Set-Location D:\repos_feast\anolis
cmake --preset dev-windows-release
cmake --build --preset dev-windows-release

Set-Location D:\repos_feast\anolis-provider-bread
cmake --preset dev-windows-release
cmake --build --preset dev-windows-release

Set-Location D:\repos_feast\anolis-provider-ezo
cmake --preset dev-windows-release
cmake --build --preset dev-windows-release
```

## A) Windows Mock Validation (PowerShell)

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

1. On Windows mock path, `bread0` may log `no hardware session`; that is expected.

## B) Linux Baseline Hardware Validation

### 1) Start runtime (Terminal A)

```bash
cd /path/to/anolis
./build/dev-release/core/anolis-runtime --config ../anolis-provider-ezo/config/mixed-bus/anolis-runtime.mixed-baseline.yaml
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

## C) Linux Lab Hardware Validation (RLHT 0x0A, DCMT 0x14/0x15, pH 0x63, DO 0x61)

### 1) Start runtime (Terminal A)

```bash
cd /path/to/anolis
./build/dev-release/core/anolis-runtime --config ../anolis-provider-ezo/config/mixed-bus/anolis-runtime.mixed-lab.yaml
```

### 2) Validate endpoints (Terminal B)

```bash
cd /path/to/anolis-provider-ezo
./scripts/mixed-bus/check_mixed_bus_http.sh \
  --base-url http://127.0.0.1:8080 \
  --expect-providers bread0,ezo0 \
  --min-device-count 5 \
  --capture-dir artifacts/mixed-bus-validation/lab
```

Expected:

1. Runtime and both providers are `AVAILABLE`.
2. Inventory includes 5 devices total (`rlht0`, `dcmt0`, `dcmt1`, `ph0`, `do0`).
3. Script exits `0` and writes artifacts.
