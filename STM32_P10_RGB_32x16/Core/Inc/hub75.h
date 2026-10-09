// HUB75 driver for P10 RGB 32x16 (P10-2727-8S-32X16-A, scan 1/8), STM32 HAL, no BCM (8 solid colors).
// Call HUB75_Init() once, then call HUB75_RefreshISR() from the TIM3 update-interrupt
// callback (period ~500us, see README). Edit the framebuffer from the main loop with
// HUB75_SetPixel / HUB75_FillScreen / HUB75_DrawText / HUB75_ScrollText; the ISR keeps
// scanning whatever is currently in the framebuffer.
#ifndef HUB75_H
#define HUB75_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

#define HUB75_WIDTH   32
#define HUB75_HEIGHT  16
#define HUB75_ROWS    (HUB75_HEIGHT / 2) // 8 row-pairs for a 1/8 scan panel

// 3-bit solid colors (no brightness/PWM): bit0=R, bit1=G, bit2=B.
#define HUB75_BLACK   0x00
#define HUB75_RED     0x01
#define HUB75_GREEN   0x02
#define HUB75_YELLOW  0x03
#define HUB75_BLUE    0x04
#define HUB75_MAGENTA 0x05
#define HUB75_CYAN    0x06
#define HUB75_WHITE   0x07

void HUB75_Init(void);
void HUB75_RefreshISR(void);

void HUB75_Clear(void);
void HUB75_FillScreen(uint8_t color);
void HUB75_SetPixel(int16_t x, int16_t y, uint8_t color);

void HUB75_DrawChar(int16_t x, int16_t y, char c, uint8_t color);
void HUB75_DrawText(int16_t x, int16_t y, const char *text, uint8_t color);
uint16_t HUB75_TextWidth(const char *text);

// Blocking: scrolls text right-to-left across the panel once. Call repeatedly (e.g. in
// a while(1) loop) for continuous scrolling. speed_ms is the delay per 1px step.
void HUB75_ScrollText(int16_t y, uint32_t speed_ms, const char *text, uint8_t color);

#endif // HUB75_H
