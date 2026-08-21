# Changelog

All notable changes to `anolis-provider-ezo` are documented in this file.

## [Unreleased]

## [0.3.5] - 2026-08-20

### Added

- **`hardware.sample_interval_ms`** (#114, default **2500**): how often this
  provider expects its samples to be refreshed. It has no sampling thread of its
  own — it samples when read — so this is the operator's statement of the
  consumer's poll interval, and should mirror the runtime's
  `polling.interval_ms`. Signal freshness bounds and `poll_hint_hz` derive from
  it. Optional, and the default matches `bioreactor-v1`, whose four runtime
  profiles all poll at 2500 ms — so the reference machine needs no change.

  It is not a no-op everywhere, though: a config polling *faster* than 2500 ms
  gets a bound sized for the default rather than its real cadence, which is loose
  rather than wrong (staleness is reported late, never early). The
  `mixed-bus-dev` Windows mock profiles poll at 500 ms and should declare
  `sample_interval_ms: 500` to get a tight bound.

### Fixed

- **Healthy probes reported STALE for most of every poll cycle** (#114).
  `stale_after_ms` was derived from `query_delay_us` — the delay between an EZO
  command write and its reply read, i.e. one transaction's latency — as though
  it were a sampling cadence. With the shipped `query_delay_us: 300000` that
  declared samples stale after 900 ms while the runtime refreshed them every
  2500 ms, so each sample was stale for ~1.6 s of every 2.5 s cycle,
  deterministically and forever. Measured on hardware: 886 `OK`<->`STALE`
  transitions across two probes in 8m38s, with `io_failed = 0`,
  `sample_failure_count = 0` and every read succeeding. Bread devices on the
  same serialized bus flapped zero times in the same window.

  The provider has no sampling thread — it samples on demand, when read — so it
  cannot observe its own refresh rate. Freshness now derives from a new
  `hardware.sample_interval_ms` (default **2500**, matching the shipped
  runtime's `polling.interval_ms`), which is the operator's statement of that
  cadence; `query_delay_us` keeps its real meaning and still bounds the I2C
  transaction and the cache-reuse window.

  Because the provider cannot verify the declared cadence, a mismatch is no
  longer silent: when the gap between two successful samples exceeds the derived
  bound, the provider warns **once per device** naming both numbers and the
  field to raise.

- **`poll_hint_hz` advertised an I2C latency as a poll rate** (#114, and the
  provider half of anolishq/anolis#269). It came from the same conflated
  constant, so the provider asked for 1000/300 = 3.33 Hz while actually being
  read at 0.4 Hz. It is now derived from `sample_interval_ms`.

### Changed

- The freshness derivation had been **duplicated** in `core/runtime_state.cpp`
  (which declares `SignalSpec.stale_after_ms` to the runtime) and
  `core/ezo_provider_runtime.cpp` (which makes this provider's own STALE
  verdict). Both copies are collapsed into one exported
  `runtime::stale_after_ms`, so there is a single definition of the rule. A test
  asserts the advertised and enforced bounds agree — it guards against the
  duplication being reintroduced, and passes against the old code too.

- The cadence-mismatch warning measures the gap on a **monotonic** clock and
  only between two *consecutive successful* reads. A Pi has no RTC, so NTP steps
  the wall clock forward shortly after boot; and the failure path deliberately
  leaves the sample stamps alone, so a gap spanning a run of failed reads would
  otherwise blame the cadence for a transport fault. Either would have latched
  the once-per-device warning on a false positive and permanently suppressed the
  real one.

- The provider warns at startup if `query_delay_us` implies a cache-reuse window
  that is not shorter than `sample_interval_ms`. A poll landing inside that
  window is served from cache rather than a fresh read, halving the effective
  rate — the same failure as #114 reached from the other side.

## [0.3.4] - 2026-08-01

### Added

- `--config-schema` (#110, executable profile v1 §2): prints the provider's
  config JSON Schema in the versioned envelope, emitted from the SDK v0.2.0
  declare-once toolkit. The SAME declaration now drives `--check-config`
  validation (all errors reported at once, with dotted paths) and typed value
  extraction — the advertised schema and the enforced validation cannot drift.
  Titles/defaults/placeholders carry the form-authoring knowledge the
  workbench previously hardcoded (anolis-workbench#270).

### Changed

- Config validation is schema-honest and STRICTER in corners the old
  hand-written parser let through (all shipped configs unaffected): quoted
  numerics (`timeout_ms: "300"`) and stoi trailing junk (`5abc`) are type
  errors; plain non-string scalars against string fields (`bus_path: 123`,
  `label: 5`, `id: 0x5`) are type errors; quoted decimal addresses (`"97"`)
  are rejected (strict `0xNN` string form); duplicated map keys are rejected
  outright; integer fields cap at int32 max explicitly. One loosening: plain
  octal addresses (`0o141`) now parse to their correct value instead of being
  misread as base-10 and rejected.

- Per-device STALE/FAULT health state (#87): `device_health` now derives a live
  device's runtime state — `STATE_FAULT` when the latest read failed,
  `STATE_STALE` when the cached sample is older than its freshness window, `OK`
  when fresh, `UNREACHABLE` when active but not yet sampled — via the SDK v0.1.5
  `DeviceHealthExtra.state` override, restoring the pre-migration nuance.
- Provider-level aggregate health metrics (#88 part 1): `provider_health` emits
  `configured/active/excluded_devices`, `call_success/failure_total`,
  `excluded_reason.<id>`, `bus_path`, and the `i2c_*` counters, and reports
  `DEGRADED` when the I2C executor is stopped, via the SDK v0.1.5
  `ProviderHealthExtra` hook.

### Changed

- Pin the provider SDK to v0.1.5 (from v0.1.4) for the health hooks above.

## [0.3.3] - 2026-07-22

### Changed

- Mock mode now runs through the bus (the follow-up noted in 0.3.2). A new
  `EzoCannedBus` answers the real EZO wire protocol below the transport
  (`?I,<code>,mock-1.0` identity, `?O,<tokens>` output config, a value CSV whose
  width matches the enabled outputs, control-command ACKs), so `mock://` now
  exercises the real EZO C-driver command + parse path instead of synthesizing
  samples above the bus. The above-bus mock synthesis (`fill_mock_identity` /
  `build_mock_sample` and the control-call short-circuit) is removed — mock and
  hardware now run the identical provider path, differing only in which
  `I2cBus` is built. Device settle timing is routed through `bus.delay_us`, so
  the canned bus is instant while real hardware still sleeps (behaviour
  unchanged). Mock read values are unchanged. (anolis-provider-sdk#19, #106)

### Added

- Always-on fault injection in mock: a fault spec on the `mock://` query
  (`mock://bus?read_fail_every=3&corrupt_every=5&…`) wraps the canned bus in the
  SDK `FaultInjectingI2cBus`, so tests drive real wire faults (NAK, timeout,
  short/corrupt reads, dropout, latency) over the real decode path. (#106)

## [0.3.2] - 2026-07-22

### Changed

- Migrated the real I2C transport onto the shared bus seam in
  `anolis-provider-sdk` 0.1.4 (`anolis::provider_sdk_i2c`), replacing the
  provider-local `ISession`/`LinuxSession` with the SDK's `I2cBus`/
  `LinuxI2cBus`. The duplicated `I2C_RDWR`/timeout/retry/errno mechanics and
  the per-address `IoStats` now live once in the SDK. This is a behaviour-
  preserving swap of the hardware read path — the `I2C_RDWR` construction,
  retry budget (`hardware.retry_count`), `EINTR`/`EAGAIN` handling,
  `I2C_TIMEOUT`/`I2C_RETRIES=0` open sequence, and io-counter accounting are
  unchanged — so the `io_ok`/`io_failed`/`io_retried_attempts` health keys
  behave exactly as in 0.3.1. Mock mode is unchanged: identity and samples are
  still synthesized above the bus (a `NoopI2cBus` stands in for the removed
  `NoopSession`). The through-bus, fault-injectable mock is a follow-up.
  (anolishq/anolis-provider-sdk#19)

## [0.3.1] - 2026-07-19

### Added

- Per-device transport io counters in `device_health()` (#100): `io_ok`,
  `io_failed`, `io_retried_attempts`, accumulated per address inside
  `LinuxSession` where the attempts happen. These measure **bus-level**
  health only — a transaction that completes electrically but carries a
  garbage or not-ready EZO payload counts as ok here and fails in the
  protocol-level `sample_*`/`call_*` counters. `io_retried_attempts` counts
  every attempt beyond an operation's first, so retry-masked bus trouble
  stays visible. Counters tally raw I2C transactions (one sample is several);
  the shared contract with other providers is the key vocabulary and the
  masked-retry property, not magnitudes. Mock builds report the keys at zero.

### Changed

- Kernel `I2C_RETRIES` is now set to 0 at session open: every attempt is
  performed and counted by the session's own retry loop, with
  `hardware.retry_count` as the single retry budget. The setting is
  adapter-global and persistent (i2c-dev writes `adapter->retries`), so
  writing 0 also clears any stale value a previous run left behind. On the
  Raspberry Pi target this is a structural no-op (i2c-core retries only on
  `-EAGAIN` arbitration loss, which the bcm2835 adapter never returns; its
  default is already 0). On adapters that do return `-EAGAIN`, total
  resilience is deliberately lower than the previous multiplicative
  kernel-times-userspace double retry — up to `(retry_count+1)^2` attempts
  before, exactly `retry_count+1` counted attempts now.

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

[Unreleased]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.3.3...HEAD
[0.3.3]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.3.2...v0.3.3
[0.3.2]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.3.1...v0.3.2
[0.3.1]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.3.0...v0.3.1
[0.3.0]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.2.7...v0.3.0
[0.2.7]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.2.6...v0.2.7
[0.2.6]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.2.4...v0.2.6
[0.2.4]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.2.3...v0.2.4
[0.2.3]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.2.2...v0.2.3
[0.2.2]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/anolishq/anolis-provider-ezo/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/anolishq/anolis-provider-ezo/releases/tag/v0.1.0
