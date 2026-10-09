import { PermissionsAndroid, Platform } from 'react-native';
import { BleManager, State } from 'react-native-ble-plx';
import { SERVICE_UUID, CHR } from './config';
import {
  buildCommand,
  deriveKey,
  encryptNewKey,
  fromB64,
  parseResponse,
  parseState,
  toB64,
} from './protocol';

export const manager = new BleManager();

export async function ensureBlePermissions() {
  if (Platform.OS !== 'android') return true;
  if (Platform.Version >= 31) {
    const res = await PermissionsAndroid.requestMultiple([
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
    ]);
    return Object.values(res).every((r) => r === PermissionsAndroid.RESULTS.GRANTED);
  }
  const r = await PermissionsAndroid.request(PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION);
  return r === PermissionsAndroid.RESULTS.GRANTED;
}

export async function ensureBluetoothOn() {
  const state = await manager.state();
  if (state !== State.PoweredOn) throw new Error('Please turn on Bluetooth');
}

// Scans only for devices advertising our service UUID.
export function startScan(onDevice, onError) {
  manager.startDeviceScan([SERVICE_UUID], { allowDuplicates: false }, (error, device) => {
    if (error) {
      onError && onError(error);
      return;
    }
    if (device) onDevice(device);
  });
  return () => manager.stopDeviceScan();
}

async function readString(device, chr) {
  const c = await device.readCharacteristicForService(SERVICE_UUID, chr);
  return fromB64(c.value);
}

function friendlyError(payload) {
  switch (payload) {
    case 'AUTH':
      return 'Wrong device key (3 wrong tries lock the display for 30 s)';
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

// One authenticated connection to one display.
export class DisplaySession {
  // expectedMac: when given, the display must report this MAC (prevents talking to a look-alike).
  static async open(deviceId, deviceKey, expectedMac) {
    await ensureBluetoothOn();
    const device = await manager.connectToDevice(deviceId, { timeout: 10000, requestMTU: 185 });
    try {
      await device.discoverAllServicesAndCharacteristics();
      const info = JSON.parse(await readString(device, CHR.INFO));
      if (expectedMac && info.mac && info.mac.toUpperCase() !== expectedMac.toUpperCase()) {
        throw new Error(`This is not the saved display (MAC ${info.mac})`);
      }
      const nonce = await readString(device, CHR.NONCE);
      if (!/^[0-9a-f]{32}$/.test(nonce)) throw new Error('Display did not send a challenge');

      const session = new DisplaySession(device, deriveKey(deviceKey), nonce, info);
      session.state = await session.send('AUTH');
      return session;
    } catch (e) {
      await manager.cancelDeviceConnection(device.id).catch(() => {});
      throw e;
    }
  }

  constructor(device, K, nonce, info) {
    this.device = device;
    this.K = K;
    this.nonce = nonce;
    this.info = info;
    this.ctr = 0;
    this.connected = true;
    this.state = null;
    this._sub = device.onDisconnected(() => {
      this.connected = false;
      this.onDisconnected && this.onDisconnected();
    });
  }

  // Sends a raw command and returns the parsed display state.
  async send(cmd, ctr = ++this.ctr) {
    const msg = buildCommand(this.K, this.nonce, ctr, cmd);
    await this.device.writeCharacteristicWithResponseForService(SERVICE_UUID, CHR.CMD, toB64(msg));
    const raw = await readString(this.device, CHR.RESP);
    const res = parseResponse(this.K, this.nonce, raw, ctr);
    if (!res.ok) throw new Error(friendlyError(res.payload));
    const state = parseState(res.payload);
    if (state) this.state = state;
    return state || this.state;
  }

  setLimit(v) { return this.send(`LIM:${v}`); }
  setBrightness(v) { return this.send(`BRI:${v}`); }
  setColors(digit, text) { return this.send(`COL:${digit},${text}`); }
  rename(name) { return this.send(`NAME:${name}`); }  // display restarts afterwards
  restart() { return this.send('RST'); }

  // Display disconnects afterwards; reconnect with the new key.
  async changeKey(newKey) {
    const ctr = ++this.ctr;
    await this.send(`KEY:${encryptNewKey(this.K, this.nonce, ctr, newKey)}`, ctr);
  }

  async close() {
    this._sub && this._sub.remove();
    this.connected = false;
    await manager.cancelDeviceConnection(this.device.id).catch(() => {});
  }
}
