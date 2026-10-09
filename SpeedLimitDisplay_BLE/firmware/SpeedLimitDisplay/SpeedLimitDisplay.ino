//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
// Speed Limit Display - ESP32 + 3 x P10 RGB 32x16 (HUB75, 1/8 scan) + secure BLE
//
//  LAYOUT_VERTICAL 1 (default)        LAYOUT_VERTICAL 0
//  +--------+                         +--------+-----------------+
//  | SPEED  |  Tile 1                 | SPEED  |                 |
//  | LIMIT  |                         | LIMIT  |       45        |
//  +--------+                         +--------+-----------------+
//  |        |  Tile 2                   Tile 1    Tile 2 + Tile 3
//  |   45   |       digits 32x32
//  |        |  Tile 3                 Digits use a "double pixel" (2 px stroke) font.
//  +--------+
//
// - The speed limit is set from the mobile app over BLE and saved in NVS
//   (Preferences), so the same value is shown again after a power cut.
// - Only the app that knows APP_SECRET *and* this display's own device key can
//   change anything (HMAC-SHA256 challenge/response, see README.md).
//
// Software (same as the tested sketch) :
// - Arduino core for the ESP32 V2.0.16
// - Adafruit GFX Library V1.11.9, Adafruit BusIO V1.16.1
// - PxMatrix Library V1.8.2
// - NimBLE-Arduino by h2zero V1.4.x  (NOT 2.x - its API is different)
//
// Arduino IDE: Tools > Partition Scheme > "Default 4MB with spiffs" (or "Huge APP" if the sketch is too big).
//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

// Same SPI speed as the tested sketch (20 MHz gave ghosting on this panel).
#define PxMATRIX_SPI_FREQUENCY 10000000

//----------------------------------------Including Libraries.
#include <PxMatrix.h>
#include <Adafruit_GFX.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include "config.h"
#include "BigDigits.h"
#include "Security.h"
//----------------------------------------

// Physical chain as PxMatrix sees it (always one long row of panels).
#define MATRIX_W (PANEL_W * PANEL_COUNT)  // 96
#define MATRIX_H PANEL_H                  // 16

// Logical drawing area (how the panels are mounted).
#if LAYOUT_VERTICAL
#define CANVAS_W PANEL_W                  // 32
#define CANVAS_H (PANEL_H * PANEL_COUNT)  // 48
#else
#define CANVAS_W MATRIX_W                 // 96
#define CANVAS_H MATRIX_H                 // 16
#endif

//----------------------------------------Display objects.
hw_timer_t *timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// The larger this number, the brighter the display. If too large the ESP will crash.
uint8_t display_draw_time = 30;

PxMATRIX display(MATRIX_W, MATRIX_H, P_LAT, P_OE, P_A, P_B, P_C);

// Everything is drawn here first, then copied to the panels in one go.
GFXcanvas16 canvas(CANVAS_W, CANVAS_H);
//----------------------------------------

//----------------------------------------Colour presets (index is what the app sends).
// 0 Red, 1 White, 2 Yellow, 3 Green, 4 Amber, 5 Cyan
const uint8_t COLOR_PRESETS[][3] = {
  {255, 0, 0}, {255, 255, 255}, {255, 255, 0}, {0, 255, 0}, {255, 110, 0}, {0, 255, 255}
};
#define COLOR_PRESET_COUNT (sizeof(COLOR_PRESETS) / sizeof(COLOR_PRESETS[0]))
//----------------------------------------

//----------------------------------------Settings (persisted in NVS / Preferences).
struct Settings {
  uint16_t limit;
  uint8_t brightness;
  uint8_t digitColor;
  uint8_t textColor;
  char name[21];
  char key[DEVICE_KEY_LEN_MAX + 1];
};

Settings settings;
Preferences prefs;
uint8_t sessionKey[32];  // K = HMAC(APP_SECRET, "SDK1|" + settings.key)

// settings / sessionKey are changed from the BLE task and read from loop().
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;

#define DIRTY_DISPLAY 0x01
#define DIRTY_SAVE    0x02
#define DIRTY_RESTART 0x04
volatile uint8_t dirtyFlags = 0;
//----------------------------------------

