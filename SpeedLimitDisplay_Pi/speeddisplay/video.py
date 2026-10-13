"""Video mode: plays the videos from a USB stick on the sign (32 x 48), in a loop.

ffmpeg scales every frame to the sign size; the frames then go through the same
panel mapping (render.to_chain) as the speed-limit screen. If no USB stick / no video
is found, `on_idle` is called so the sign falls back to the speed limit.
"""

import logging
import os
import subprocess
import threading
import time

log = logging.getLogger("speeddisplay.video")

VIDEO_EXT = (".mp4", ".mkv", ".avi", ".mov", ".webm", ".m4v", ".mpg", ".mpeg", ".gif", ".wmv", ".flv")
USB_MOUNT = "/run/speeddisplay/usb"
RESCAN_SECONDS = 5
SETTLE_SECONDS = 10   # files changed more recently are still being copied (scp / USB) -> skipped for now


def _mounted_dirs():
    """Mount points of USB drives that are already mounted (desktop auto-mount), plus ours."""
    out = []
    try:
        with open("/proc/mounts") as f:
            for line in f:
                dev, mnt = line.split()[:2]
                mnt = mnt.replace("\\040", " ")
                if dev.startswith("/dev/sd") and (mnt.startswith("/media/") or mnt.startswith("/mnt/") or mnt == USB_MOUNT):
                    out.append(mnt)
    except OSError:
        pass
    return out


def _mount_usb():
    """Mounts the first unmounted USB partition read-only at USB_MOUNT (when nobody else did)."""
    if any(m.startswith(("/media/", "/mnt/", USB_MOUNT)) for m in _mounted_dirs()):
        return
    try:
        res = subprocess.run(["lsblk", "-rnpo", "NAME,TRAN,FSTYPE,MOUNTPOINT,TYPE"], capture_output=True, text=True,
                             timeout=5)
    except (OSError, subprocess.SubprocessError):
        return
    disk_is_usb = {}
    for line in res.stdout.splitlines():
        parts = (line.split(" ") + [""] * 5)[:5]
        name, tran, fstype, mountpoint, typ = parts
        if typ == "disk":
            disk_is_usb[name] = tran == "usb"
        elif typ == "part" and fstype and not mountpoint and name.startswith("/dev/sd"):
            disk = name.rstrip("0123456789")
            if disk_is_usb.get(disk):
                os.makedirs(USB_MOUNT, exist_ok=True)
                r = subprocess.run(["mount", "-o", "ro,noexec,nosuid,nodev", name, USB_MOUNT], capture_output=True,
                                   text=True, timeout=15)
                if r.returncode == 0:
                    log.info("USB %s (%s) mounted at %s", name, fstype, USB_MOUNT)
                else:
                    log.warning("Could not mount %s: %s", name, r.stderr.strip())
                return


def _unmount_if_gone():
    """Our mount stays behind when the stick is pulled out -> lazy unmount it."""
    if USB_MOUNT in _mounted_dirs():
        try:
            os.listdir(USB_MOUNT)
        except OSError:
            subprocess.run(["umount", "-l", USB_MOUNT], capture_output=True, timeout=10)
            log.info("USB removed")


def find_videos(extra_dirs=()):
    files = []
    now = time.time()
    for root_dir in list(_mounted_dirs()) + [d for d in extra_dirs if os.path.isdir(d)]:
        for dirpath, dirnames, filenames in os.walk(root_dir):
            dirnames[:] = sorted(d for d in dirnames if not d.startswith((".", "System Volume")))
            for fn in sorted(filenames):
                if fn.lower().endswith(VIDEO_EXT) and not fn.startswith("."):
                    path = os.path.join(dirpath, fn)
                    try:
                        if now - os.path.getmtime(path) < SETTLE_SECONDS:
                            continue
                    except OSError:
                        continue
                    files.append(path)
    return files


class VideoPlayer:
    def __init__(self, width, height, show_frame, on_idle, fit="fill", extra_dirs=(), fps=30):
        self.w, self.h = width, height
        self.show_frame = show_frame      # callback(rows) with rows of (r, g, b), size w x h
        self.on_idle = on_idle            # callback() when there is nothing to play
        self.fit = fit
        self.extra_dirs = extra_dirs
        self.fps = fps
        self.videos = []
        self.current = ""
        self._stop = threading.Event()
        self._thread = None
        self._proc = None

    @property
    def running(self):
        return self._thread is not None and self._thread.is_alive()

    def scan(self):
        _unmount_if_gone()
        _mount_usb()
        self.videos = find_videos(self.extra_dirs)
        return self.videos

    def start(self):
        if self.running:
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, name="video", daemon=True)
        self._thread.start()

    def stop(self):
        self._stop.set()
        p = self._proc
        if p and p.poll() is None:
            p.kill()
        if self._thread:
            self._thread.join(timeout=3)
        self._thread = None
        self.current = ""

    def _vf(self):
        w, h = self.w, self.h
        if self.fit == "fit":   # whole picture, black bars
            return f"scale={w}:{h}:force_original_aspect_ratio=decrease:flags=area,pad={w}:{h}:(ow-iw)/2:(oh-ih)/2"
        # fill the sign, cut the sides
        return f"scale={w}:{h}:force_original_aspect_ratio=increase:flags=area,crop={w}:{h}"

    def _run(self):
        idle_reported = False
        last = None
        while not self._stop.is_set():
            # Scan before every video: files copied in (USB / scp) are played from the next video on.
            videos = self.scan()
            if not videos:
                self.current = ""
                if not idle_reported:
                    log.info("Video mode: no videos (USB stick / video folders) -> showing the speed limit")
                    self.on_idle()
                    idle_reported = True
                self._stop.wait(RESCAN_SECONDS)
                continue
            idle_reported = False
            nxt = videos[(videos.index(last) + 1) % len(videos)] if last in videos else videos[0]
            self._play(nxt)
            last = nxt
        self.current = ""

    def _play(self, path):
        self.current = os.path.basename(path)
        log.info("Playing %s", path)
        frame_bytes = self.w * self.h * 3
        cmd = ["ffmpeg", "-nostdin", "-loglevel", "error", "-re", "-i", path, "-an", "-vf", self._vf(),
               "-r", str(self.fps), "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"]
        started = time.monotonic()
        frames = 0
        try:
            self._proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        except OSError as e:
            log.error("ffmpeg not available: %s", e)
            self._stop.wait(RESCAN_SECONDS)
            return
        try:
            while not self._stop.is_set():
                buf = self._proc.stdout.read(frame_bytes)
                if len(buf) < frame_bytes:
                    break
                w = self.w
                rows = [[tuple(buf[(y * w + x) * 3:(y * w + x) * 3 + 3]) for x in range(w)] for y in range(self.h)]
                self.show_frame(rows)
                frames += 1
        finally:
            if self._proc.poll() is None:
                self._proc.kill()
            err = self._proc.stderr.read().decode(errors="replace").strip()
            self._proc.wait()
            self._proc = None
        if frames == 0:
            log.warning("Could not play %s: %s", os.path.basename(path), err[-200:] or "no frames")
            self._stop.wait(1)  # broken file: do not spin
        else:
            log.info("Finished %s (%d frames, %.0fs)", os.path.basename(path), frames, time.monotonic() - started)
