"""Draws the speed-limit screen exactly like the ESP32 firmware (SpeedLimitDisplay.ino).

Logical canvas (LAYOUT_VERTICAL): 32 x 48 = three 32x16 tiles one below the other
  tile 1 : "SPEED" / "LIMIT"            (text colour)
  tile 2+3: blue ring + bold 2-digit number (digit colour)
The canvas is then mapped onto the physical chain of panels (96 x 16 for 3 panels).
"""

from .font5x7 import GLYPHS

# Same order as COLOR_PRESETS in the firmware / app: 0 Red, 1 White, 2 Yellow, 3 Green, 4 Amber, 5 Cyan
COLOR_PRESETS = [(255, 0, 0), (255, 255, 255), (255, 255, 0), (0, 255, 0), (255, 110, 0), (0, 255, 255)]

BLACK = (0, 0, 0)

# 5x7 bold digits (same as BOLD_DIGITS in BigDigits.h). Bars = rows 0, 3, 6; side strokes = cols 0, 4
# (col 2 for "1"); stroke cells get `stroke` px, the gaps share the rest of the box.
BOLD_DIGITS = [
    ["#####", "#...#", "#...#", "#...#", "#...#", "#...#", "#####"],  # 0
    [".##..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],  # 1
    ["#####", "....#", "....#", "#####", "#....", "#....", "#####"],  # 2
    ["#####", "....#", "....#", ".####", "....#", "....#", "#####"],  # 3
    ["#...#", "#...#", "#...#", "#####", "....#", "....#", "....#"],  # 4
    ["#####", "#....", "#....", "#####", "....#", "....#", "#####"],  # 5
    ["#####", "#....", "#....", "#####", "#...#", "#...#", "#####"],  # 6
    ["#####", "....#", "....#", "....#", "....#", "....#", "....#"],  # 7
    ["#####", "#...#", "#...#", "#####", "#...#", "#...#", "#####"],  # 8
    ["#####", "#...#", "#...#", "#####", "....#", "....#", "#####"],  # 9
]


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[BLACK] * w for _ in range(h)]

    def set(self, x, y, color):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = color

    def fill_rect(self, x, y, w, h, color):
        for j in range(y, y + h):
            for i in range(x, x + w):
                self.set(i, j, color)

    # Adafruit GFX classic font: 6 px advance, text width = 6 * len (incl. trailing spacing).
    def text(self, s, x, y, color):
        for ch in s:
            cols = GLYPHS.get(ch, GLYPHS["?"])
            for i, bits in enumerate(cols):
                for j in range(8):
                    if bits & (1 << j):
                        self.set(x + i, y + j, color)
            x += 6

    def text_centered(self, s, area_x, area_w, y, color):
        self.text(s, area_x + (area_w - 6 * len(s)) // 2, y, color)

    # Ring touching the edges of the size x size square at (x, y) (same maths as drawRing()).
    def ring(self, x, y, size, thickness, color):
        c = (size - 1) / 2.0
        r_out = size / 2.0
        r_in = r_out - thickness
        for j in range(size):
            for i in range(size):
                d2 = (i - c) ** 2 + (j - c) ** 2
                if r_in * r_in <= d2 <= r_out * r_out:
                    self.set(x + i, y + j, color)


def _bold_sizes(is_stroke, total, stroke):
    rest = max(0, total - sum(is_stroke) * stroke)
    n_plain = len(is_stroke) - sum(is_stroke)
    out, k = [], 0
    for s in is_stroke:
        if s:
            out.append(stroke)
        else:
            out.append(rest // n_plain + (1 if k < rest % n_plain else 0))
            k += 1
    return out


def draw_bold_digit(cv, x, y, w, h, digit, stroke, color):
    g = BOLD_DIGITS[digit]
    row_bar = [r in (0, 3, 6) for r in range(7)]
    col_stroke = [(c == 2) if digit == 1 else (c in (0, 4)) for c in range(5)]
    rh = _bold_sizes(row_bar, h, stroke)
    cw = _bold_sizes(col_stroke, w, stroke)
    yy = y
    for r in range(7):
        xx = x
        for c in range(5):
            if g[r][c] == "#":
                cv.fill_rect(xx, yy, cw[c], rh[r], color)
            xx += cw[c]
        yy += rh[r]
    # round the outer corners by 1 px
    if g[0][0] == "#":
        cv.set(x, y, BLACK)
    if g[0][4] == "#":
        cv.set(x + w - 1, y, BLACK)
    if g[6][0] == "#":
        cv.set(x, y + h - 1, BLACK)
    if g[6][4] == "#":
        cv.set(x + w - 1, y + h - 1, BLACK)


def draw_bold_number(cv, area_x, area_y, area_w, area_h, value, w, h, gap, stroke, color):
    text = str(min(int(value), 99))
    total_w = len(text) * w + (len(text) - 1) * gap
    x = area_x + (area_w - total_w) // 2
    y = area_y + (area_h - h) // 2
    for ch in text:
        draw_bold_digit(cv, x, y, w, h, int(ch), stroke, color)
        x += w + gap


def preset(index):
    return COLOR_PRESETS[index] if 0 <= index < len(COLOR_PRESETS) else COLOR_PRESETS[0]


def render_speed_limit(state, look):
    """state: dict(limit, dcol, tcol); look: Look (panel / ring / digit sizes). Returns a Canvas."""
    pw, ph = look.panel_w, look.panel_h
    cv = Canvas(pw, ph * 3)
    tc = preset(state["tcol"])
    cv.text_centered("SPEED", 0, pw, 0, tc)   # rows 0..6
    cv.text_centered("LIMIT", 0, pw, 9, tc)   # rows 9..15
    if look.ring:
        cv.ring(0, ph, pw, look.ring_thickness, look.ring_color)
    draw_bold_number(cv, 0, ph, pw, 2 * ph, state["limit"], look.digit_w, look.digit_h, look.digit_gap,
                     look.digit_stroke, preset(state["dcol"]))
    return cv


def render_device_key(key, look):
    """Shows the device key so the owner can add the display in the app."""
    pw, ph = look.panel_w, look.panel_h
    cv = Canvas(pw, ph * 3)
    cv.text_centered("KEY", 0, pw, 4, (255, 255, 0))
    for i in range(3):
        part = key[i * 4:i * 4 + 4]
        if part:
            cv.text_centered(part, 0, pw, ph + 2 + i * 10, (255, 255, 255))
    return cv


def canvas_from_rows(rows):
    """Wraps a video frame (rows of RGB tuples) as a Canvas."""
    cv = Canvas(len(rows[0]), len(rows))
    cv.px = rows
    return cv


def to_chain(cv, look):
    """Logical 32x48 canvas -> physical chain image (panel_w * 3) x panel_h, as rows of RGB tuples.

    Same mapping as drawLogicalPixel(): tile n = canvas rows n*16..n*16+15, optional reversed order,
    and panels in look.flip_mask are rotated 180 degrees.
    """
    pw, ph, n = look.panel_w, look.panel_h, 3
    out = [[BLACK] * (pw * n) for _ in range(ph)]
    for y in range(cv.h):
        panel, ly = divmod(y, ph)
        if look.order_reversed:
            panel = n - 1 - panel
        flip = (look.flip_mask >> panel) & 1
        for x in range(cv.w):
            lx, yy = x, ly
            if flip:
                lx, yy = pw - 1 - lx, ph - 1 - yy
            out[yy][panel * pw + lx] = cv.px[y][x]
    return out
