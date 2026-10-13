"""Reads /etc/speeddisplay/config.ini (see deploy/config.ini)."""

import configparser
from dataclasses import dataclass


@dataclass
class Look:
    panel_w: int = 32
    panel_h: int = 16
    order_reversed: bool = False
    flip_mask: int = 0b010
    ring: bool = True
    ring_color: tuple = (0, 70, 255)
    ring_thickness: int = 2
    digit_stroke: int = 3
    digit_w: int = 10
    digit_h: int = 18
    digit_gap: int = 2


@dataclass
class Config:
    name: str
    app_secret: str
    bind: str
    port: int
    state_path: str
    preview: bool
    look: Look
    matrix: dict
    defaults: dict
    video_fit: str = "fill"
    video_dirs: tuple = ()
    video_fps: int = 30


def _rgb(s):
    r, g, b = (int(v) for v in s.split(","))
    return (r, g, b)


def load(path) -> Config:
    cp = configparser.ConfigParser(inline_comment_prefixes=(";", "#"))
    if not cp.read(path):
        raise SystemExit(f"config file not found: {path}")
    d, n, p, lk, df = cp["display"], cp["network"], cp["panel"], cp["look"], cp["defaults"]
    v = cp["video"] if cp.has_section("video") else {}
    look = Look(
        panel_w=p.getint("cols", 32),
        panel_h=p.getint("rows", 16),
        order_reversed=p.getboolean("order_reversed", False),
        flip_mask=int(p.get("flip_mask", "0b010"), 0),
        ring=lk.getboolean("ring", True),
        ring_color=_rgb(lk.get("ring_color", "0,70,255")),
        ring_thickness=lk.getint("ring_thickness", 2),
        digit_stroke=lk.getint("digit_stroke", 3),
        digit_w=lk.getint("digit_w", 10),
        digit_h=lk.getint("digit_h", 18),
        digit_gap=lk.getint("digit_gap", 2),
    )
    matrix = {
        "rows": p.getint("rows", 16),
        "cols": p.getint("cols", 32),
        "chain": p.getint("chain", 3),
        "hardware_mapping": p.get("gpio_mapping", "speeddisplay"),
        "gpio_slowdown": p.getint("gpio_slowdown", 4),
        "pwm_bits": p.getint("pwm_bits", 11),
        "pwm_lsb_nanoseconds": p.getint("pwm_lsb_nanoseconds", 130),
        "multiplexing": p.getint("multiplexing", 0),
        "row_address_type": p.getint("row_address_type", 0),
        "scan_mode": p.getint("scan_mode", 0),
        "disable_hardware_pulsing": p.getboolean("no_hardware_pulse", True),
        "led_rgb_sequence": p.get("rgb_sequence", "RGB"),
    }
    defaults = {
        "limit": df.getint("limit", 40),
        "bri": df.getint("brightness", 125),
        "dcol": df.getint("digit_color", 0),
        "tcol": df.getint("text_color", 3),
    }
    return Config(
        name=d.get("name", "SpeedDisplayPi1"),
        app_secret=cp["security"]["app_secret"],
        bind=n.get("bind", "0.0.0.0"),
        port=n.getint("port", 8099),
        state_path=d.get("state_path", "/var/lib/speeddisplay/settings.json"),
        preview=d.getboolean("preview_only", False),
        look=look,
        matrix=matrix,
        defaults=defaults,
        video_fit=v.get("fit", "fill"),
        video_dirs=tuple(x.strip() for x in v.get("dirs", "/var/lib/speeddisplay/videos").split(",") if x.strip()),
        video_fps=int(v.get("fps", 30)),
    )
