#pragma once

// 7x8 digit font that is stretched to any box size.
// Horizontal bars and vertical strokes are always 2 px thick ("double pixel");
// the remaining rows/columns share the leftover space.
//   14x16 box -> exactly the font drawn at 2x (horizontal layout)
//   14x32 / 9x32 box -> tall digits for 2 / 3 digit numbers (vertical layout)
// Edit the '#' patterns to change the look of a digit.

#define BIG_DIGIT_COLS   7
#define BIG_DIGIT_ROWS   8
#define BIG_DIGIT_STROKE 2
#define BIG_DIGIT_MAX_W  14

static const char *const BIG_DIGITS[10][BIG_DIGIT_ROWS] = {
  { ".#####.", "#.....#", "#.....#", "#.....#", "#.....#", "#.....#", "#.....#", ".#####." }, // 0
  { "...#...", "..##...", ".#.#...", "...#...", "...#...", "...#...", "...#...", ".#####." }, // 1
  { ".#####.", "#.....#", "......#", "....##.", "..##...", ".#.....", "#......", "#######" }, // 2
  { ".#####.", "#.....#", "......#", "..####.", "......#", "......#", "#.....#", ".#####." }, // 3
  { "#....#.", "#....#.", "#....#.", "#....#.", "#....#.", "#######", ".....#.", ".....#." }, // 4 (open, road-sign style)
  { "#######", "#......", "#......", "######.", "......#", "......#", "#.....#", ".#####." }, // 5
  { "..####.", ".#.....", "#......", "######.", "#.....#", "#.....#", "#.....#", ".#####." }, // 6
  { "#######", "......#", ".....#.", "....#..", "...#...", "...#...", "...#...", "...#..." }, // 7
  { ".#####.", "#.....#", "#.....#", ".#####.", "#.....#", "#.....#", "#.....#", ".#####." }, // 8
  { ".#####.", "#.....#", "#.....#", "#.....#", ".######", "......#", ".....#.", ".####.." }, // 9
};

// Splits `total` pixels over n cells: stroke cells get BIG_DIGIT_STROKE px,
// the others share the rest. Falls back to an even split if it does not fit.
inline void bigDigitSizes(const bool *stroke, uint8_t n, int16_t total, uint8_t *out) {
  uint8_t nStroke = 0;
  for (uint8_t i = 0; i < n; i++) nStroke += stroke[i];
  int16_t rest = total - nStroke * BIG_DIGIT_STROKE;
  uint8_t nPlain = n - nStroke;
  bool even = (nPlain == 0 || rest < nPlain);
  if (even) { rest = total; nPlain = n; }
  uint8_t k = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (stroke[i] && !even) {
      out[i] = BIG_DIGIT_STROKE;
    } else {
      out[i] = rest / nPlain + (k < rest % nPlain ? 1 : 0);
      k++;
    }
  }
}

// Draws one digit stretched into the box (x, y, w, h).
template <typename GFX>
void drawBigDigit(GFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t digit, uint16_t color) {
  if (digit > 9) return;
  const char *const *g = BIG_DIGITS[digit];

  bool rowBar[BIG_DIGIT_ROWS], colStroke[BIG_DIGIT_COLS];
  for (uint8_t r = 0; r < BIG_DIGIT_ROWS; r++) rowBar[r] = strstr(g[r], "###") != nullptr;  // run of >= 3
  for (uint8_t c = 0; c < BIG_DIGIT_COLS; c++) {
    colStroke[c] = false;
    for (uint8_t r = 0; r + 1 < BIG_DIGIT_ROWS; r++) {
      if (g[r][c] == '#' && g[r + 1][c] == '#') colStroke[c] = true;                    // run of >= 2
    }
  }

  uint8_t rh[BIG_DIGIT_ROWS], cw[BIG_DIGIT_COLS];
  bigDigitSizes(rowBar, BIG_DIGIT_ROWS, h, rh);
  bigDigitSizes(colStroke, BIG_DIGIT_COLS, w, cw);

  int16_t yy = y;
  for (uint8_t r = 0; r < BIG_DIGIT_ROWS; r++) {
    int16_t xx = x;
    for (uint8_t c = 0; c < BIG_DIGIT_COLS; c++) {
      if (g[r][c] == '#') gfx.fillRect(xx, yy, cw[c], rh[r], color);
      xx += cw[c];
    }
    yy += rh[r];
  }
}

// Draws a number (0-999) centred in the area (areaX, areaY, areaW, areaH).
// gap < 0 -> default spacing between digits (3 px, 2 px for 3 digits).
template <typename GFX>
void drawBigNumber(GFX &gfx, int16_t areaX, int16_t areaY, int16_t areaW, int16_t areaH, uint16_t value, uint16_t color,
                   int16_t gap = -1) {
  char text[4];
  snprintf(text, sizeof(text), "%u", (unsigned)(value > 999 ? 999 : value));
  uint8_t n = strlen(text);
  if (gap < 0) gap = (n == 3) ? 2 : 3;
  int16_t w = (areaW - (n - 1) * gap) / n;
  if (w > BIG_DIGIT_MAX_W) w = BIG_DIGIT_MAX_W;
  int16_t totalW = n * w + (n - 1) * gap;
  int16_t x = areaX + (areaW - totalW) / 2;
  for (uint8_t i = 0; i < n; i++) {
    drawBigDigit(gfx, x, areaY, w, areaH, text[i] - '0', color);
    x += w + gap;
  }
}

