"""Speed limit display service for Raspberry Pi: panels + Wi-Fi (HTTP) API.

The API speaks the SAME signed protocol as the ESP32 over BLE, only the transport differs:

  GET  /api/info      -> {"name","mac","fw","transport":"wifi"}
  POST /api/session   -> {"sid": "...", "nonce": "<32 hex>"}   (= a new BLE connection)
  POST /api/cmd       header X-Session: <sid>, body "<ctr>|<cmd>|<mac>"
                      -> text "<ctr>|<OK|ERR>|<payload>|<mac>"  (= write CMD + read RESP)
  GET  /preview.png   the sign as it looks right now (/preview-chain.png = raw panel chain)

Extra command (Pi only): MODE:speed | MODE:video  -> video mode plays the videos from a USB stick
in a loop; with no stick / no videos the sign shows the speed limit.
"""

import json
import logging
import os
import re
import secrets as pysecrets
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from . import render, security
from .panel import Panel
from .video import VideoPlayer

FW_VERSION = "pi-1.0.0"
MAX_LIMIT = 99

AUTH_TIMEOUT = 15        # s: session not authenticated within this time -> dropped
IDLE_TIMEOUT = 120       # s: no valid command for this time -> dropped
MAX_SESSIONS = 8
MAX_AUTH_FAILS = 3       # wrong signatures from one IP before lock-out
LOCKOUT_SECONDS = 30
KEY_SHOW_SECONDS = 60
USB_SCAN_SECONDS = 10    # how often the USB stick is checked while in speed-limit mode
MODES = ("speed", "video")

log = logging.getLogger("speeddisplay")


def wlan_mac():
    for iface in ("wlan0", "eth0"):
        try:
            with open(f"/sys/class/net/{iface}/address") as f:
                return f.read().strip().upper()
        except OSError:
            pass
    return "00:00:00:00:00:00"


class Store:
    """Settings in a small JSON file (written atomically)."""

    def __init__(self, path, name, defaults):
        self.path = path
        self.state = {**defaults, "name": name, "key": "", "mode": "speed"}
        try:
            with open(path) as f:
                saved = json.load(f)
            self.state.update({k: saved[k] for k in self.state if k in saved})
        except (OSError, ValueError):
            pass
        s = self.state
        if not 0 <= int(s["limit"]) <= MAX_LIMIT:
            s["limit"] = defaults["limit"]
        for k in ("dcol", "tcol"):
            if not 0 <= int(s[k]) < len(render.COLOR_PRESETS):
                s[k] = defaults[k]
        if s["mode"] not in MODES:
            s["mode"] = "speed"

    def save(self):
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        tmp = self.path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(self.state, f, indent=1)
        os.chmod(tmp, 0o600)  # contains the device key
        os.replace(tmp, self.path)


class Session:
    def __init__(self, ip):
        self.ip = ip
        self.nonce = security.new_nonce()
        self.created = self.last = time.monotonic()
        self.last_counter = 0
        self.authed = False


