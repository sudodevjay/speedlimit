// Opens the right kind of session for a saved display record.
import { DisplaySession } from './ble';
import { WifiSession } from './wifi';

export const isWifi = (record) => record.transport === 'wifi';

export function openSession(record) {
  return isWifi(record)
    ? WifiSession.open(record.host, record.key, record.mac)
    : DisplaySession.open(record.id, record.key, record.mac);
}
