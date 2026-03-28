# ADR-0001: Provider Scope and Shared-Bus Safety Model

- Status: Accepted
- Date: 2026-03-27

## Context
`anolis-provider-ezo` is being introduced alongside `anolis-provider-bread`, with both potentially sharing one Linux I2C adapter (`/dev/i2c-X`).

The design needs to stay simple, avoid hidden coupling between providers, and remain safe under concurrent runtime behavior.

## Decision
1. Provider boundaries are strict:
   - `anolis-provider-bread` remains BREAD-over-CRUMBS only.
   - `anolis-provider-ezo` handles EZO devices only.
2. v1 transport scope is I2C only.
3. v1 discovery mode is manual config only.
4. v1 startup behavior is strict identity verification + partial-ready startup:
   - type mismatch or unreachable devices are excluded with diagnostics.
   - healthy devices remain available.
5. v1 function surface is limited to safe controls:
   - `find`
   - `set_led`
   - `sleep`
6. Shared-bus safety is layered:
   - each provider must serialize its own bus operations through a single internal executor.
   - runtime must reject duplicate `(bus_path, i2c_address)` ownership across providers at startup.
7. Cross-process advisory lock is optional hardening and not baseline for v1.

## Consequences
1. Clear ownership and maintainable provider code boundaries.
2. Reduced race risk inside each provider.
3. Deterministic startup failure for ambiguous topology.
4. Lower initial complexity than a global broker model.

## Non-goals for v1
1. UART transport support.
2. Auto discovery.
3. High-risk control/calibration command surface.
4. Runtime-global lock manager or broker process.