class SpeedDisplay:
    def __init__(self, cfg):
        self.cfg = cfg
        self.lock = threading.RLock()
        self.store = Store(cfg.state_path, cfg.name, cfg.defaults)
        self.mac = wlan_mac()
        self.sessions = {}
        self.fails = {}           # ip -> count
        self.lockout = {}         # ip -> unlock time
        self.show_key_until = 0
        self.panel = Panel(cfg.matrix, cfg.preview)
        if self.panel.error:
            log.warning("Panels not driven: %s (preview only)", self.panel.error)
        self.player = VideoPlayer(cfg.look.panel_w, cfg.look.panel_h * 3, self._show_video_frame, self.redraw,
                                  fit=cfg.video_fit, extra_dirs=cfg.video_dirs, fps=cfg.video_fps)
        self._last_usb_scan = 0

        if not self.store.state["key"]:
            self.new_device_key()
        else:
            log.info("Device key: %s", self.store.state["key"])
        self.K = security.derive_key(cfg.app_secret, self.store.state["key"])
        self.redraw()
        if self.store.state["mode"] == "video":
            self.player.start()

    # ------------------------------------------------------------------ display
    def redraw(self):
        with self.lock:
            s = dict(self.store.state)
            showing_key = time.monotonic() < self.show_key_until
        if s["mode"] == "video" and self.player.current and not showing_key:
            return  # a video is on the sign; it shows the next frame itself
        if showing_key:
            cv = render.render_device_key(s["key"], self.cfg.look)
        else:
            cv = render.render_speed_limit(s, self.cfg.look)
        self.panel.show(render.to_chain(cv, self.cfg.look), s["bri"], logical=cv.px)

    def _show_video_frame(self, rows):
        with self.lock:
            if self.store.state["mode"] != "video" or time.monotonic() < self.show_key_until:
                return
            bri = self.store.state["bri"]
        cv = render.canvas_from_rows(rows)
        self.panel.show(render.to_chain(cv, self.cfg.look), bri, logical=rows)

    def new_device_key(self):
        key = security.generate_device_key()
        with self.lock:
            self.store.state["key"] = key
            self.store.save()
            self.K = security.derive_key(self.cfg.app_secret, key)
            self.sessions.clear()
            self.show_key_until = time.monotonic() + KEY_SHOW_SECONDS
        log.info("==============================================")
        log.info("  NEW DEVICE KEY : %s", key)
        log.info("  Write it down / stick it inside the enclosure.")
        log.info("==============================================")

    def tick(self):
        """Called every second: session timeouts, end of the key screen."""
        now = time.monotonic()
        with self.lock:
            for sid, s in list(self.sessions.items()):
                if (not s.authed and now - s.created > AUTH_TIMEOUT) or (s.authed and now - s.last > IDLE_TIMEOUT):
                    del self.sessions[sid]
            key_ended = self.show_key_until and now >= self.show_key_until
            if key_ended:
                self.show_key_until = 0
        if key_ended:
            self.redraw()
        # Keep the USB video count fresh for the app while the player is not running.
        if not self.player.running and now - self._last_usb_scan > USB_SCAN_SECONDS:
            self._last_usb_scan = now
            self.player.scan()

    # ------------------------------------------------------------------ protocol
    def info(self):
        with self.lock:
            name = self.store.state["name"]
        return {"name": name, "mac": self.mac, "fw": FW_VERSION, "transport": "wifi"}

    def open_session(self, ip):
        now = time.monotonic()
        with self.lock:
            if self.lockout.get(ip, 0) > now:
                return None
            if len(self.sessions) >= MAX_SESSIONS:
                oldest = min(self.sessions, key=lambda k: self.sessions[k].last)
                del self.sessions[oldest]
            sid = pysecrets.token_hex(12)
            s = Session(ip)
            self.sessions[sid] = s
            return {"sid": sid, "nonce": s.nonce}

    def _failure(self, ip):
        self.fails[ip] = self.fails.get(ip, 0) + 1
        log.warning("Bad signature from %s (%d/%d)", ip, self.fails[ip], MAX_AUTH_FAILS)
        if self.fails[ip] >= MAX_AUTH_FAILS:
            self.lockout[ip] = time.monotonic() + LOCKOUT_SECONDS
            self.fails[ip] = 0
            for sid in [k for k, v in self.sessions.items() if v.ip == ip]:
                del self.sessions[sid]
            log.warning("Too many failures from %s -> lock-out %ds", ip, LOCKOUT_SECONDS)

    def _signed(self, K, nonce, ctr, status, payload):
        return f"{ctr}|{status}|{payload}|{security.mac32(K, f'{nonce}|R|{ctr}|{status}|{payload}')}"

    def state_string(self):
        s = self.store.state
        playing = 1 if (s["mode"] == "video" and self.player.current) else 0
        return (f"lim={s['limit']};bri={s['bri']};dcol={s['dcol']};tcol={s['tcol']};name={s['name']}"
                f";mode={s['mode']};vid={len(self.player.videos)};play={playing}")

    def handle_command(self, ip, sid, raw):
        """Returns (http_status, response_text, after) - `after` runs once the reply is sent."""
        with self.lock:
            if self.lockout.get(ip, 0) > time.monotonic():
                return 429, "0|ERR|LOCKED|", None
            s = self.sessions.get(sid)
            if s is None:
                return 401, "0|ERR|SESSION|", None
            p1, p2 = raw.find("|"), raw.rfind("|")
            if len(raw) > 200 or p1 < 0 or p2 == p1:
                self._failure(ip)
                return 400, "0|ERR|FORMAT|", None
            ctr, cmd, mac = raw[:p1], raw[p1 + 1:p2], raw[p2 + 1:]
            K, nonce = self.K, s.nonce
            expected = security.mac32(K, f"{nonce}|{ctr}|{cmd}")
            if len(mac) != 32 or not security.ct_equal(mac, expected):
                self._failure(ip)
                return 403, "0|ERR|AUTH|", None
            if not re.fullmatch(r"\d{1,5}", ctr) or int(ctr) <= s.last_counter:
                return 200, self._signed(K, nonce, ctr, "ERR", "REPLAY"), None
            s.last_counter = int(ctr)
            s.last = time.monotonic()
            if not s.authed:
                log.info("Client %s authenticated", ip)
            s.authed = True
            self.fails[ip] = 0
            return self._execute(K, nonce, sid, ctr, cmd)

    def _execute(self, K, nonce, sid, ctr, cmd):
        op, _, arg = cmd.partition(":")
        st = self.store.state
        err = lambda p: (200, self._signed(K, nonce, ctr, "ERR", p), None)  # noqa: E731
        after = None

        def uint(a, max_v):
            return int(a) if re.fullmatch(r"\d{1,5}", a) and int(a) <= max_v else None

        if op in ("AUTH", "GET"):
            pass
        elif op == "LIM":
            v = uint(arg, MAX_LIMIT)
            if v is None:
                return err("RANGE")
            st["limit"] = v
            after = self._save_and_redraw
        elif op == "BRI":
            v = uint(arg, 255)
            if v is None or v < 5:
                return err("RANGE")
            st["bri"] = v
            after = self._save_and_redraw
        elif op == "COL":
            d, _, t = arg.partition(",")
            n = len(render.COLOR_PRESETS) - 1
            d, t = uint(d, n), uint(t, n)
            if d is None or t is None:
                return err("RANGE")
            st["dcol"], st["tcol"] = d, t
            after = self._save_and_redraw
        elif op == "NAME":
            if not re.fullmatch(r"[A-Za-z0-9_-]{1,20}", arg):
                return err("NAME")
            st["name"] = arg
            after = self.store.save
        elif op == "KEY":
            new_key = security.decrypt_new_key(K, nonce, ctr, arg)
            if new_key is None:
                return err("KEY")
            reply = self._signed(K, nonce, ctr, "OK", "KEY")  # signed with the OLD key
            st["key"] = new_key
            self.store.save()
            self.K = security.derive_key(self.cfg.app_secret, new_key)
            self.sessions.clear()  # new key from the next session
            log.info("Device key changed from the app")
            return 200, reply, None
        elif op == "MODE":
            if arg not in MODES:
                return err("RANGE")
            st["mode"] = arg
            after = self._apply_mode
        elif op == "RST":
            after = self._restart
        else:
            return err("UNKNOWN")
        return 200, self._signed(K, nonce, ctr, "OK", self.state_string()), after

    def _save_and_redraw(self):
        with self.lock:
            self.show_key_until = 0
            self.store.save()
        self.redraw()
        log.info("Speed limit = %s", self.store.state["limit"])

    def _apply_mode(self):
        with self.lock:
            self.store.save()
            mode = self.store.state["mode"]
        log.info("Mode = %s", mode)
        if mode == "video":
            self.player.start()
        else:
            self.player.stop()
            self.redraw()

    def _restart(self):
        log.info("Restarting (requested from the app)")
        time.sleep(1.5)
        os._exit(0)  # systemd (Restart=always) starts us again


