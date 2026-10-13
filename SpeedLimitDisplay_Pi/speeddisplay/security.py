"""Same protocol as the ESP32 firmware (Security.h) and the app (src/protocol.js).

K        = HMAC-SHA256(APP_SECRET, "SDK1|" + DEVICE_KEY)
request  = "<ctr>|<cmd>|<mac>",  mac = first 32 hex of HMAC(K, nonce + "|" + ctr + "|" + cmd)
response = "<ctr>|<OK|ERR>|<payload>|<mac>", mac = HMAC(K, nonce + "|R|" + ctr + "|" + status + "|" + payload)
"""

import hashlib
import hmac
import secrets

KEY_ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789"  # no 0/O/1/I/L
KEY_LEN_GEN, KEY_LEN_MIN, KEY_LEN_MAX = 8, 8, 32


def hmac_sha256(key: bytes, msg: str) -> bytes:
    return hmac.new(key, msg.encode(), hashlib.sha256).digest()


def mac32(key: bytes, msg: str) -> str:
    return hmac_sha256(key, msg)[:16].hex()


def derive_key(app_secret: str, device_key: str) -> bytes:
    return hmac_sha256(app_secret.encode(), "SDK1|" + device_key)


def generate_device_key() -> str:
    return "".join(secrets.choice(KEY_ALPHABET) for _ in range(KEY_LEN_GEN))


def new_nonce() -> str:
    return secrets.token_hex(16)


def normalize_device_key(key: str):
    key = key.upper()
    if not (KEY_LEN_MIN <= len(key) <= KEY_LEN_MAX) or not all(c.isascii() and c.isalnum() for c in key):
        return None
    return key


def ct_equal(a: str, b: str) -> bool:
    return hmac.compare_digest(a.encode(), b.encode())


def decrypt_new_key(K: bytes, nonce: str, ctr: str, enc_hex: str):
    """KEY command: hex(newKey XOR HMAC(K, nonce|KEY|ctr))."""
    try:
        enc = bytes.fromhex(enc_hex)
    except ValueError:
        return None
    if not 0 < len(enc) <= KEY_LEN_MAX:
        return None
    ks = hmac_sha256(K, f"{nonce}|KEY|{ctr}")
    try:
        key = bytes(e ^ k for e, k in zip(enc, ks)).decode("ascii")
    except UnicodeDecodeError:
        return None
    return normalize_device_key(key)
