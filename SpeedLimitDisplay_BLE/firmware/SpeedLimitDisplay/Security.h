#pragma once

#include <Arduino.h>
#include <string>
#include "mbedtls/md.h"
#include "config.h"

// Small crypto helpers used by the BLE protocol (see README.md -> "Security protocol").
namespace sec {

// Characters used for generated device keys (no 0/O/1/I/L to avoid confusion).
static const char KEY_ALPHABET[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
#define DEVICE_KEY_LEN_GEN 8
#define DEVICE_KEY_LEN_MIN 8
#define DEVICE_KEY_LEN_MAX 32

inline void hmacSha256(const uint8_t *key, size_t keyLen, const uint8_t *msg, size_t msgLen, uint8_t out[32]) {
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, keyLen, msg, msgLen, out);
}

inline void toHex(const uint8_t *in, size_t n, char *out) {
  static const char *h = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[i * 2]     = h[in[i] >> 4];
    out[i * 2 + 1] = h[in[i] & 0x0F];
  }
  out[n * 2] = 0;
}

inline int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Returns number of bytes decoded, or 0 on error.
inline size_t fromHex(const char *in, size_t len, uint8_t *out, size_t outMax) {
  if (len == 0 || (len % 2) || len / 2 > outMax) return 0;
  for (size_t i = 0; i < len / 2; i++) {
    int hi = hexVal(in[i * 2]), lo = hexVal(in[i * 2 + 1]);
    if (hi < 0 || lo < 0) return 0;
    out[i] = (hi << 4) | lo;
  }
  return len / 2;
}

// Constant-time comparison (does not leak how many characters matched).
inline bool ctEqual(const char *a, const char *b, size_t n) {
  uint8_t d = 0;
  for (size_t i = 0; i < n; i++) d |= a[i] ^ b[i];
  return d == 0;
}

// HMAC-SHA256(key, msg) truncated to 128 bits -> 32 hex chars.
inline void hmacHex32(const uint8_t key[32], const std::string &msg, char out[33]) {
  uint8_t mac[32];
  hmacSha256(key, 32, (const uint8_t *)msg.data(), msg.size(), mac);
  toHex(mac, 16, out);
}

// Session key K = HMAC-SHA256(APP_SECRET, "SDK1|" + DEVICE_KEY).
// Needs BOTH the app secret (only in our app) and the per-device key (only the owner knows).
inline void deriveKey(const char *deviceKey, uint8_t out[32]) {
  std::string m = "SDK1|";
  m += deviceKey;
  hmacSha256((const uint8_t *)APP_SECRET, strlen(APP_SECRET), (const uint8_t *)m.data(), m.size(), out);
}

inline void generateDeviceKey(char out[DEVICE_KEY_LEN_GEN + 1]) {
  const size_t n = sizeof(KEY_ALPHABET) - 1;
  for (int i = 0; i < DEVICE_KEY_LEN_GEN; i++) out[i] = KEY_ALPHABET[esp_random() % n];
  out[DEVICE_KEY_LEN_GEN] = 0;
}

// Keys are case-insensitive: stored upper-case, 8-32 chars of A-Z / 0-9.
inline bool normalizeDeviceKey(char *key, size_t len) {
  if (len < DEVICE_KEY_LEN_MIN || len > DEVICE_KEY_LEN_MAX) return false;
  for (size_t i = 0; i < len; i++) {
    char c = toupper((unsigned char)key[i]);
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    key[i] = c;
  }
  key[len] = 0;
  return true;
}

}  // namespace sec
