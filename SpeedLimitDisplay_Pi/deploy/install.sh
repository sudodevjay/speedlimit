#!/bin/bash
# Installs the speed limit display service on a Raspberry Pi (Pi OS Bookworm / Trixie, 64-bit).
# Run from the SpeedLimitDisplay_Pi folder:   sudo ./deploy/install.sh
#
# Everything goes into its own places and nothing else on the Pi is changed:
#   /opt/speeddisplay   code + Python venv (with the rpi-rgb-led-matrix binding)
#   /etc/speeddisplay   config.ini (kept if it already exists)
#   /var/lib/speeddisplay  settings.json (speed limit, colours, device key)
#   speeddisplay.service
set -euo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)"
LIB_DIR="${LIB_DIR:-$SRC/rpi-rgb-led-matrix}"
LIB_URL="https://github.com/hzeller/rpi-rgb-led-matrix.git"
LIB_COMMIT="${LIB_COMMIT:-51d3231}"   # version this was tested with

[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }

echo "== packages"
apt-get install -y --no-install-recommends python3-venv python3-dev git g++ make ffmpeg >/dev/null

echo "== rpi-rgb-led-matrix"
if [ ! -d "$LIB_DIR/.git" ]; then
  git clone -q "$LIB_URL" "$LIB_DIR"
fi
git -C "$LIB_DIR" fetch -q --depth 50 origin || true
git -C "$LIB_DIR" checkout -q "$LIB_COMMIT" 2>/dev/null || echo "   (commit $LIB_COMMIT not found, using current checkout)"

# Our own GPIO mapping "speeddisplay": avoids GPIO 27 / 10 / 18 (relays, LED strip, audio PWM),
# 14 / 15 (UART), 2 / 3 (I2C) and the SPI0 pins 7-11.
python3 - "$LIB_DIR/lib/hardware-mapping.c" <<'EOF'
import sys
p = sys.argv[1]
s = open(p).read()
if '"speeddisplay"' not in s:
    anchor = "struct HardwareMapping matrix_hardware_mappings[] = {"
    assert anchor in s, "hardware-mapping.c layout changed"
    entry = '''
  /* Speed limit display: keeps GPIO 2,3,7-11,14,15,18,27 free. */
  {
    .name          = "speeddisplay",
    .output_enable = GPIO_BIT(4),
    .clock         = GPIO_BIT(17),
    .strobe        = GPIO_BIT(21),
    .a             = GPIO_BIT(22),
    .b             = GPIO_BIT(26),
    .c             = GPIO_BIT(25),
    .d             = GPIO_BIT(20),
    .e             = GPIO_BIT(24),
    .p0_r1         = GPIO_BIT(5),
    .p0_g1         = GPIO_BIT(13),
    .p0_b1         = GPIO_BIT(6),
    .p0_r2         = GPIO_BIT(12),
    .p0_g2         = GPIO_BIT(16),
    .p0_b2         = GPIO_BIT(23),
  },
'''
    s = s.replace(anchor, anchor + entry, 1)
    open(p, "w").write(s)
    print("   added GPIO mapping 'speeddisplay'")
else:
    print("   GPIO mapping 'speeddisplay' already there")
EOF

echo "== venv + binding (takes a few minutes the first time)"
mkdir -p /opt/speeddisplay
[ -x /opt/speeddisplay/venv/bin/python ] || python3 -m venv /opt/speeddisplay/venv
if [ -n "${REBUILD_LIB:-}" ] || ! /opt/speeddisplay/venv/bin/python -c "import rgbmatrix" 2>/dev/null; then
  /opt/speeddisplay/venv/bin/pip install -q --upgrade pip
  /opt/speeddisplay/venv/bin/pip install -q "$LIB_DIR"
else
  echo "   rgbmatrix already installed (REBUILD_LIB=1 to rebuild)"
fi

echo "== code"
rm -rf /opt/speeddisplay/speeddisplay
cp -r "$SRC/speeddisplay" /opt/speeddisplay/
find /opt/speeddisplay/speeddisplay -name __pycache__ -prune -exec rm -rf {} +

echo "== config"
mkdir -p /etc/speeddisplay /var/lib/speeddisplay
chmod 700 /var/lib/speeddisplay
if [ ! -f /etc/speeddisplay/config.ini ]; then
  install -m 600 "$SRC/deploy/config.ini" /etc/speeddisplay/config.ini
  echo "   /etc/speeddisplay/config.ini created"
else
  echo "   /etc/speeddisplay/config.ini kept"
fi

echo "== service"
install -m 644 "$SRC/deploy/speeddisplay.service" /etc/systemd/system/speeddisplay.service
systemctl daemon-reload
systemctl enable -q speeddisplay.service
systemctl restart speeddisplay.service
sleep 3
systemctl --no-pager --lines=15 status speeddisplay.service || true
echo
echo "Done. Preview: http://$(hostname -I | awk '{print $1}'):8099/"
