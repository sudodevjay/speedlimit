// Signed command protocol shared with the firmware (see README.md -> "Security protocol").
import { sha256 } from 'js-sha256';
import { APP_SECRET } from './config';

// K = HMAC-SHA256(APP_SECRET, "SDK1|" + DEVICE_KEY)  -> 32-byte array
export function deriveKey(deviceKey) {
  return sha256.hmac.array(APP_SECRET, 'SDK1|' + normalizeKey(deviceKey));
}

export function normalizeKey(key) {
  return (key || '').trim().toUpperCase();
}

export function isValidKey(key) {
  return /^[A-Z0-9]{8,32}$/.test(normalizeKey(key));
}

function mac32(K, msg) {
  return sha256.hmac(K, msg).slice(0, 32);
}

export function buildCommand(K, nonce, ctr, cmd) {
  return `${ctr}|${cmd}|${mac32(K, `${nonce}|${ctr}|${cmd}`)}`;
}

// Returns { ok, payload }. Throws if the response is not signed by the real display.
export function parseResponse(K, nonce, raw, expectedCtr) {
  const parts = (raw || '').split('|');
  if (parts.length < 3) throw new Error('No response from display');

  // Unsigned errors (device could not verify us).
  if (parts[0] === '0' && parts[1] === 'ERR') {
    return { ok: false, payload: parts[2] };
  }
  if (parts.length !== 4) throw new Error('Malformed response');
  const [ctr, status, payload, mac] = parts;
  if (ctr !== String(expectedCtr)) throw new Error('Response out of order');
  if (mac !== mac32(K, `${nonce}|R|${ctr}|${status}|${payload}`)) {
    throw new Error('Display signature invalid - this is not a genuine display');
  }
  return { ok: status === 'OK', payload };
}

// "lim=45;bri=125;dcol=0;tcol=1;name=SpeedDisplay1" -> object
export function parseState(payload) {
  const s = {};
  (payload || '').split(';').forEach((kv) => {
    const i = kv.indexOf('=');
    if (i > 0) s[kv.slice(0, i)] = kv.slice(i + 1);
  });
  if (s.lim === undefined) return null;
  return {
    limit: Number(s.lim),
    brightness: Number(s.bri),
    digitColor: Number(s.dcol),
    textColor: Number(s.tcol),
    name: s.name,
    // Raspberry Pi (Wi-Fi) displays only: 'speed' | 'video', videos on the USB stick, video playing now
    mode: s.mode,
    videos: s.vid === undefined ? undefined : Number(s.vid),
    playing: s.play === '1',
  };
}

// New key is sent encrypted: hex(newKey XOR HMAC(K, nonce|KEY|ctr)).
export function encryptNewKey(K, nonce, ctr, newKey) {
  const ks = sha256.hmac.array(K, `${nonce}|KEY|${ctr}`);
  const k = normalizeKey(newKey);
  let hex = '';
  for (let i = 0; i < k.length; i++) {
    hex += ((k.charCodeAt(i) ^ ks[i]) & 0xff).toString(16).padStart(2, '0');
  }
  return hex;
}

// All protocol strings are ASCII, so btoa/atob (built into Hermes) are enough.
export const toB64 = (s) => btoa(s);
export const fromB64 = (b) => (b ? atob(b) : '');