//----------------------------------------BLE session state.
struct Session {
  volatile bool connected;
  volatile bool authed;
  volatile uint16_t connHandle;
  volatile uint32_t connectedAt;
  volatile uint32_t lastActivity;
  volatile uint32_t lastCounter;
  volatile uint8_t fails;
  char nonce[33];
};

Session session;
volatile uint32_t lockoutUntil = 0;
volatile bool lockoutActive = false;
volatile bool requestDisconnect = false;

NimBLEServer *pServer = nullptr;
NimBLECharacteristic *chrInfo = nullptr;
NimBLECharacteristic *chrNonce = nullptr;
NimBLECharacteristic *chrCmd = nullptr;
NimBLECharacteristic *chrResp = nullptr;
//----------------------------------------

// Key overlay (first boot / after BOOT-button key reset).
uint32_t showKeyUntil = 0;
bool showingKey = false;
uint32_t bootBtnPressedAt = 0;

//________________________________________________________________________________ Display refresh (timer ISR)
void IRAM_ATTR display_updater() {
  portENTER_CRITICAL_ISR(&timerMux);
  display.display(display_draw_time);
  portEXIT_CRITICAL_ISR(&timerMux);
}

void display_timer_start() {
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &display_updater, true);
  timerAlarmWrite(timer, 1500, true);
  timerAlarmEnable(timer);
}

// The refresh ISR must be stopped while writing to flash (NVS), otherwise the ESP32 can crash.
void display_timer_pause(bool pause) {
  if (!timer) return;
  if (pause) timerAlarmDisable(timer);
  else timerAlarmEnable(timer);
}
//________________________________________________________________________________

//________________________________________________________________________________ Rendering
uint16_t presetColor(uint8_t index) {
  if (index >= COLOR_PRESET_COUNT) index = 0;
  return display.color565(COLOR_PRESETS[index][0], COLOR_PRESETS[index][1], COLOR_PRESETS[index][2]);
}

// Logical (x, y) on the canvas -> physical pixel on the panel chain.
void drawLogicalPixel(int16_t x, int16_t y, uint16_t color) {
#if LAYOUT_VERTICAL
  int16_t panel = y / PANEL_H, lx = x, ly = y % PANEL_H;
#else
  int16_t panel = x / PANEL_W, lx = x % PANEL_W, ly = y;
#endif
#if PANEL_ORDER_REVERSED
  panel = PANEL_COUNT - 1 - panel;
#endif
  if ((PANEL_FLIP_MASK >> panel) & 1) {
    lx = PANEL_W - 1 - lx;
    ly = PANEL_H - 1 - ly;
  }
  display.drawPixel(panel * PANEL_W + lx, ly, color);
}

void pushCanvas() {
  uint16_t *buf = canvas.getBuffer();
  for (int16_t y = 0; y < CANVAS_H; y++) {
    for (int16_t x = 0; x < CANVAS_W; x++) {
      drawLogicalPixel(x, y, buf[y * CANVAS_W + x]);
    }
  }
}

void drawCenteredText(const char *text, int16_t areaX, int16_t areaW, int16_t y, uint16_t color) {
  int16_t x1, y1;
  uint16_t w, h;
  canvas.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  canvas.setTextColor(color);
  canvas.setCursor(areaX + (areaW - (int16_t)w) / 2, y);
  canvas.print(text);
}

// Ring touching the edges of the size x size square at (x, y), RING_THICKNESS px wide.
void drawRing(int16_t x, int16_t y, int16_t size, uint16_t color) {
  float c = (size - 1) / 2.0f, rOut = size / 2.0f, rIn = rOut - RING_THICKNESS;
  for (int16_t j = 0; j < size; j++) {
    for (int16_t i = 0; i < size; i++) {
      float d2 = (i - c) * (i - c) + (j - c) * (j - c);
      if (d2 <= rOut * rOut && d2 >= rIn * rIn) canvas.drawPixel(x + i, y + j, color);
    }
  }
}

