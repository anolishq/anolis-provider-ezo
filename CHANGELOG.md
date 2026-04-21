# Changelog

All notable changes to `anolis-provider-ezo` are documented in this file.

## [Unreleased]

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
