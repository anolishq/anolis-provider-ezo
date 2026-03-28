# Runtime Ownership Validation Inputs (Cross-Repo)

This document defines the minimum data contract required for runtime duplicate-ownership validation.

## Problem
When multiple providers share one Linux I2C bus, duplicate ownership of the same address must be rejected at startup.

## Required provider-emitted tags
Every discovered/configured hardware device exposed by a provider must include:
1. `hw.bus_path` (example: `/dev/i2c-1`)
2. `hw.i2c_address` (canonical hex string, example: `0x61`)

## Compatibility aliases (transitional)
Providers may also emit legacy aliases `bus_path` and `i2c_address` for downstream compatibility.
Runtime ownership validation must use the canonical `hw.*` tags.

## Canonical formatting rules
1. `hw.bus_path` is the exact configured path string.
2. `hw.i2c_address` must be lowercase hex prefixed with `0x`, zero-padded to two hex digits (`0x08`..`0x77`).

## Runtime validator behavior
1. Build ownership key `(hw.bus_path, hw.i2c_address)` for every discovered device across all providers.
2. If any key has more than one owner, runtime startup fails fast.
3. Runtime error output must include:
   - provider name
   - device id
   - bus path
   - address

## Scope
1. This contract is required for `anolis-provider-ezo` and `anolis-provider-bread`.
2. Validation lives in `anolis` runtime.
