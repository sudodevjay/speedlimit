# SMD 3535-4S panel — notes

Second panel, **deferred** until the 2727-8S panel (see `panel_2727_8S.md`) is
confirmed working on STM32 hardware. Nothing has been implemented for this panel yet
— this file is research/planning notes only, gathered 2026-09-11.

## Panel identity

- Driver IC: **ICN2038S** (confirmed by user, 2026-09-11) — not FM6126A/FM6126B as
  first guessed, but same family of problem: a "smart" row/constant-current driver
  chip, not a plain shift-register + latch chip like the 2727-8S uses.
- Row scan: **1/4** (implied by "4S" naming) — different from the 2727-8S's 1/8 scan.
  A 1/4-scan panel typically only needs 2 address lines (A,B) if BINARY mux is used,
  but see caveats below — this panel may not use BINARY at all.

## Why this panel is problematic (known from research + user report)

User reports this panel **also fails to display data correctly under PxMatrix on
ESP32** — i.e. the problem is panel/driver-IC-specific, not an STM32 porting issue.
Two independent, compounding causes are expected (from PxMatrix/SmartMatrix/
ESP32-HUB75-MatrixPanel-DMA issue trackers — see conversation 2026-09-11 for sources):

1. **Driver-chip wake-up / POC (power-on-configuration) sequence.**
   Chips like ICN2038S/FM6126A/FM6126B commonly boot in a state where they will not
   pass through pixel data at all until a special one-time init sequence is shifted
   in during startup (a specific non-pixel data pattern + extra clock pulses,
   distinct from normal per-frame refresh). Without it, symptoms are exactly what's
   being seen: blank panel, random/scrambled pixels, or only some pixels lit — even
   though the HUB75 timing (shift/latch/OE/row-address) is otherwise correct. This is
   *not* a wiring or protocol-sequence bug — it's a missing extra init step.
   - PxMatrix has some FM6126A support (`setDriverChip(FM6126A)`), but that's
     ESP8266-side and reported unreliable even there — so PxMatrix's existing
     handling can't just be assumed to work here either.
   - The exact POC sequence for **ICN2038S specifically** has not been looked up yet
     — this is the next research step before writing any 3535-4S code.

2. **Mux pattern / scan pattern likely differ from the 2727-8S.**
   Per PxMatrix's own docs (quoted in conversation 2026-09-11): most panels use
   BINARY mux, but some 1/4-scan panels use **STRAIGHT** (A,B,C,D map directly/
   sequentially to rows instead of binary-decoded) or **SHIFTREG_ABC** (row selection
   via an extra shift register on the A/B/C lines, seen on RT5957-based panels — B
   held LOW). Row spacing for 1/4 scan is also different (spacing of 4, not 8) and
   scan pattern (column bit ordering) may not be simple LINE either. Reusing the
   2727-8S driver's assumptions (BINARY mux, ROWS=8, LINE order) unmodified on this
   panel will not work regardless of the POC issue above.

## Plan

1. Get the 2727-8S panel fully validated on STM32 hardware first.
2. Find the ICN2038S datasheet / a known-working init sequence for it (look at how
   existing libraries — PxMatrix FM6126A path, SmartMatrix's
   `SM_HUB75_OPTIONS_FM6126A_RESET_AT_START`, ESP32-HUB75-MatrixPanel-DMA issues —
   handle ICN2038S/FM6126A-family chips specifically, since ICN2038S needs its own
   confirmed sequence, not just copying FM6126A's).
3. Confirm this panel's actual mux pattern and scan pattern (STRAIGHT vs BINARY vs
   SHIFTREG_ABC; LINE vs zigzag) — likely needs physical inspection of the panel's
   labeled connector pins and/or datasheet, not just assumption.
4. Only then write/adapt the STM32 driver for this panel — it will need its own
   pin/address-line handling, separate from (or as a configurable variant of)
   `hub75.c`.

## Status

Not started. Blocked on step 1 (2727-8S hardware validation) by design — see
`panel_2727_8S.md`.
