// Saved displays (incl. their device keys) live in the Android Keystore / iOS Keychain.
import * as Keychain from 'react-native-keychain';

const INDEX_KEY = 'displays_index';
const itemKey = (mac) => 'display_' + mac.replace(/[^A-Za-z0-9]/g, '_');

async function getItem(key) {
  const res = await Keychain.getGenericPassword({ service: key });
  return res ? res.password : null;
}

const setItem = (key, value) => Keychain.setGenericPassword('v', value, { service: key });
const deleteItem = (key) => Keychain.resetGenericPassword({ service: key });

async function readIndex() {
  const raw = await getItem(INDEX_KEY);
  return raw ? JSON.parse(raw) : [];
}

// record: { mac, id, name, key, lastLimit }
export async function listDisplays() {
  const macs = await readIndex();
  const items = await Promise.all(macs.map((m) => getItem(itemKey(m))));
  return items.filter(Boolean).map((s) => JSON.parse(s));
}

export async function saveDisplay(record) {
  const mac = record.mac.toUpperCase();
  await setItem(itemKey(mac), JSON.stringify({ ...record, mac }));
  const macs = await readIndex();
  if (!macs.includes(mac)) {
    macs.push(mac);
    await setItem(INDEX_KEY, JSON.stringify(macs));
  }
}

export async function updateDisplay(mac, patch) {
  const raw = await getItem(itemKey(mac));
  if (!raw) return;
  await saveDisplay({ ...JSON.parse(raw), ...patch });
}

export async function removeDisplay(mac) {
  await deleteItem(itemKey(mac));
  const macs = (await readIndex()).filter((m) => m !== mac);
  await setItem(INDEX_KEY, JSON.stringify(macs));
}
