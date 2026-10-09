# Runtime Ownership Validation Inputs (Cross-Repo)

This document defines the data a provider publishes so the runtime can reject
two providers driving the same device on a shared bus.

## Problem

When multiple providers share one Linux I2C bus, duplicate ownership of the same address must be rejected at startup.

## The claim tag

Every hardware device a provider exposes carries an `anolis.claim` descriptor
tag: the resources it needs exclusively, as space-separated keys. For an I2C
device that is one key, built with the provider SDK's `i2c::claim_key`:

```text
anolis.claim: i2c:/dev/i2c-1:0x61
```

The key is the configured bus path as given, then a lowercase, two-digit hex
address. Providers must use the SDK helper rather than format it themselves:
the runtime compares keys as exact strings, so two providers spelling the same
address differently would not collide.

The `hw.bus_path` / `hw.i2c_address` tags and their `bus_path` / `i2c_address`
aliases are no longer published (anolishq/anolis#318).

## Runtime validator behavior

1. Collect every `anolis.claim` key across all providers' devices.
2. If any key has more than one owner, startup fails, and so does a provider
   restart that would introduce one. The error names the key and each owning
   provider and device.
3. The runtime parses nothing: it knows no transport, only that keys must be
   unique.

## Scope

1. `anolis-provider-ezo` and `anolis-provider-bread` publish claims.
2. Validation lives in the `anolis` runtime.
