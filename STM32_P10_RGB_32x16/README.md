# P10 RGB 32x16 on STM32 (Nucleo-F401RE), STM32CubeIDE + HAL

PxMatrix (the library used on the ESP32 side) only supports ESP8266, ESP32 and AVR —
its header has zero STM32 code paths. So this is a small hand-written bare-metal HUB75
driver (`Core/Src/hub75.c`, `Core/Inc/hub75.h`) plus a generated 5x7 font
(`Core/Inc/font5x7.h`), built on STM32 HAL. It drives the panel the same way PxMatrix
does (continuous per-row refresh from a timer interrupt), just without brightness/PWM —
you get the same 8 solid colors your ESP32 demo used (R, G, B, Y, C, M, W, black).

## 1. Wiring (HUB75 IN connector -> Nucleo-F401RE Arduino header)

Same panel connector layout as your `Installation.png` (IN side only — this drives a
single panel). Power the panel from a separate 5V supply, same as the ESP32 setup
(common GND between the 5V supply, the panel, and the Nucleo GND).

| HUB75 pin | Nucleo Arduino pin | MCU pin |
|-----------|--------------------|---------|
| R1        | D2                 | PA10    |
| G1        | D3                 | PB3     |
| B1        | D4                 | PB5     |
| R2        | D5                 | PB4     |
| G2        | D6                 | PB10    |
| B2        | D7                 | PA8     |
| A         | D8                 | PA9     |
| B         | D9                 | PC7     |
| C         | D10                | PB6     |
| CLK       | D11                | PA7     |
| LAT (STB) | D12                | PA6     |
| OE        | D13                | PA5     |
| GND       | GND                | -       |

D0/D1 (PA2/PA3, ST-LINK VCP) and the SWD pins (PA13/PA14) are left free on purpose.
If you'd rather wire it differently, just edit the `#define ..._PORT / ..._PIN` block
at the top of `hub75.c`.

## 2. Create the project in STM32CubeIDE

STM32CubeIDE's built-in CubeMX generates all the HAL/CMSIS/linker/startup boilerplate
correctly, so use that instead of hand-built project files:

1. **File > New > STM32 Project** -> Board Selector -> search **NUCLEO-F401RE** -> Next -> name it (e.g. `STM32_P10_RGB_32x16`) -> Finish. Accept the default "Initialize all peripherals with their default mode" (this sets up the ST-LINK VCP UART and the user LED/button, which don't conflict with anything above).
2. **Pinout & Configuration**, set each pin from the table above as **GPIO_Output**, and give it the matching label (PA10 -> `HUB75_R1`, etc.) via the right-click "Enter User Label" — purely cosmetic, but makes the generated `main.h` self-documenting.
3. Add **TIM3**: Categories -> Timers -> TIM3 -> Clock Source = Internal Clock.
   - Go to the **NVIC** tab of TIM3 and enable "TIM3 global interrupt".
4. Open the **Clock Configuration** tab, note the frequency shown for **APB1 Timer clocks** (with the default HSI config on a fresh Nucleo-F401RE project this is normally 84 MHz — confirm the number you actually see).
5. Back in TIM3 Parameter Settings, set:
   - **Prescaler**: `(APB1_timer_clock_Hz / 100000) - 1` — e.g. `839` for 84 MHz (gives a 100kHz counter tick).
   - **Counter Period (ARR)**: `99` (100kHz / 100 = 1kHz interrupt, i.e. each row-pair refreshes every 1ms, full 8-row frame every 8ms - flicker-free).
   - If your APB1 timer clock is a different number, redo the prescaler math so the tick lands on 100kHz.
6. **Project Manager** tab -> Toolchain/IDE should already say STM32CubeIDE. Click **GENERATE CODE**.

## 3. Drop in the driver files

Copy these three files from this folder into the generated project (overwrite nothing
else):

- `Core/Inc/hub75.h` -> project's `Core/Inc/`
- `Core/Src/hub75.c` -> project's `Core/Src/`
- `Core/Inc/font5x7.h` -> project's `Core/Inc/`

Refresh/rebuild the index in CubeIDE (right-click project -> Refresh) so it picks up
the new sources.

## 4. Edit the generated `main.c`

CubeMX only lets you safely edit inside the `/* USER CODE BEGIN ... */` /
`/* USER CODE END ... */` markers (anything else gets wiped on the next "Generate
Code"). Add the following:

```c
/* USER CODE BEGIN Includes */
#include "hub75.h"
/* USER CODE END Includes */
```

```c
/* USER CODE BEGIN 2 */
HUB75_Init();
HAL_TIM_Base_Start_IT(&htim3);

// Startup color test, same as the ESP32 demo.
HUB75_FillScreen(HUB75_RED);    HAL_Delay(800);
HUB75_FillScreen(HUB75_GREEN);  HAL_Delay(800);
HUB75_FillScreen(HUB75_BLUE);   HAL_Delay(800);
HUB75_FillScreen(HUB75_WHITE);  HAL_Delay(800);
HUB75_Clear();
/* USER CODE END 2 */
```

```c
/* USER CODE BEGIN WHILE */
while (1)
{
  HUB75_ScrollText(4, 35, "HELLO STM32 + P10 RGB 32X16", HUB75_YELLOW);
  /* USER CODE END WHILE */

  /* USER CODE BEGIN 3 */
}
/* USER CODE END 3 */
```

```c
/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM3)
  {
    HUB75_RefreshISR();
  }
}
/* USER CODE END 4 */
```

## 5. Build & flash

Nucleo boards have an onboard ST-LINK, so with the board plugged in just hit the
**Run** (or Debug) button in CubeIDE — it builds, flashes over the ST-LINK USB, and
resets the board automatically. No separate COM-port upload step like the ESP32 side
(the ST-LINK's VCP COM port is only for `printf`/UART debug, not for flashing).

## Notes / how to extend

- `hub75.h` exposes `HUB75_SetPixel`, `HUB75_FillScreen`, `HUB75_DrawText`,
  `HUB75_ScrollText` — same shape as the ESP32 sketch's helpers, so porting your
  scrolling-text logic over is mostly a find-and-replace.
- The font (`font5x7.h`) covers space, `0-9`, `A-Z` (lowercase is folded to uppercase),
  and `+ - . , ! :`. Any other character just draws blank — tell me which ones you
  need and I'll add them (each glyph is a 7-line ASCII-art block in
  `gen_font.py`-style source, regenerated to hex, so new ones are low-risk to add).
- This driver has no brightness control / BCM (only 8 solid colors, no dimming) —
  matching the `myCOLOR_ARRAY` 8-color demo from your ESP32 code. If you want
  brightness/grayscale later, that needs a bit-angle-modulation refresh scheme (more
  timer ISR complexity) — say so and I'll add it.
- I could not flash/verify this on real hardware from here (no ST-Link tools were
  installed on this machine, and PxMatrix's ESP32 wiring/HAL calls are well-trodden
  ground I've verified before, but this HUB75 bit-bang sequence and the font glyphs are
  new code) — please flash it and tell me exactly what you see (blank, right colors,
  garbled text, mirrored, flickering, wrong rows) so I can fix it in one pass instead
  of guessing.
