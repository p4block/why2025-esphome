#!/usr/bin/env python3
"""Generate local build credentials without printing or overwriting them."""
import base64
import os
from pathlib import Path
import secrets


def main():
    path = Path(__file__).resolve().parent.parent / 'secrets.yaml'
    values = {
        'api_encryption_key': base64.b64encode(secrets.token_bytes(32)).decode(),
        'ota_password': secrets.token_urlsafe(24),
        'fallback_ap_password': secrets.token_urlsafe(12),
    }
    try:
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError:
        raise SystemExit('secrets.yaml already exists; keeping existing credentials.')
    with os.fdopen(fd, 'w') as stream:
        stream.write('# Local credentials. Never commit this file.\n')
        for name, value in values.items():
            stream.write(f'{name}: "{value}"\n')
    print('Created secrets.yaml (owner-only permissions). Values were not printed.')


if __name__ == '__main__':
    main()