def make_handler(app: SpeedDisplay):
    class Handler(BaseHTTPRequestHandler):
        server_version = "SpeedDisplay"
        sys_version = ""

        def log_message(self, fmt, *args):
            log.debug("%s %s", self.client_address[0], fmt % args)

        def _send(self, code, body, ctype="text/plain; charset=utf-8"):
            data = body if isinstance(body, bytes) else body.encode()
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            if self.path == "/api/info":
                self._send(200, json.dumps(app.info()), "application/json")
            elif self.path.startswith(("/preview.png", "/preview-chain.png")):
                png = app.panel.preview_png(chain=self.path.startswith("/preview-chain"))
                self._send(200, png, "image/png") if png else self._send(503, "no frame yet")
            elif self.path == "/":
                self._send(200, '<meta name="viewport" content="width=device-width">'
                                '<meta http-equiv="refresh" content="3">'
                                '<body style="background:#000;margin:0;text-align:center">'
                                '<img src="/preview.png" style="image-rendering:pixelated;height:95vh">',
                           "text/html")
            else:
                self._send(404, "not found")

        def do_POST(self):
            ip = self.client_address[0]
            length = int(self.headers.get("Content-Length") or 0)
            if length > 512:
                self._send(413, "too large")
                return
            body = self.rfile.read(length).decode("utf-8", "replace") if length else ""
            if self.path == "/api/session":
                res = app.open_session(ip)
                if res is None:
                    self._send(429, json.dumps({"error": "LOCKED"}), "application/json")
                else:
                    self._send(200, json.dumps(res), "application/json")
            elif self.path == "/api/cmd":
                code, text, after = app.handle_command(ip, self.headers.get("X-Session", ""), body.strip())
                self._send(code, text)
                if after:
                    threading.Thread(target=after, daemon=True).start()
            else:
                self._send(404, "not found")

    return Handler


def run(cfg):
    app = SpeedDisplay(cfg)
    srv = ThreadingHTTPServer((cfg.bind, cfg.port), make_handler(app))
    srv.daemon_threads = True
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    log.info("%s (%s) listening on %s:%d", app.info()["name"], app.mac, cfg.bind, cfg.port)
    try:
        while True:
            time.sleep(1)
            app.tick()
    except KeyboardInterrupt:
        pass
    finally:
        srv.shutdown()
        app.player.stop()
        app.panel.close()


def new_key(cfg):
    """`python -m speeddisplay newkey`: same as holding BOOT on the ESP32 (service must be stopped)."""
    store = Store(cfg.state_path, cfg.name, cfg.defaults)
    store.state["key"] = security.generate_device_key()
    store.save()
    print("NEW DEVICE KEY:", store.state["key"])