// Normal screen: "SPEED"/"LIMIT" on tile 1 (top half / bottom half), digits on tiles 2+3.
void renderSpeedLimit(const Settings &s) {
  canvas.fillScreen(0);
  canvas.setTextWrap(false);
  canvas.setTextSize(1);

  uint16_t textColor = presetColor(s.textColor);
  drawCenteredText("SPEED", 0, PANEL_W, 0, textColor);  // rows 0..6
  drawCenteredText("LIMIT", 0, PANEL_W, 9, textColor);  // rows 9..15

#if LAYOUT_VERTICAL && SPEED_RING
  // Tiles 2+3 = 32x32 below tile 1: round ring like a road sign, digits inside it.
  drawRing(0, PANEL_H, PANEL_W, display.color565(RING_COLOR_R, RING_COLOR_G, RING_COLOR_B));
  // Bold 10x18 digits with 3 px strokes, centred in the ring (corners stay inside it).
  drawBoldNumber(canvas, 0, PANEL_H, PANEL_W, 2 * PANEL_H, s.limit, DIGIT_W, DIGIT_H, DIGIT_GAP, DIGIT_STROKE,
                 presetColor(s.digitColor));
#elif LAYOUT_VERTICAL
  // Tiles 2+3 = 32x32 below tile 1; 1 px margin top and bottom -> 30 px tall digits.
  drawBigNumber(canvas, 0, PANEL_H + 1, PANEL_W, 2 * PANEL_H - 2, s.limit, presetColor(s.digitColor));
#else
  // Tiles 2+3 = 64x16 right of tile 1.
  drawBigNumber(canvas, PANEL_W, 0, CANVAS_W - PANEL_W, PANEL_H, s.limit, presetColor(s.digitColor));
#endif
  pushCanvas();
}

// Shows the device key so the owner can add the display in the app.
void renderDeviceKey(const Settings &s) {
  canvas.fillScreen(0);
  canvas.setTextWrap(false);
  canvas.setTextSize(1);
  uint16_t yellow = display.color565(255, 255, 0);
  drawCenteredText("KEY", 0, PANEL_W, 4, yellow);
  uint16_t white = display.color565(255, 255, 255);
#if LAYOUT_VERTICAL
  // 32 px is only 5 characters wide -> key in lines of 4 characters on tiles 2+3.
  char line[5];
  for (uint8_t i = 0; i < 3 && s.key[i * 4]; i++) {
    strlcpy(line, s.key + i * 4, sizeof(line));
    drawCenteredText(line, 0, PANEL_W, PANEL_H + 2 + i * 10, white);
  }
#else
  drawCenteredText(s.key, PANEL_W, CANVAS_W - PANEL_W, 4, white);
#endif
  pushCanvas();
}
//________________________________________________________________________________

//________________________________________________________________________________ Settings (NVS)
void loadSettings() {
  prefs.begin("speeddisp", true);
  settings.limit = prefs.getUShort("lim", DEFAULT_LIMIT);
  settings.brightness = prefs.getUChar("bri", DEFAULT_BRIGHTNESS);
  settings.digitColor = prefs.getUChar("dcol", DEFAULT_DIGIT_COLOR);
  settings.textColor = prefs.getUChar("tcol", DEFAULT_TEXT_COLOR);

  // If this firmware was built with a different DEFAULT_DEVICE_NAME than the one
  // stored, the new build name wins (so re-flashing as "SpeedDisplay2" works).
  String buildName = prefs.getString("bname", "");
  String name = prefs.getString("name", DEFAULT_DEVICE_NAME);
  if (buildName != DEFAULT_DEVICE_NAME) name = DEFAULT_DEVICE_NAME;
  strlcpy(settings.name, name.c_str(), sizeof(settings.name));

  String key = prefs.getString("key", "");
  strlcpy(settings.key, key.c_str(), sizeof(settings.key));
  bool outdated = prefs.getUChar("ver", 0) < SETTINGS_VERSION;
  prefs.end();

  if (outdated) {
    settings.textColor = DEFAULT_TEXT_COLOR;
    prefs.begin("speeddisp", false);
    prefs.putUChar("tcol", settings.textColor);
    prefs.putUChar("ver", SETTINGS_VERSION);
    prefs.end();
  }

  if (settings.limit > MAX_LIMIT) settings.limit = DEFAULT_LIMIT;
  if (settings.digitColor >= COLOR_PRESET_COUNT) settings.digitColor = DEFAULT_DIGIT_COLOR;
  if (settings.textColor >= COLOR_PRESET_COUNT) settings.textColor = DEFAULT_TEXT_COLOR;
}

