#include "hub75.h"
#include "font5x7.h"
#include <string.h>

// --------------------------------------------------------------------------
// Wiring (Nucleo-64 Arduino header -> HUB75 IDC connector), see README.md.
// Change these if you wire it differently.
// --------------------------------------------------------------------------
#define R1_PORT  GPIOA
#define R1_PIN   GPIO_PIN_10   // D2
#define G1_PORT  GPIOB
#define G1_PIN   GPIO_PIN_3    // D3
#define B1_PORT  GPIOB
#define B1_PIN   GPIO_PIN_5    // D4
#define R2_PORT  GPIOB
#define R2_PIN   GPIO_PIN_4    // D5
#define G2_PORT  GPIOB
#define G2_PIN   GPIO_PIN_10   // D6
#define B2_PORT  GPIOA
#define B2_PIN   GPIO_PIN_8    // D7
#define A_PORT   GPIOA
#define A_PIN    GPIO_PIN_9    // D8
#define B_PORT   GPIOC
#define B_PIN    GPIO_PIN_7    // D9
#define C_PORT   GPIOB
#define C_PIN    GPIO_PIN_6    // D10
#define CLK_PORT GPIOA
#define CLK_PIN  GPIO_PIN_7    // D11
#define LAT_PORT GPIOA
#define LAT_PIN  GPIO_PIN_6    // D12
#define OE_PORT  GPIOA
#define OE_PIN   GPIO_PIN_5    // D13

static uint8_t framebuffer[HUB75_HEIGHT][HUB75_WIDTH];
static volatile uint8_t current_row = 0;

static inline void pin_write(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state) {
  HAL_GPIO_WritePin(port, pin, state);
}

static void configure_output(GPIO_TypeDef *port, uint32_t pin) {
  GPIO_InitTypeDef init = {0};
  init.Pin = pin;
  init.Mode = GPIO_MODE_OUTPUT_PP;
  init.Pull = GPIO_NOPULL;
  init.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(port, &init);
}

void HUB75_Init(void) {
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  configure_output(R1_PORT, R1_PIN);
  configure_output(G1_PORT, G1_PIN);
  configure_output(B1_PORT, B1_PIN);
  configure_output(R2_PORT, R2_PIN);
  configure_output(G2_PORT, G2_PIN);
  configure_output(B2_PORT, B2_PIN);
  configure_output(A_PORT, A_PIN);
  configure_output(B_PORT, B_PIN);
  configure_output(C_PORT, C_PIN);
  configure_output(CLK_PORT, CLK_PIN);
  configure_output(LAT_PORT, LAT_PIN);
  configure_output(OE_PORT, OE_PIN);

  pin_write(OE_PORT, OE_PIN, GPIO_PIN_SET);   // OE active-low: start blanked
  pin_write(LAT_PORT, LAT_PIN, GPIO_PIN_RESET);
  pin_write(CLK_PORT, CLK_PIN, GPIO_PIN_RESET);

  HUB75_Clear();
  current_row = 0;
}

// Scans one row-pair per call. Call this from the TIM3 update IRQ (~500us period).
void HUB75_RefreshISR(void) {
  pin_write(OE_PORT, OE_PIN, GPIO_PIN_SET); // blank while shifting

  for (uint8_t col = 0; col < HUB75_WIDTH; col++) {
    uint8_t top = framebuffer[current_row][col];
    uint8_t bot = framebuffer[current_row + HUB75_ROWS][col];

    pin_write(R1_PORT, R1_PIN, (top & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    pin_write(G1_PORT, G1_PIN, (top & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    pin_write(B1_PORT, B1_PIN, (top & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    pin_write(R2_PORT, R2_PIN, (bot & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    pin_write(G2_PORT, G2_PIN, (bot & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    pin_write(B2_PORT, B2_PIN, (bot & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    pin_write(CLK_PORT, CLK_PIN, GPIO_PIN_SET);
    pin_write(CLK_PORT, CLK_PIN, GPIO_PIN_RESET);
  }

  pin_write(LAT_PORT, LAT_PIN, GPIO_PIN_SET);
  pin_write(LAT_PORT, LAT_PIN, GPIO_PIN_RESET);

  pin_write(A_PORT, A_PIN, (current_row & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  pin_write(B_PORT, B_PIN, (current_row & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  pin_write(C_PORT, C_PIN, (current_row & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);

  pin_write(OE_PORT, OE_PIN, GPIO_PIN_RESET); // enable output for this row-pair

  current_row = (current_row + 1) % HUB75_ROWS;
}

void HUB75_Clear(void) {
  memset(framebuffer, 0, sizeof(framebuffer));
}

void HUB75_FillScreen(uint8_t color) {
  for (uint8_t y = 0; y < HUB75_HEIGHT; y++) {
    for (uint8_t x = 0; x < HUB75_WIDTH; x++) {
      framebuffer[y][x] = color;
    }
  }
}

void HUB75_SetPixel(int16_t x, int16_t y, uint8_t color) {
  if (x < 0 || x >= HUB75_WIDTH || y < 0 || y >= HUB75_HEIGHT) return;
  framebuffer[y][x] = color & 0x07;
}

void HUB75_DrawChar(int16_t x, int16_t y, char c, uint8_t color) {
  if (c >= 'a' && c <= 'z') c -= 32; // fold lowercase onto the uppercase glyphs

  if ((uint8_t)c < FONT5X7_FIRST_CHAR || (uint8_t)c > FONT5X7_LAST_CHAR) return;

  const uint8_t *glyph = font5x7[(uint8_t)c - FONT5X7_FIRST_CHAR];
  for (uint8_t col = 0; col < FONT5X7_GLYPH_WIDTH; col++) {
    uint8_t bits = glyph[col];
    for (uint8_t row = 0; row < FONT5X7_GLYPH_HEIGHT; row++) {
      if (bits & (1 << row)) {
        HUB75_SetPixel(x + col, y + row, color);
      }
    }
  }
}

void HUB75_DrawText(int16_t x, int16_t y, const char *text, uint8_t color) {
  int16_t cursor = x;
  for (const char *p = text; *p; p++) {
    HUB75_DrawChar(cursor, y, *p, color);
    cursor += FONT5X7_GLYPH_WIDTH + 1; // 1px letter spacing
  }
}

uint16_t HUB75_TextWidth(const char *text) {
  size_t len = strlen(text);
  if (len == 0) return 0;
  return (uint16_t)(len * (FONT5X7_GLYPH_WIDTH + 1) - 1);
}

void HUB75_ScrollText(int16_t y, uint32_t speed_ms, const char *text, uint8_t color) {
  uint16_t text_w = HUB75_TextWidth(text);

  for (int16_t x = HUB75_WIDTH; x > -(int16_t)(HUB75_WIDTH + text_w); x--) {
    HUB75_Clear();
    HUB75_DrawText(x, y, text, color);
    HAL_Delay(speed_ms);
  }
}
