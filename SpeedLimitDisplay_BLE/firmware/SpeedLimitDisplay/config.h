#pragma once

//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
// PER-DEVICE SETTING - change this for every display before flashing.
//   Display 1 -> "SpeedDisplay1", Display 2 -> "SpeedDisplay2", ...
// (Max 20 characters: A-Z a-z 0-9 _ -). It can also be renamed later from the app.
//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
#define DEFAULT_DEVICE_NAME "SpeedDisplay1"

#define FW_VERSION "1.0.0"

//----------------------------------------Shared with the mobile app (mobile-app/src/config.js).
// These MUST be identical in the firmware and in the app.
// APP_SECRET lives in secrets.h (not in git). Copy secrets.example.h -> secrets.h
// and put your own random value there (same value in the app's src/secrets.js).
#include "secrets.h"

#define SERVICE_UUID  "c6c4d30e-41ad-47d9-8521-0e902c3c6421"
#define CHR_INFO_UUID "725f07fd-510e-4008-ad3a-dc07de78fac4"  // read  : {"name","mac","fw"}
#define CHR_NONCE_UUID "3b994d37-11de-4192-8eac-5dbf22085757" // read  : random challenge, new on every connection
#define CHR_CMD_UUID  "05ced542-4bfb-4d77-acaf-bba8b755cd47"  // write : signed command
#define CHR_RESP_UUID "de36aebf-2fa0-42a5-95e8-d61b16ffdb5c"  // read  : signed response to the last command
//----------------------------------------

//----------------------------------------Panels.
// 3 x P10 RGB 32x16. Data always goes through the ribbon cables
// (ESP32 -> Panel1 IN, Panel1 OUT -> Panel2 IN, Panel2 OUT -> Panel3 IN);
// how the panels are MOUNTED is chosen here:
//
//   LAYOUT_VERTICAL 1  (panels one below the other, 32 x 48)
//     +--------+
//     | SPEED  |  Panel 1 (top)
//     | LIMIT  |
//     +--------+
//     |        |  Panel 2
//     |   45   |       digits 32 x 32
//     |        |  Panel 3 (bottom)
//     +--------+
//
//   LAYOUT_VERTICAL 0  (panels side by side, 96 x 16)
//     | SPEED/LIMIT |      45       |
#define LAYOUT_VERTICAL 1

#define PANEL_W     32
#define PANEL_H     16
#define PANEL_COUNT 3

// If "SPEED LIMIT" appears on the LAST panel (bottom / right) instead of the
// first one, set this to 1 (it reverses the order of the panels in software).
#define PANEL_ORDER_REVERSED 0

// Panels that are mounted upside down (rotated 180 deg) relative to the others.
// Bit n = n-th panel in the data chain as PxMatrix sees it (x = n*32 .. n*32+31).
// Current build: the middle panel is mounted the other way round (arrow on PCB
// points the opposite way), so bit 1 is set.
#define PANEL_FLIP_MASK 0b010
//----------------------------------------

//----------------------------------------Pins for LED MATRIX (same as the tested sketch).
#define P_LAT 5
#define P_A   19
#define P_B   23
#define P_C   18
#define P_OE  4
//----------------------------------------

// BOOT button on the ESP32 DEVKIT V1. Hold it for 5 seconds (while running) to
// generate a NEW device key; the new key is shown on the display for 60 seconds.
#define BOOT_BTN_PIN          0
#define KEY_RESET_HOLD_MS     5000
#define KEY_SHOW_MS           60000UL

//----------------------------------------Security tuning.
#define AUTH_TIMEOUT_MS       15000UL   // Not authenticated within this time -> disconnected.
#define IDLE_TIMEOUT_MS       120000UL  // No valid command for this time -> disconnected.
#define MAX_AUTH_FAILS        3         // Wrong signatures before lock-out.
#define LOCKOUT_MS            30000UL   // Lock-out time, all connections are refused.
//----------------------------------------

//----------------------------------------Defaults (used only on first boot).
#define DEFAULT_LIMIT       40
#define MAX_LIMIT           99  // speed limit is always 0-99 (2 digits)
#define DEFAULT_BRIGHTNESS  125
#define DEFAULT_DIGIT_COLOR 0   // index into COLOR_PRESETS (0 = red)
#define DEFAULT_TEXT_COLOR  3   // index into COLOR_PRESETS (3 = green)

// Speed-limit sign look (vertical layout): a round ring around the number on tiles 2+3.
#define SPEED_RING          1   // 0 = digits only, 1 = ring + digits
#define RING_COLOR_R        0
#define RING_COLOR_G        70
#define RING_COLOR_B        255
#define RING_THICKNESS      2   // px
#define DIGIT_STROKE        3   // px, thickness of the digit lines
#define DIGIT_W             10  // px per digit
#define DIGIT_H             18  // px
#define DIGIT_GAP           2   // px between the two digits

// Bump this to re-apply DEFAULT_TEXT_COLOR once on devices that already have
// settings saved (the device key and everything else are kept).
#define SETTINGS_VERSION    2
//----------------------------------------