void saveSettings(const Settings &s) {
  display_timer_pause(true);
  prefs.begin("speeddisp", false);
  prefs.putUShort("lim", s.limit);
  prefs.putUChar("bri", s.brightness);
  prefs.putUChar("dcol", s.digitColor);
  prefs.putUChar("tcol", s.textColor);
  prefs.putString("name", s.name);
  prefs.putString("bname", DEFAULT_DEVICE_NAME);
  prefs.putString("key", s.key);
  prefs.end();
  display_timer_pause(false);
  Serial.printf("[NVS] Saved: limit=%u bri=%u dcol=%u tcol=%u name=%s\n",
                s.limit, s.brightness, s.digitColor, s.textColor, s.name);
}

// Creates a new random device key, saves it and shows it on the panel.
void newDeviceKey() {
  char key[DEVICE_KEY_LEN_GEN + 1];
  sec::generateDeviceKey(key);
  uint8_t k[32];
  sec::deriveKey(key, k);

  portENTER_CRITICAL(&stateMux);
  strlcpy(settings.key, key, sizeof(settings.key));
  memcpy(sessionKey, k, 32);
  portEXIT_CRITICAL(&stateMux);

  saveSettings(settings);
  Serial.println("==============================================");
  Serial.printf("  NEW DEVICE KEY : %s\n", key);
  Serial.println("  Write it down / stick it inside the enclosure.");
  Serial.println("==============================================");
  showKeyUntil = millis() + KEY_SHOW_MS;
  showingKey = true;
  renderDeviceKey(settings);
}
//________________________________________________________________________________

//________________________________________________________________________________ BLE protocol
// Request : "<counter>|<command>|<mac>"
//           mac = first 32 hex of HMAC(K, nonce + "|" + counter + "|" + command)
// Response: "<counter>|<OK|ERR>|<payload>|<mac>"
//           mac = first 32 hex of HMAC(K, nonce + "|R|" + counter + "|" + status + "|" + payload)

void setResp(const std::string &s) {
  chrResp->setValue((const uint8_t *)s.data(), s.size());
}

void replySigned(const uint8_t key[32], const std::string &ctr, const char *status, const std::string &payload) {
  std::string msg = std::string(session.nonce) + "|R|" + ctr + "|" + status + "|" + payload;
  char mac[33];
  sec::hmacHex32(key, msg, mac);
  setResp(ctr + "|" + status + "|" + payload + "|" + mac);
}

std::string stateString(const Settings &s) {
  char buf[96];
  snprintf(buf, sizeof(buf), "lim=%u;bri=%u;dcol=%u;tcol=%u;name=%s",
           s.limit, s.brightness, s.digitColor, s.textColor, s.name);
  return std::string(buf);
}

void registerFailure() {
  session.fails++;
  Serial.printf("[SEC] Bad signature (%u/%u)\n", session.fails, MAX_AUTH_FAILS);
  if (session.fails >= MAX_AUTH_FAILS) {
    lockoutUntil = millis() + LOCKOUT_MS;
    lockoutActive = true;
    requestDisconnect = true;
    Serial.println("[SEC] Too many failures -> lock-out");
  }
}

bool isValidName(const std::string &n) {
  if (n.empty() || n.size() > 20) return false;
  for (char c : n) {
    if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
  }
  return true;
}

bool parseUInt(const std::string &s, uint32_t maxVal, uint32_t &out) {
  if (s.empty() || s.size() > 5) return false;
  uint32_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
  }
  if (v > maxVal) return false;
  out = v;
  return true;
}

