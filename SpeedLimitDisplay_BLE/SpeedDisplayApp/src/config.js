// Must match firmware/SpeedLimitDisplay/config.h exactly.
// APP_SECRET lives in src/secrets.js (not in git); copy secrets.example.js -> secrets.js.
export { APP_SECRET } from './secrets';

export const SERVICE_UUID = 'c6c4d30e-41ad-47d9-8521-0e902c3c6421';
export const CHR = {
  INFO: '725f07fd-510e-4008-ad3a-dc07de78fac4',
  NONCE: '3b994d37-11de-4192-8eac-5dbf22085757',
  CMD: '05ced542-4bfb-4d77-acaf-bba8b755cd47',
  RESP: 'de36aebf-2fa0-42a5-95e8-d61b16ffdb5c',
};

// Same order as COLOR_PRESETS in the firmware.
export const COLOR_PRESETS = [
  { name: 'Red', hex: '#ff2020' },
  { name: 'White', hex: '#ffffff' },
  { name: 'Yellow', hex: '#ffe600' },
  { name: 'Green', hex: '#20e040' },
  { name: 'Amber', hex: '#ff8a00' },
  { name: 'Cyan', hex: '#00e5ff' },
];

export const QUICK_LIMITS = [10, 20, 25, 30, 40, 50, 60, 70, 80, 90];
export const MAX_LIMIT = 99; // must match MAX_LIMIT in firmware config.h
