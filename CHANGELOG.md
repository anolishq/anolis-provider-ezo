# Changelog

All notable changes to `anolis-provider-ezo` are documented in this file.

## [Unreleased]

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
