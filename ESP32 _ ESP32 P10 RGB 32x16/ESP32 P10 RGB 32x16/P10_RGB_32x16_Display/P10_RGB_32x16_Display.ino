// ESP32 + P10 RGB 32x16 (HUB75, Scan 1/8, P10-2727-8S-32X16-A) via PxMatrix
// Wiring matches Installation.png:
//   ESP32 19->A, 23->B, 18->C, 5->LAT/STB, 4->OE, 14->CLK, 13->R1, GND->GND

#define PxMATRIX_SPI_FREQUENCY 10000000

#include <PxMatrix.h>

#define P_LAT 5
#define P_A   19
#define P_B   23
#define P_C   18
#define P_OE  4

#define matrix_width  32
#define matrix_height 16

hw_timer_t *timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

uint8_t display_draw_time = 30; // 30-70 is usually fine, higher = brighter but may crash if too high

PxMATRIX display(matrix_width, matrix_height, P_LAT, P_OE, P_A, P_B, P_C);

uint16_t myRED     = display.color565(255, 0, 0);
uint16_t myGREEN   = display.color565(0, 255, 0);
uint16_t myBLUE    = display.color565(0, 0, 255);
uint16_t myWHITE   = display.color565(255, 255, 255);
uint16_t myYELLOW  = display.color565(255, 255, 0);
uint16_t myCYAN    = display.color565(0, 255, 255);
uint16_t myMAGENTA = display.color565(255, 0, 255);
uint16_t myBLACK   = display.color565(0, 0, 0);
uint16_t myPurple   = display.color565(119, 0, 200);

// Edit this to change the scrolling message.
const char scrolling_message[] = "Hello, World!  ESP32 + P10 RGB 32x16  ";

void IRAM_ATTR display_updater() {
  portENTER_CRITICAL_ISR(&timerMux);
  display.display(display_draw_time);
  portEXIT_CRITICAL_ISR(&timerMux);
}

void display_update_enable(bool is_enable) {
  if (is_enable) {
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &display_updater, true);
    timerAlarmWrite(timer, 1500, true);
    timerAlarmEnable(timer);
  } else {
    timerDetachInterrupt(timer);
    timerAlarmDisable(timer);
  }
}

uint16_t getTextWidth(const char *text) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return w;
}

void run_scrolling_text(uint8_t y_pos, unsigned long speed_ms, const char *text, uint16_t color) {
  uint16_t text_width_px = getTextWidth(text);

  for (int x = matrix_width; x > -(matrix_width + text_width_px); x--) {
    display.setTextColor(myBLACK);
    display.setCursor(x + 1, y_pos);
    display.print(text);

    display.setTextColor(color);
    display.setCursor(x, y_pos);
    display.print(text);

    delay(speed_ms);
  }
}

void setup() {
  delay(2000);
  Serial.begin(115200);
  Serial.println();

  display.begin(8); // 8 = 1/8 row scan panel
  delay(100);

  display_update_enable(true);
  delay(100);

  display.clearDisplay();
  display.setBrightness(125); // 0-255
  display.setTextWrap(false);
  display.setTextSize(1);
  display.setRotation(0);

  // Quick startup color test.
  display.fillScreen(myRED);
  delay(800);
  display.fillScreen(myGREEN);
  delay(800);
  display.fillScreen(myBLUE);
  delay(800);
  display.fillScreen(myWHITE);
  delay(800);
  display.clearDisplay();
  delay(500);
}

void loop() {
  run_scrolling_text(4, 35, scrolling_message, myPurple);
}
