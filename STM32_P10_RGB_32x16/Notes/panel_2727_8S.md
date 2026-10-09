# P10-2727-8S-32X16-A — notes

Panel currently being ported from the working ESP32 + PxMatrix sketch to STM32 HAL.
This is the panel we are bringing up **first**; 3535-4S notes are kept separate in
`panel_3535_4S.md`.

## Panel identity

- Model: P10-2727-8S-32X16-A
- Resolution: 32 x 16
- Row scan: **1/8** (`HUB75_ROWS = HEIGHT/2 = 8`)
- No known special driver-chip quirks (not FM6126A/ICN2038S-type) — behaves like a
  standard HUB75 shift-register panel, no power-on-configuration (POC) wake-up
  sequence expected.

## HUB75 protocol recap (why the driver is shaped the way it is)

- Pins: R1,G1,B1,R2,G2,B2 (2 rows of color data shifted in parallel), A/B/C (row
  address), CLK (shift clock), LAT/STB (latch), OE (output enable, **active-low**).
- Refresh cycle, repeated per row-pair, timer-driven:
  1. OE = HIGH (blank output while shifting)
  2. For each column: set R1/G1/B1/R2/G2/B2, pulse CLK
  3. Pulse LAT (copies shifted data to the output latches)
  4. Set A/B/C to select the next row-pair
  5. OE = LOW (enable output for this row-pair)
  6. Move to next row-pair on the next timer tick
- Flicker-free display = persistence of vision from repeating this fast enough across
  all 8 row-pairs.

## Row scan / mux / scan pattern (matched against PxMatrix's own docs)

- **Row scan**: 1/8 → only A, B, C needed (2^3 = 8 row-pairs). Row N pairs with row
  N+8 (spacing = ROWS = 8). Confirmed to match PxMatrix's own description of 32x16
  1/8-scan behavior.
- **Mux pattern**: BINARY (default) — A=bit0, B=bit1, C=bit2 of the row-pair index.
  This is what the current driver implements and is the standard for this panel type
  (SHIFTREG_ABC / STRAIGHT are for other chips like RT5957, not expected here).
- **Scan pattern**: assumed **LINE** (simple sequential column order, no
  zigzag/interleave remapping). This is the standard case for indoor P10 2727 panels,
  but is the one thing that can only be confirmed by flashing real hardware — if the
  image comes out diagonally split/interleaved instead of a clean picture, the panel
  actually needs a ZIGZAG-family remap of column bit order.
- **Colors / brightness**: driver currently does **1-bit per channel only** (8 solid
  colors: R,G,B,Y,C,M,W,black), matching the ESP32 demo's `myCOLOR_ARRAY`. No BCM
  (Binary Code Modulation) yet, so no grayscale/brightness control. Adding BCM later
  means running each row-pair through multiple weighted sub-passes (1x/2x/4x OE-on
  time) instead of a single pass — noted as a possible future upgrade, not started.

## Current STM32 implementation

- Board: Nucleo-F401RE, STM32CubeIDE + HAL (no DMA, plain GPIO bit-bang).
- Files: `Core/Src/hub75.c`, `Core/Inc/hub75.h`, `Core/Inc/font5x7.h`, `README.md`
  (all in this project folder).
- Refresh driven by TIM3 update interrupt (~1kHz per row-pair → full 8-row frame every
  ~8ms). `HUB75_RefreshISR()` is called from `HAL_TIM_PeriodElapsedCallback`.
- Wiring uses direct 6-pin parallel color lines (no PxMatrix-style PI→PO
  jumper/SPI-daisy-chain pin-saving trick) — valid and simpler since STM32 has plenty
  of free GPIOs; do **not** add those jumper wires on the panel for this setup.
- Pin map (edit the `#define ..._PORT/..._PIN` block at the top of `hub75.c` to
  rewire):

  | Signal | Nucleo pin | MCU pin |
  |---|---|---|
  | R1 | D2  | PA10 |
  | G1 | D3  | PB3  |
  | B1 | D4  | PB5  |
  | R2 | D5  | PB4  |
  | G2 | D6  | PB10 |
  | B2 | D7  | PA8  |
  | A  | D8  | PA9  |
  | B  | D9  | PC7  |
  | C  | D10 | PB6  |
  | CLK| D11 | PA7  |
  | LAT| D12 | PA6  |
  | OE | D13 | PA5  |

  Note: OE on PA5 shares the pin with the Nucleo-F401RE's onboard LD2 LED — cosmetic
  flicker side-effect only (LED will blink with OE toggling), not a functional
  conflict, unless the LED is also driven elsewhere in user code.

## Verification done so far (no real hardware test yet)

- Protocol sequence cross-checked against three real-world STM32 HUB75 reference
  implementations found via research: embedded-dbl.com's TIM+DMA driver, the
  board707/DMD_STM32 library, and kostaman/HUB75 — all use the same
  blank→shift→latch→row-address→enable sequence this driver implements (those use
  DMA for the shifting step, this driver bit-bangs it in the ISR instead — valid for
  a small 32-wide panel, just more CPU-bound).
- As of 2026-09-11: not yet flashed on real hardware. User (kankshi@houstonsystem.com)
  is going to test on STM32 and report back what they see (blank / right colors /
  garbled / mirrored / flickering / wrong rows) so any bugs can be fixed against real
  symptoms instead of guessing.

## Status

Get this panel fully working first — it's the reference/no-special-quirks case.
3535-4S work is intentionally deferred until this one is confirmed on hardware.
