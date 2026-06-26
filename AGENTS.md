# AGENTS.md — anolis-provider-ezo

> Per-repo conventions for coding agents. The canonical cross-repo rules
> (Conventional Commits, minimal-first/YAGNI, no secrets, run checks before
> claiming success) live in the user's **global** `AGENTS.md` and are not
> repeated here. This file records only what is specific to this repo.

## Build / test

- Use the `justfile`: `just setup` (configure), `just check` (fmt-check +
  clang-tidy), `just test` (build + ctest). Primary preset is
  **`ci-linux-release`**.
- The required CI status check is the **`ok`** job; never merge red.

## Tooling

- clang-format / clang-tidy are pinned to **18.1.8** via the shared
  `setup-clang-tools` action — do NOT use pip/apt/pre-commit/container
  versions. Run `just fmt` (clang-format -i) before **every** commit.
- vcpkg comes from the shared `setup-vcpkg` action. Shared `.github` actions are
  SHA-pinned with a `# <tag>` comment for Renovate — keep it when bumping.

## Repo-specific gotchas

- **Mock vs hardware is selected by the `mock://` bus path in config, NOT a
  build flag** — e.g. `config/conformance.yaml` uses `bus_path: mock://...`.
  There is one binary; the bus path decides.
- `devices::adapter_for(EzoDeviceType)` is the single device-type dispatch
  point: a **compile-time-exhaustive `switch`**. Adding a device type without a
  case fails the build (warnings-as-errors is ON in the CI preset).
- `tests/.clang-tidy` deliberately relaxes test-only check noise (performance-*,
  narrowing, optional-access idioms) — keep test-specific suppressions there.
- Conformance runs via `--provider-profile config/conformance.toml` (the
  provider-owned manifest where waivers + conformance level live). Note:
  `conformance.toml` = profile/waivers, `conformance.yaml` = runtime config.
- Targets the Atlas Scientific **EZO** I2C sensor family.
