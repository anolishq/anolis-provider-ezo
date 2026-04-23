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

## Quick Start

### Download and run (recommended)

Download the latest release binary (Linux x86_64):

```bash
VERSION=$(curl -fsSL https://api.github.com/repos/anolishq/anolis-provider-ezo/releases/latest | grep '"tag_name"' | sed 's/.*"v\([^"]*\)".*/\1/')
curl -fsSL "https://github.com/anolishq/anolis-provider-ezo/releases/download/v${VERSION}/anolis-provider-ezo-${VERSION}-linux-x86_64.tar.gz" \
  | tar -xz
# binary is at ./bin/anolis-provider-ezo
```

Validate a config file without connecting hardware:

```bash
./bin/anolis-provider-ezo --check-config config/example.local.yaml
```

Provider-ezo is started by the Anolis runtime as a subprocess — it is not run directly.
Point your [`anolis`](https://github.com/anolishq/anolis/releases/latest) runtime config at it:

```yaml
# in your anolis-runtime.yaml providers section:
providers:
  - id: ezo0
    command: ./bin/anolis-provider-ezo
    args: ["--config", "./providers/ezo0.yaml"]
    timeout_ms: 5000
```

See `config/example.local.yaml` in the source for a full annotated config. Validation assets
and mixed-bus runbooks are in `anolishq/anolis-projects`.

### Build from source (contributors / hardware builds)

No sibling checkouts required — `anolis-protocol` and `ezo-driver` resolve automatically at configure time.

For active development of `ezo-driver` alongside this repo, clone it as a sibling and pass
`-DEZO_DRIVER_DIR=../ezo-driver` to override the release artifact lookup.

#### Install build dependencies

Linux (Debian/Ubuntu):

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build git curl zip unzip tar pkg-config
```

Windows (PowerShell):

```powershell
winget install Kitware.CMake; winget install Ninja-build.Ninja; winget install Git.Git
# Install Visual Studio 2022 with "Desktop development with C++" workload
```

Install vcpkg and set `VCPKG_ROOT`:

```bash
git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh"
export VCPKG_ROOT="$HOME/vcpkg"
```

#### Configure, build, test

Linux/macOS:

```bash
git clone https://github.com/anolishq/anolis-provider-ezo.git
cd anolis-provider-ezo
cmake --preset dev-release
cmake --build --preset dev-release --parallel
ctest --preset dev-release
```

Linux hardware build:

```bash
cmake --preset dev-linux-hardware-release
cmake --build --preset dev-linux-hardware-release --parallel
```

Windows (MSVC):

```powershell
cmake --preset dev-windows-release
cmake --build --preset dev-windows-release --parallel
ctest --preset dev-windows-release
```

## Run (from source build)

Linux:

```bash
./build/dev-release/anolis-provider-ezo --check-config config/example.local.yaml
./build/dev-release/anolis-provider-ezo --config config/example.local.yaml
```

Windows:

```powershell
.\build\dev-windows-release\Release\anolis-provider-ezo.exe --check-config config\example.local.yaml
```



## Validation Assets

1. Mixed-bus config pack and runbook: `anolis-projects/projects/mixed-bus-dev/` in the `anolishq/anolis-projects` repo
   (clone separately; these are realization assets, not bundled here)
2. Canonical HTTP validation script: `validation/check_mixed_bus_http.sh` in that same directory
3. Validation summary: `docs/mixed-bus-validation.md`

For Linux mixed-bus hardware runs:

1. Build `anolis-provider-bread` with `dev-linux-hardware-release`.
2. Build `anolis-provider-ezo` with `dev-linux-hardware-release`.
3. `../anolis-projects/projects/mixed-bus-dev/config/provider-bread.yaml` sets
   `hardware.require_live_session: true` so startup fails fast if bread is built
   without hardware support.

## Docs

1. [docs/README.md](docs/README.md)
2. Planning notes: `working/` (gitignored)

Local API docs: run `doxygen docs/Doxyfile` from the repo root.
Generated output goes to `build/docs/doxygen/html/` and remains untracked.