// Executes an already authenticated command. Runs in the NimBLE task:
// only changes `settings` + dirty flags; drawing and NVS writes happen in loop().
void executeCommand(const uint8_t key[32], const std::string &ctr, const std::string &cmd) {
  size_t colon = cmd.find(':');
  std::string op = cmd.substr(0, colon);
  std::string arg = (colon == std::string::npos) ? "" : cmd.substr(colon + 1);
  uint32_t v;

  if (op == "AUTH" || op == "GET") {
    // nothing to change
  } else if (op == "LIM") {
    if (!parseUInt(arg, MAX_LIMIT, v)) { replySigned(key, ctr, "ERR", "RANGE"); return; }
    portENTER_CRITICAL(&stateMux);
    settings.limit = v;
    dirtyFlags |= DIRTY_DISPLAY | DIRTY_SAVE;
    portEXIT_CRITICAL(&stateMux);
  } else if (op == "BRI") {
    if (!parseUInt(arg, 255, v) || v < 5) { replySigned(key, ctr, "ERR", "RANGE"); return; }
    portENTER_CRITICAL(&stateMux);
    settings.brightness = v;
    dirtyFlags |= DIRTY_DISPLAY | DIRTY_SAVE;
    portEXIT_CRITICAL(&stateMux);
  } else if (op == "COL") {
    size_t comma = arg.find(',');
    uint32_t d, t;
    if (comma == std::string::npos ||
        !parseUInt(arg.substr(0, comma), COLOR_PRESET_COUNT - 1, d) ||
        !parseUInt(arg.substr(comma + 1), COLOR_PRESET_COUNT - 1, t)) {
      replySigned(key, ctr, "ERR", "RANGE");
      return;
    }
    portENTER_CRITICAL(&stateMux);
    settings.digitColor = d;
    settings.textColor = t;
    dirtyFlags |= DIRTY_DISPLAY | DIRTY_SAVE;
    portEXIT_CRITICAL(&stateMux);
  } else if (op == "NAME") {
    if (!isValidName(arg)) { replySigned(key, ctr, "ERR", "NAME"); return; }
    portENTER_CRITICAL(&stateMux);
    strlcpy(settings.name, arg.c_str(), sizeof(settings.name));
    dirtyFlags |= DIRTY_SAVE | DIRTY_RESTART;  // BLE name is applied after restart
    portEXIT_CRITICAL(&stateMux);
  } else if (op == "KEY") {
    // New key is encrypted: hex(newKey XOR HMAC(K, nonce|KEY|counter)).
    uint8_t enc[DEVICE_KEY_LEN_MAX];
    size_t n = sec::fromHex(arg.c_str(), arg.size(), enc, sizeof(enc));
    if (n == 0) { replySigned(key, ctr, "ERR", "KEY"); return; }
    uint8_t ks[32];
    std::string ksMsg = std::string(session.nonce) + "|KEY|" + ctr;
    sec::hmacSha256(key, 32, (const uint8_t *)ksMsg.data(), ksMsg.size(), ks);
    char newKey[DEVICE_KEY_LEN_MAX + 1];
    for (size_t i = 0; i < n; i++) newKey[i] = (char)(enc[i] ^ ks[i]);
    if (!sec::normalizeDeviceKey(newKey, n)) { replySigned(key, ctr, "ERR", "KEY"); return; }

    // Reply is signed with the OLD key; the new key is used from the next connection.
    replySigned(key, ctr, "OK", "KEY");
    uint8_t k[32];
    sec::deriveKey(newKey, k);
    portENTER_CRITICAL(&stateMux);
    strlcpy(settings.key, newKey, sizeof(settings.key));
    memcpy(sessionKey, k, 32);
    dirtyFlags |= DIRTY_SAVE;
    portEXIT_CRITICAL(&stateMux);
    requestDisconnect = true;
    return;
  } else if (op == "RST") {
    portENTER_CRITICAL(&stateMux);
    dirtyFlags |= DIRTY_RESTART;
    portEXIT_CRITICAL(&stateMux);
  } else {
    replySigned(key, ctr, "ERR", "UNKNOWN");
    return;
  }

  Settings snap;
  portENTER_CRITICAL(&stateMux);
  snap = settings;
  portEXIT_CRITICAL(&stateMux);
  replySigned(key, ctr, "OK", stateString(snap));
}

