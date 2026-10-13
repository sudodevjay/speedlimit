// Wi-Fi displays (Raspberry Pi). Same signed protocol as BLE, carried over HTTP:
//   POST /api/session -> { sid, nonce }        (= a new BLE connection)
//   POST /api/cmd     X-Session: sid, body "<ctr>|<cmd>|<mac>" -> "<ctr>|<status>|<payload>|<mac>"
import { buildCommand, deriveKey, encryptNewKey, parseResponse, parseState } from './protocol';

export const WIFI_DEFAULT_PORT = 8099;
const TIMEOUT_MS = 6000;

// "192.168.1.94", "housys.local:8099", "http://100.107.8.31" -> "http://host:port"
export function baseUrl(host) {
  let h = host.trim().replace(/^https?:\/\//i, '').replace(/\/+$/, '');
  if (!/:\d+$/.test(h)) h += `:${WIFI_DEFAULT_PORT}`;
  return `http://${h}`;
}

async function request(url, options = {}) {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), TIMEOUT_MS);
  try {
    return await fetch(url, { ...options, signal: ctrl.signal });
  } catch (e) {
    throw new Error(`Display not reachable (${url.split('/')[2]}). Same Wi-Fi / Tailscale?`);
  } finally {
    clearTimeout(timer);
  }
}

function friendlyError(payload) {
  switch (payload) {
    case 'AUTH':
      return 'Wrong device key (3 wrong tries lock the display for 30 s)';
    case 'LOCKED':
      return 'Too many wrong keys, try again in 30 s';
    case 'RANGE':
      return 'Value out of range';
    case 'NAME':
      return 'Invalid name (1-20 chars: A-Z a-z 0-9 _ -)';
    case 'KEY':
      return 'Invalid key (8-32 chars: A-Z 0-9)';
    case 'REPLAY':
      return 'Request rejected (replay)';
    default:
      return payload || 'Display rejected the request';
  }
}

// Same interface as DisplaySession in ble.js.
export class WifiSession {
  static async open(host, deviceKey, expectedMac) {
    const base = baseUrl(host);
    const info = await (await request(`${base}/api/info`)).json();
    if (expectedMac && info.mac && info.mac.toUpperCase() !== expectedMac.toUpperCase()) {
      throw new Error(`This is not the saved display (MAC ${info.mac})`);
    }
    const session = new WifiSession(base, deriveKey(deviceKey), info);
    await session._newSession();
    session.state = await session.send('AUTH');
    return session;
  }

  constructor(base, K, info) {
    this.base = base;
    this.K = K;
    this.info = info;
    this.connected = true;
    this.state = null;
  }

  async _newSession() {
    const res = await request(`${this.base}/api/session`, { method: 'POST' });
    if (res.status === 429) throw new Error(friendlyError('LOCKED'));
    const { sid, nonce } = await res.json();
    if (!/^[0-9a-f]{32}$/.test(nonce || '')) throw new Error('Display did not send a challenge');
    this.sid = sid;
    this.nonce = nonce;
    this.ctr = 0;
  }

  async _post(cmd, ctr) {
    const res = await request(`${this.base}/api/cmd`, {
      method: 'POST',
      headers: { 'X-Session': this.sid, 'Content-Type': 'text/plain' },
      body: buildCommand(this.K, this.nonce, ctr, cmd),
    });
    return { status: res.status, raw: await res.text() };
  }

  // Sends a raw command and returns the parsed display state.
  async send(cmd, ctr = ++this.ctr) {
    let { status, raw } = await this._post(cmd, ctr);
    if (status === 401) {
      // Session expired on the display (idle > 2 min): start a new one and retry once.
      await this._newSession();
      ctr = ++this.ctr;
      ({ status, raw } = await this._post(cmd, ctr));
    }
    if (raw.startsWith('0|ERR|')) throw new Error(friendlyError(raw.split('|')[2]));
    const res = parseResponse(this.K, this.nonce, raw, ctr);
    if (!res.ok) throw new Error(friendlyError(res.payload));
    const state = parseState(res.payload);
    if (state) this.state = state;
    return state || this.state;
  }

  setLimit(v) { return this.send(`LIM:${v}`); }
  setBrightness(v) { return this.send(`BRI:${v}`); }
  setColors(digit, text) { return this.send(`COL:${digit},${text}`); }
  rename(name) { return this.send(`NAME:${name}`); }
  setMode(mode) { return this.send(`MODE:${mode}`); }  // 'speed' | 'video'
  restart() { return this.send('RST'); }

  async changeKey(newKey) {
    const ctr = ++this.ctr;
    await this.send(`KEY:${encryptNewKey(this.K, this.nonce, ctr, newKey)}`, ctr);
    this.connected = false;
  }

  async close() {
    this.connected = false;
  }
}