//________________________________________________________________________________ Bold digits
// 5x7 bold digit font for the round sign: bars (rows 0, 3, 6) and side strokes
// (cols 0 and 4; col 2 for "1") are BOLD_STROKE px thick, the gaps share the rest.
// 10x18 box with 3 px strokes inside a 2 px ring ~ the look of a real speed-limit sign.
#define BOLD_DIGIT_COLS 5
#define BOLD_DIGIT_ROWS 7

static const char *const BOLD_DIGITS[10][BOLD_DIGIT_ROWS] = {
  { "#####", "#...#", "#...#", "#...#", "#...#", "#...#", "#####" }, // 0
  { ".##..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." }, // 1
  { "#####", "....#", "....#", "#####", "#....", "#....", "#####" }, // 2
  { "#####", "....#", "....#", ".####", "....#", "....#", "#####" }, // 3
  { "#...#", "#...#", "#...#", "#####", "....#", "....#", "....#" }, // 4
  { "#####", "#....", "#....", "#####", "....#", "....#", "#####" }, // 5
  { "#####", "#....", "#....", "#####", "#...#", "#...#", "#####" }, // 6
  { "#####", "....#", "....#", "....#", "....#", "....#", "....#" }, // 7
  { "#####", "#...#", "#...#", "#####", "#...#", "#...#", "#####" }, // 8
  { "#####", "#...#", "#...#", "#####", "....#", "....#", "#####" }, // 9
};

// Fixed-stroke split: stroke cells get `stroke` px, the others share the rest.
inline void boldSizes(const bool *isStroke, uint8_t n, int16_t total, uint8_t stroke, uint8_t *out) {
  uint8_t nStroke = 0;
  for (uint8_t i = 0; i < n; i++) nStroke += isStroke[i];
  int16_t rest = total - nStroke * stroke;
  if (rest < 0) rest = 0;
  uint8_t nPlain = n - nStroke, k = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (isStroke[i]) {
      out[i] = stroke;
    } else {
      out[i] = rest / nPlain + (k < rest % nPlain ? 1 : 0);
      k++;
    }
  }
}

// Draws one bold digit in the box (x, y, w, h); the 4 outer corners are rounded by 1 px.
template <typename GFX>
void drawBoldDigit(GFX &gfx, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t digit, uint8_t stroke, uint16_t color) {
  if (digit > 9) return;
  const char *const *g = BOLD_DIGITS[digit];

  bool rowBar[BOLD_DIGIT_ROWS], colStroke[BOLD_DIGIT_COLS];
  for (uint8_t r = 0; r < BOLD_DIGIT_ROWS; r++) rowBar[r] = (r == 0 || r == 3 || r == 6);
  for (uint8_t c = 0; c < BOLD_DIGIT_COLS; c++) colStroke[c] = (digit == 1) ? (c == 2) : (c == 0 || c == 4);

  uint8_t rh[BOLD_DIGIT_ROWS], cw[BOLD_DIGIT_COLS];
  boldSizes(rowBar, BOLD_DIGIT_ROWS, h, stroke, rh);
  boldSizes(colStroke, BOLD_DIGIT_COLS, w, stroke, cw);

  int16_t yy = y;
  for (uint8_t r = 0; r < BOLD_DIGIT_ROWS; r++) {
    int16_t xx = x;
    for (uint8_t c = 0; c < BOLD_DIGIT_COLS; c++) {
      if (g[r][c] == '#') gfx.fillRect(xx, yy, cw[c], rh[r], color);
      xx += cw[c];
    }
    yy += rh[r];
  }

  // Round the outer corners (only where the corner pixel is lit by this digit).
  const char *top = g[0], *bottom = g[BOLD_DIGIT_ROWS - 1];
  if (top[0] == '#') gfx.drawPixel(x, y, 0);
  if (top[BOLD_DIGIT_COLS - 1] == '#') gfx.drawPixel(x + w - 1, y, 0);
  if (bottom[0] == '#') gfx.drawPixel(x, y + h - 1, 0);
  if (bottom[BOLD_DIGIT_COLS - 1] == '#') gfx.drawPixel(x + w - 1, y + h - 1, 0);
}

// Draws a 1-2 digit number with bold digits (each w x h) centred in the area.
template <typename GFX>
void drawBoldNumber(GFX &gfx, int16_t areaX, int16_t areaY, int16_t areaW, int16_t areaH,
                    uint16_t value, int16_t w, int16_t h, int16_t gap, uint8_t stroke, uint16_t color) {
  char text[4];
  snprintf(text, sizeof(text), "%u", (unsigned)(value > 99 ? 99 : value));
  uint8_t n = strlen(text);
  int16_t totalW = n * w + (n - 1) * gap;
  int16_t x = areaX + (areaW - totalW) / 2;
  int16_t y = areaY + (areaH - h) / 2;
  for (uint8_t i = 0; i < n; i++) {
    drawBoldDigit(gfx, x, y, w, h, text[i] - '0', stroke, color);
    x += w + gap;
  }
}
