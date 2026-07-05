# Changelog

All notable changes to `anolis-provider-ezo` are documented in this file.

## [Unreleased]

## [0.3.0] - 2026-07-04

### Added

- **Migrated onto `anolis-provider-sdk`.** The provider now builds on the shared
  SDK's `ProviderRuntime` + run-loop and device-model framework rather than a
  private copy, pinned to SDK v0.1.2. (#85, #86, #89)
- **Per-device health metrics + `last_seen`** — health enrichment surfaced per
  device (SDK#9). (#86)
- **Restored ezo `WaitReady` diagnostics** (init/readiness reporting; Part 2 of
  #88). (#91)
- Curated default signal set. (#56)

### Changed

- Device-model refactors toward the SDK: `EzoDeviceType` moved into the devices
  layer; read/call delegated to wrapped-result execution functions; single-stage
  error mapping on the call path; signal-quality rule extracted into
  `devices::quality_from`; C++20 `std::format` adopted for diagnostics.

### Fixed

- `min_timestamp` is treated as a non-fatal freshness hint rather than a hard
  error (ezo#70 Stage 4). (#76)
- Capabilities: `signal_ids` are snake_case and per-type `function_ids` are
  numbered from 1. (#61)

### CI

- Native arm64 unit-test lane; clang-tidy diff gate promoted to blocking;
  pinned clang-format / clang-tools; routine dependency maintenance.

## [0.2.7] - 2026-06-22

### Added

- **ADPP conformance level 2.** Declare `conformance_level = 2` in
  `config/conformance.toml`. The provider satisfies the L2 clauses of the ADPP
  semantics: a non-Hello request received before a successful Hello is rejected
  with `CODE_FAILED_PRECONDITION` (§3.2), and `function_id` is preferred over
  `function_name` when both are supplied in a `Call` (§6.2).

### CI

- Add the ADPP `provider.conformance` lane: run the pinned
  `anolis-adpp-conformance` harness against the built binary using the
  provider-owned `config/conformance.toml` manifest.
- Add a ThreadSanitizer lane and the shared Valgrind leak-check hardening
  workflow.
- Add a keyless dependency/CVE scan (`cve-bin-tool`) lane.

### Changed

- Routine dependency maintenance: refresh pinned GitHub Actions to the current
  org-tracked revisions.

## [0.2.6] - 2026-06-16

### Changed

- Bump the vcpkg baseline to the vcpkg `2026.06.01` release: protobuf
  `5.29.5` → `6.33.4`, with abseil and the rest of the C++ dependency set
  refreshed. No source changes required.
- Centralize the vcpkg pin: the shared `setup-vcpkg` action now derives the
  vcpkg commit from `vcpkg-configuration.json`, so the per-workflow
  `VCPKG_COMMIT` env was removed.

### CI

- Migrate Windows build to Visual Studio 2026. The hosted `windows-2025` /
  `windows-latest` runner image moved from VS 2022 to VS 2026; update the
  `base-windows-msvc` preset generator `Visual Studio 17 2022` →
  `Visual Studio 18 2026` and move the Windows CI lane from `windows-2022` to
  `windows-2025`. The plain `x64-windows` triplet inherits the image's default
  toolset (`v145`), so no triplet/toolset changes are required.
- Add CI OK aggregator gate: removed `paths-ignore`, added `dorny/paths-filter`
  to detect code-vs-docs changes, gated all jobs behind the filter, and added a
  final `ok` job as the sole required status check for `main` branch protection.

## [0.2.4] - 2026-04-24

### Changed

- Updated `anolis-protocol` dependency from v1.1.4 to v1.2.0. The new release
  adds `optional` presence to `ArgSpec` bounds fields. No source changes
  required in this provider.

## [0.2.3] - 2026-04-23

### CI

- Fixed binary portability: added custom `triplets/x64-linux-static.cmake` vcpkg triplet
  (`VCPKG_LIBRARY_LINKAGE=static`, `VCPKG_CRT_LINKAGE=dynamic`, `VCPKG_CMAKE_SYSTEM_NAME=Linux`)
  and applied it to the `ci-linux-release` configure preset via `VCPKG_OVERLAY_TRIPLETS`.
  All vcpkg dependencies (protobuf, yaml-cpp, gtest) are now statically linked into the
  released binary. glibc remains dynamic. The tarball contains a single self-contained executable.
  ezo-driver (`ezo_core`) was already explicitly static and is unaffected by the triplet change.

## [0.2.2] - 2026-04-23

### Changed

- Validation Assets paths updated to use `anolis-projects` layout.
- Bump `anolis-protocol` FetchContent pin from `v1.1.3` to `v1.1.4`.

### CI

- Version-sync check wired: `version-locations.txt` added tracking `CMakeLists.txt`
  and `vcpkg.json`; CI calls reusable `version-sync` workflow from `anolishq/.github`.
- `vcpkg.json` version aligned to `0.2.0` (was stale at `0.1.0`).
- `.anpkg` added to `.gitignore`.

### Docs

- Build setup and Validation Asset references updated.

## [0.2.1] - 2026-04-21

### CI

- Add `ci-linux-release` CMake preset with `EZO_DRIVER_DIR`; release workflow
  updated to use it.

> **Note:** the `v0.2.1` tag was applied to a CI-only commit; version strings in
> source remained at `0.2.0`. This entry is recorded for completeness.

## [0.2.0] - 2026-04-21

### Changed

- Switch `anolis-protocol` dependency from git submodule to FetchContent, pinned at `v1.0.0` then bumped to `v1.1.3`.
- Cut `ezo-driver` dependency to `find_package`; located via `EZO_DRIVER_DIR` CMake variable — removes the submodule requirement.
- Remove stale `ANOLIS_PROTOCOL_DIR` variable from `CMakePresets.json`.

### CI

- Pin org reusable workflow refs from `@main` to `@v1`.
- Add metrics collection to release workflow; `metrics.json` uploaded as release asset on each `v*` tag.

## [0.1.0] - 2026-04-20

First tagged release. The EZO provider was developed in full before tagging; this
entry summarizes the meaningful work that landed prior to `v0.1.0`.

Historical note: this changelog was written retrospectively from git history at the
time of the first tagged release. Earlier development was tracked in commit messages
only.

### Added

- Full ADPP v1 device provider implementation over gRPC: `Handshake`, `Health`,
  `ListDevices`, `DescribeDevice`, `ReadDevice`, `CallDevice`, `StreamTelemetry`.
- I2C executor/session core bridging `ezo-driver` into the ADPP surface: runtime
  wiring, health metrics, and unit tests.
- EZO pH vertical slice: startup identity checks, active/excluded inventory,
  cached sampling, and ADPP `ListDevices`/`DescribeDevice`/`ReadDevice` handlers.
- Full EZO family coverage (DO, ORP, EC, RTD, …): generalized sampling/read paths
  with per-sensor multi-output signal surfaces.
- Safe EZO call dispatch (`find`, `set_led`, `sleep`) with strict validation,
  executor dispatch, and call-aware health telemetry.
- Mixed-bus (CRUMBS + EZO) validation configs, runbooks, and evidence templates for
  combined bread+ezo hardware sessions.
- `dev-linux-hardware-*` preset aliases aligning Linux hardware preset naming with
  `anolis-provider-bread`.
- Cross-platform CI: Linux build/test lane and Windows build lane via shared org
  workflows.
- Release workflow: on `v*` tag, builds `dev-release` preset, packages binary +
  source tarball + `manifest.json` + `SHA256SUMS`.

### Changed

- Mixed-bus validation assets simplified to two canonical baseline flows (Windows
  mock, Linux real-hardware); legacy conflict-focused assets removed.
- Operator commands consolidated into `config/mixed-bus/COMMANDS.md`.
- Linux mixed-bus baseline provider configs aligned to active CRUMBS lab hardware
  map (`0x0A`, `0x14`, `0x15`, `0x63`, `0x61`).
- `docs/` flattened into canonical top-level documents; nested directory structure
  removed.
- Protocol submodule URL migrated from `FEASTorg/anolis` to
  `anolishq/anolis-protocol` after protocol extraction.
- EZO timeout increased to match slow hardware handshake.
- Default polling interval updated based on hardware feedback.
- CI dependency checkout pinned to immutable release tag: `ezo-driver` `v0.5.1`.
- Org renamed from `FEASTorg` to `anolishq` throughout.
- License: AGPL-3.0.

### Removed

- Hardware configs moved to `anolis` main repo; no longer tracked here.

[Unreleased]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.2.7...v0.3.0
[0.2.7]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.2.6...v0.2.7
[0.2.6]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.2.4...v0.2.6
[0.2.4]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.2.3...v0.2.4
[0.2.3]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.2.2...v0.2.3
[0.2.2]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/anolishq/anolis-provider-ezo/releases/tag/v0.1.0
