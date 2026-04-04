# anolis-provider-ezo

EZO sensor hardware provider for the Anolis runtime.

## Current status

1. Runnable ADPP provider with config parsing, framed stdio transport, and unit tests.
2. Serialized I2C execution model with Linux and mock session support.
3. Full EZO family coverage (`ph`, `orp`, `ec`, `do`, `rtd`, `hum`) with fixed signal surfaces.
4. Safe control functions across families (`find`, `set_led`, `sleep`).
5. Startup identity verification with active/excluded inventory diagnostics.
6. Mixed-bus validation assets for Windows mock and Linux real hardware.
7. Runtime duplicate ownership validation integration landed in `anolis`.

## Build

Requires local access to:

1. `anolis-protocol` (default: `../anolis-protocol` or `external/anolis-protocol`)
2. `ezo-driver` (default: `../ezo-driver` or `external/ezo-driver`)

### Install Build Dependencies

Linux (Debian/Ubuntu):

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build git curl zip unzip tar pkg-config python3 python3-pip
```

Windows (PowerShell):

```powershell
winget install Kitware.CMake
winget install Ninja-build.Ninja
winget install Git.Git
winget install Python.Python.3.12
```

Install Visual Studio 2022 (or Build Tools) with the `Desktop development with C++` workload.

### Install vcpkg

Linux/macOS:

```bash
git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh"
echo 'export VCPKG_ROOT="$HOME/vcpkg"' >> ~/.bashrc
export VCPKG_ROOT="$HOME/vcpkg"
test -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

Windows (PowerShell):

```powershell
git clone https://github.com/microsoft/vcpkg.git $env:USERPROFILE\vcpkg
& "$env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat"
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "$env:USERPROFILE\\vcpkg", "User")
$env:VCPKG_ROOT = [Environment]::GetEnvironmentVariable("VCPKG_ROOT", "User")
Test-Path "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
```

Linux/macOS (Release):

```bash
cmake --preset dev-release
cmake --build --preset dev-release
```

Linux mixed-bus hardware validation (canonical cross-provider preset naming):

```bash
cmake --preset dev-linux-hardware-release
cmake --build --preset dev-linux-hardware-release
```

`dev-linux-hardware-*` presets in this repo are naming-alias presets for cross-provider consistency with `anolis-provider-bread`.

Windows (MSVC Release):

```powershell
cmake --preset dev-windows-release
cmake --build --preset dev-windows-release
```

## Test

Linux/macOS:

```bash
ctest --preset dev-release
```

Linux mixed-bus hardware validation path:

```bash
ctest --preset dev-linux-hardware-release
```

Windows:

```powershell
ctest --preset dev-windows-release
```

## Run

Linux/macOS:

```bash
./build/dev-release/anolis-provider-ezo --check-config config/example.local.yaml
./build/dev-release/anolis-provider-ezo --config config/example.local.yaml
```

Linux mixed-bus hardware validation build path:

```bash
./build/dev-linux-hardware-release/anolis-provider-ezo --check-config config/example.local.yaml
./build/dev-linux-hardware-release/anolis-provider-ezo --config config/example.local.yaml
```

Windows:

```powershell
.\build\dev-windows-release\Release\anolis-provider-ezo.exe --check-config config\example.local.yaml
.\build\dev-windows-release\Release\anolis-provider-ezo.exe --config config\example.local.yaml
```

## Validation Assets

1. Canonical config pack and runbook: `../anolis/config/mixed-bus-providers/`
2. Canonical HTTP validation script: `../anolis/config/mixed-bus-providers/check_mixed_bus_http.sh`
3. Validation summary: `docs/mixed-bus-validation.md`

For Linux mixed-bus hardware runs:
1. Build `anolis-provider-bread` with `dev-linux-hardware-release`.
2. Build `anolis-provider-ezo` with `dev-linux-hardware-release`.
3. `../anolis/config/mixed-bus-providers/provider-bread.yaml` sets `hardware.require_live_session: true` so startup fails fast if bread is built without hardware support.

## Docs

1. [docs/README.md](docs/README.md)
2. Planning notes: `working/` (gitignored)

Local API docs: run `doxygen docs/Doxyfile` from the repo root.
Generated output goes to `build/docs/doxygen/html/` and remains untracked.
