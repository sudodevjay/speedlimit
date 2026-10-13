"""Output to the P10 panels via hzeller/rpi-rgb-led-matrix, or a PNG-only preview.

The preview (GET /preview.png = the sign, /preview-chain.png = raw panel chain) always shows
exactly what is sent to the panels, so the service can be tested without looking at the sign.
"""

import struct
import threading
import zlib


def png_bytes(rows, scale=8):
    """Tiny PNG encoder (no Pillow needed). rows: list of rows of (r, g, b)."""
    h, w = len(rows), len(rows[0])
    raw = bytearray()
    for row in rows:
        line = bytearray()
        for (r, g, b) in row:
            line += bytes((r, g, b)) * scale
        for _ in range(scale):
            raw += b"\x00" + line

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")


class Panel:
    def __init__(self, matrix_opts, preview_only=False):
        self._lock = threading.Lock()
        self._frame = None
        self._logical = None
        self.matrix = None
        self.canvas = None
        self.error = None
        if preview_only:
            self.error = "preview_only = yes in config"
            return
        try:
            from rgbmatrix import RGBMatrix, RGBMatrixOptions
        except ImportError as e:
            self.error = f"rgbmatrix not installed ({e})"
            return
        o = RGBMatrixOptions()
        o.rows = matrix_opts["rows"]
        o.cols = matrix_opts["cols"]
        o.chain_length = matrix_opts["chain"]
        o.parallel = 1
        o.hardware_mapping = matrix_opts["hardware_mapping"]
        o.gpio_slowdown = matrix_opts["gpio_slowdown"]
        o.pwm_bits = matrix_opts["pwm_bits"]
        o.pwm_lsb_nanoseconds = matrix_opts["pwm_lsb_nanoseconds"]
        o.multiplexing = matrix_opts["multiplexing"]
        o.row_address_type = matrix_opts["row_address_type"]
        o.scan_mode = matrix_opts["scan_mode"]
        o.disable_hardware_pulsing = matrix_opts["disable_hardware_pulsing"]
        o.led_rgb_sequence = matrix_opts["led_rgb_sequence"]
        o.drop_privileges = False  # keep root: we also write the settings file
        self.matrix = RGBMatrix(options=o)
        self.canvas = self.matrix.CreateFrameCanvas()

    def show(self, rows, brightness_255, logical=None):
        """rows: physical chain image; logical: the sign as people see it (for the preview)."""
        with self._lock:
            self._frame = rows
            self._logical = logical or rows
            if not self.matrix:
                return
            self.matrix.brightness = max(1, min(100, round(brightness_255 * 100 / 255)))
            c = self.canvas
            c.Clear()
            for y, row in enumerate(rows):
                for x, (r, g, b) in enumerate(row):
                    if r or g or b:
                        c.SetPixel(x, y, r, g, b)
            self.canvas = self.matrix.SwapOnVSync(c)

    def preview_png(self, chain=False):
        with self._lock:
            rows = self._frame if chain else self._logical
        if rows is None:
            return None
        return png_bytes(rows)

    def close(self):
        if self.matrix:
            self.matrix.Clear()
