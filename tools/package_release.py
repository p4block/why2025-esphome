#!/usr/bin/env python3
"""Create an allowlisted, source-only GitHub package; never copy secrets/builds."""
import hashlib
from pathlib import Path
import re
import shutil
import zipfile

TOP_FILES = ['README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', '.gitignore',
             'requirements.txt', 'secrets.example.yaml', 'whybadge.yaml',
             'whybadge-radio-bridge.yaml', 'whybadge-serial.yaml']
SOURCE_DIRS = ['components', 'tools', 'tests', 'homeassistant', 'radio', '.github']
ASSETS = ['assets/badge-desktop.png', 'assets/badge-preview.png']
SKIP_PARTS = {'__pycache__', '.git', '.venv', '.esphome', '.cache', 'backups'}
SKIP_SUFFIXES = {'.pyc', '.pyo', '.bin', '.elf', '.log'}


def sanitize_config(text):
    text = re.sub(r'^#.*\bMAC\b.*\n', '', text, flags=re.MULTILINE)
    text = re.sub(r'(type: esp-idf\n)(?!\s+version:)', r'\1    version: 5.5.5\n', text)
    text = text.replace('    password: why2025setup', '    password: !secret fallback_ap_password')
    if '  id: badge_api\n' in text and '    key: !secret api_encryption_key' not in text:
        text = text.replace('  id: badge_api\n', '  id: badge_api\n  encryption:\n    key: !secret api_encryption_key\n')
    if 'ota:\n  - platform: esphome\n' in text and 'password: !secret ota_password' not in text:
        text = text.replace('ota:\n  - platform: esphome\n',
                            'ota:\n  - platform: esphome\n    password: !secret ota_password\n')
    return text


def audit(folder):
    errors = []
    for path in folder.rglob('*'):
        if not path.is_file():
            continue
        rel = path.relative_to(folder)
        if any(part in SKIP_PARTS for part in rel.parts) or path.suffix in SKIP_SUFFIXES:
            errors.append(f'Forbidden generated file: {rel}')
        if path.name == 'secrets.yaml' or (path.name.startswith('secrets.') and path.name != 'secrets.example.yaml'):
            errors.append(f'Local secrets file: {rel}')
        if path.suffix == '.png':
            continue
        text = path.read_text()
        # Runtime field names and !secret references are legitimate source code.
        patterns = {
            'device MAC': r'(?i)\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b',
            'personal home path': r'/(?:home|Users)/[A-Za-z0-9_.-]+/',
            'literal IP address': r'\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}\b',
            'private key': r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',
            'GitHub token': r'\b(?:ghp_|github_pat_)[A-Za-z0-9_]{20,}',
        }
        for label, pattern in patterns.items():
            if re.search(pattern, text):
                errors.append(f'{label} in {rel}')
        if path.suffix == '.yaml':
            for match in re.finditer(r'^\s*(?:password|key):\s*(.+)$', text, flags=re.MULTILINE):
                value = match.group(1)
                if not value.startswith('!secret '):
                    errors.append(f'Embedded credential field in {rel}')
    if errors:
        raise SystemExit('\n'.join(errors))


def main():
    root = Path(__file__).resolve().parent.parent
    output = root / 'dist/whybadge-esphome'
    # Only replace our generated package, never arbitrary user paths.
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    files = [root / name for name in TOP_FILES + ASSETS]
    for directory in SOURCE_DIRS:
        files += [p for p in (root / directory).rglob('*') if p.is_file()
                  and not any(part in SKIP_PARTS for part in p.relative_to(root).parts)
                  and p.suffix not in SKIP_SUFFIXES]
    for path in sorted(set(files)):
        if path.is_symlink():
            raise SystemExit(f'Refusing symlink: {path.relative_to(root)}')
        rel = path.relative_to(root)
        destination = output / rel
        destination.parent.mkdir(parents=True, exist_ok=True)
        if path.name.startswith('whybadge') and path.suffix == '.yaml':
            destination.write_text(sanitize_config(path.read_text()))
        elif rel == Path('homeassistant/SETUP.txt'):
            text = path.read_text().replace(
                'The hotel network cannot currently reach Home Assistant; end-to-end discovery\nand physical light changes must be checked once on the home network.',
                'Verify discovery and physical light changes on your own Home Assistant network.')
            destination.write_text(text)
        else:
            shutil.copyfile(path, destination)
    audit(output)
    # Hashes describe source files only, never a device image or local credential.
    manifest = ''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(output).as_posix()}\n'
                       for p in sorted(output.rglob('*')) if p.is_file())
    (output / 'MANIFEST.sha256').write_text(manifest)
    archive = root / 'dist/whybadge-esphome.zip'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as zip_file:
        for path in sorted(output.rglob('*')):
            if path.is_file():
                zip_file.write(path, Path(output.name) / path.relative_to(output))
    print(f'Clean source directory: {output}')
    print(f'Source archive: {archive} ({archive.stat().st_size} bytes)')
    print('Allowlist/privacy audit passed; no flash images or local secrets included.')


if __name__ == '__main__':
    main()
