# Speed Limit Display (ESP32 + P10 RGB 32x16)

| Folder | What it is |
|---|---|
| `SpeedLimitDisplay_BLE/firmware/SpeedLimitDisplay` | ESP32 firmware: 3 x P10 RGB 32x16 (2727-8S, 1/8 scan) speed-limit sign, set over secure BLE |
| `SpeedLimitDisplay_BLE/SpeedDisplayApp` | Android/iOS app (React Native CLI) to add displays and set the speed limit |
| `SpeedLimitDisplay_BLE/README.md` | Wiring, security design, setup steps |
| `ESP32 _ ESP32 P10 RGB 32x16` | Original single-panel PxMatrix reference sketch (tested) |
| `STM32_P10_RGB_32x16` | STM32 HUB75 driver R&D |

## Firmware
Arduino core for ESP32 2.0.x, PxMatrix 1.8.2, Adafruit GFX, NimBLE-Arduino **1.4.x**.
Per-device name: `DEFAULT_DEVICE_NAME` in `config.h` (SpeedDisplay1, SpeedDisplay2, ...).

## App
```
cd SpeedLimitDisplay_BLE/SpeedDisplayApp
npm install
cd android && gradlew assembleRelease   # -> android/app/build/outputs/apk/release/app-release.apk
```
A ready APK is attached to the GitHub release.

`APP_SECRET` and the BLE UUIDs must be identical in `firmware/.../config.h` and `SpeedDisplayApp/src/config.js`.