void handleCommand(const std::string &raw) {
  size_t p1 = raw.find('|');
  size_t p2 = raw.rfind('|');
  if (raw.size() > 200 || p1 == std::string::npos || p2 == p1) {
    setResp("0|ERR|FORMAT|");
    registerFailure();
    return;
  }
  std::string ctr = raw.substr(0, p1);
  std::string cmd = raw.substr(p1 + 1, p2 - p1 - 1);
  std::string mac = raw.substr(p2 + 1);

  uint8_t key[32];
  portENTER_CRITICAL(&stateMux);
  memcpy(key, sessionKey, 32);
  portEXIT_CRITICAL(&stateMux);

  char expected[33];
  sec::hmacHex32(key, std::string(session.nonce) + "|" + ctr + "|" + cmd, expected);
  if (mac.size() != 32 || !sec::ctEqual(mac.c_str(), expected, 32)) {
    setResp("0|ERR|AUTH|");  // not signed: the sender does not have the key anyway
    registerFailure();
    return;
  }

  uint32_t counter;
  if (!parseUInt(ctr, 99999, counter) || counter <= session.lastCounter) {
    replySigned(key, ctr, "ERR", "REPLAY");
    return;
  }
  session.lastCounter = counter;
  session.fails = 0;
  session.lastActivity = millis();
  if (!session.authed) Serial.println("[SEC] Client authenticated");
  session.authed = true;

  executeCommand(key, ctr, cmd);
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *s, ble_gap_conn_desc *desc) override {
    if (lockoutActive && (int32_t)(lockoutUntil - millis()) > 0) {
      Serial.println("[SEC] Connection refused (lock-out)");
      s->disconnect(desc->conn_handle);
      return;
    }
    lockoutActive = false;

    // Fresh random challenge for every connection -> old messages cannot be replayed.
    uint8_t rnd[16];
    esp_fill_random(rnd, sizeof(rnd));
    sec::toHex(rnd, sizeof(rnd), session.nonce);
    chrNonce->setValue((const uint8_t *)session.nonce, 32);
    setResp("");

    session.connHandle = desc->conn_handle;
    session.connectedAt = millis();
    session.lastActivity = millis();
    session.lastCounter = 0;
    session.fails = 0;
    session.authed = false;
    session.connected = true;
    Serial.printf("[BLE] Connected: %s\n", NimBLEAddress(desc->peer_ota_addr).toString().c_str());
  }

  void onDisconnect(NimBLEServer *s, ble_gap_conn_desc *desc) override {
    if (desc->conn_handle == session.connHandle) {
      session.connected = false;
      session.authed = false;
      memset(session.nonce, 0, sizeof(session.nonce));
      chrNonce->setValue((const uint8_t *)"", 0);
      setResp("");
    }
    Serial.println("[BLE] Disconnected");
  }
};

class CmdCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, ble_gap_conn_desc *desc) override {
    if (!session.connected || desc->conn_handle != session.connHandle) return;
    std::string v = c->getValue();
    handleCommand(v);
  }
};

void setupBLE() {
  NimBLEDevice::init(settings.name);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setMTU(185);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  pServer->advertiseOnDisconnect(true);

  NimBLEService *svc = pServer->createService(SERVICE_UUID);
  chrInfo = svc->createCharacteristic(CHR_INFO_UUID, NIMBLE_PROPERTY::READ);
  chrNonce = svc->createCharacteristic(CHR_NONCE_UUID, NIMBLE_PROPERTY::READ);
  chrCmd = svc->createCharacteristic(CHR_CMD_UUID, NIMBLE_PROPERTY::WRITE);
  chrResp = svc->createCharacteristic(CHR_RESP_UUID, NIMBLE_PROPERTY::READ);
  chrCmd->setCallbacks(new CmdCallbacks());

  String mac = String(NimBLEDevice::getAddress().toString().c_str());
  mac.toUpperCase();
  String info = String("{\"name\":\"") + settings.name + "\",\"mac\":\"" + mac + "\",\"fw\":\"" FW_VERSION "\"}";
  chrInfo->setValue((const uint8_t *)info.c_str(), info.length());
  svc->start();

  Serial.printf("[BLE] Name: %s  MAC: %s\n", settings.name, mac.c_str());
}

