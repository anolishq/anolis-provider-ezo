# Device Health Metrics

Each device's health carries counters at two levels: the I2C transport, and
the EZO protocol on top of it. The transport counters use the key vocabulary
in the provider SDK's `docs/metrics.md`. The protocol counters are ezo's own.

## Transport I/O counters

The SDK's Linux I2C bus counts these per address. One raw I2C transaction
counts once: a write, a read, or a combined write-then-read. Taking one EZO
sample takes several transactions, so these magnitudes are not comparable with
a provider that counts logical operations.

```text
io_ok               — transactions the bus completed, after retries if any
io_failed           — transactions that still failed once the bus's retries ran out
io_retried_attempts — attempts beyond each transaction's first, including those that then succeeded
```

The transport sees only electrical success. A reply that completes on the bus
but is unusable, such as a garbage payload or a not-ready or error response
code, counts in `io_ok`. Such failures show up in the protocol counters below.

## Protocol counters

```text
sample_success_count         — poll cycles that produced a reading for the device
sample_failure_count         — poll cycles that did not, for any reason: the bus, the reply, or the job timing out
call_success_count           — function calls that succeeded
call_failure_count           — function calls that failed
compensation_reapplied_count — see Device Contracts v1; only on devices with read compensation
```

A device whose `sample_failure_count` rises while `io_failed` stays flat is
answering on the bus, but not with a usable reading.