void startAdvertising() {
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();

  NimBLEAdvertisementData advData;
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  advData.setCompleteServices(NimBLEUUID(SERVICE_UUID));
  adv->setAdvertisementData(advData);

  // Name does not fit next to a 128-bit UUID (31 byte limit) -> scan response.
  NimBLEAdvertisementData scanData;
  scanData.setName(settings.name);
  adv->setScanResponseData(scanData);

  adv->start();
  Serial.println("[BLE] Advertising");
}
//________________________________________________________________________________

//________________________________________________________________________________ loop() helpers
void handleSessionTimers() {
  if (!session.connected) {
    requestDisconnect = false;
    return;
  }
  uint32_t now = millis();
  bool kick = requestDisconnect;
  if (!session.authed && now - session.connectedAt > AUTH_TIMEOUT_MS) {
    Serial.println("[SEC] Auth timeout");
    kick = true;
  }
  if (session.authed && now - session.lastActivity > IDLE_TIMEOUT_MS) {
    Serial.println("[BLE] Idle timeout");
    kick = true;
  }
  if (kick) {
    requestDisconnect = false;
    if (session.authed && !lockoutActive) delay(300);  // let the app read the last response
    pServer->disconnect(session.connHandle);
  }
}

void handleBootButton() {
  if (digitalRead(BOOT_BTN_PIN) == LOW) {
    if (bootBtnPressedAt == 0) bootBtnPressedAt = millis();
    if (millis() - bootBtnPressedAt > KEY_RESET_HOLD_MS) {
      Serial.println("[SEC] BOOT button held -> new device key");
      if (session.connected) pServer->disconnect(session.connHandle);
      newDeviceKey();
      while (digitalRead(BOOT_BTN_PIN) == LOW) delay(10);
      bootBtnPressedAt = 0;
    }
  } else {
    bootBtnPressedAt = 0;
  }
}
//________________________________________________________________________________

//________________________________________________________________________________ VOID SETUP()
void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.println();
  Serial.println("Speed Limit Display v" FW_VERSION);

  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);

  loadSettings();
  setupBLE();  // radio on -> esp_random() is a true RNG from here

  bool firstBoot = strlen(settings.key) == 0;
  if (!firstBoot) sec::deriveKey(settings.key, sessionKey);

  // Display initialization (same as the tested sketch).
  display.begin(8);  // 8 = 1/8 row scan panel
  delay(100);
  display_timer_start();
  delay(100);
  display.clearDisplay();
  display.setBrightness(settings.brightness);  // 0-255
  display.setTextWrap(false);
  display.setRotation(0);

  if (firstBoot) {
    newDeviceKey();  // saves to NVS and shows the key on the panel
  } else {
    Serial.printf("[SEC] Device key: %s\n", settings.key);
    renderSpeedLimit(settings);
  }

  startAdvertising();
}
//________________________________________________________________________________

//________________________________________________________________________________ VOID LOOP()
void loop() {
  handleBootButton();
  handleSessionTimers();

  uint8_t flags;
  Settings snap;
  portENTER_CRITICAL(&stateMux);
  flags = dirtyFlags;
  dirtyFlags = 0;
  snap = settings;
  portEXIT_CRITICAL(&stateMux);

  if (flags & DIRTY_SAVE) saveSettings(snap);

  if (flags & DIRTY_DISPLAY) {
    display.setBrightness(snap.brightness);
    showingKey = false;
    renderSpeedLimit(snap);
    Serial.printf("[DSP] Speed limit = %u\n", snap.limit);
  }

  if (showingKey && (int32_t)(millis() - showKeyUntil) > 0) {
    showingKey = false;
    renderSpeedLimit(snap);
  }

  if (flags & DIRTY_RESTART) {
    Serial.println("[SYS] Restarting...");
    delay(1500);  // time for the app to read the response
    ESP.restart();
  }

  delay(20);
}
//________________________________________________________________________________
