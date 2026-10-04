#!/usr/bin/env python3
"""Program the C6 through whybadge-radio-bridge.yaml, then restore whybadge.yaml.

Run with the workspace venv; radio images are under assets/radio.
The bridge holds C6 BOOT low. Do not let esptool reset the host during flashing.
"""
import argparse
import hashlib
import json
from pathlib import Path
import serial
import esptool
from esptool import cmds

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', default='/dev/ttyUSB0')
args = parser.parse_args()
base = Path(__file__).resolve().parent.parent / 'assets/radio'
image = base / 'esp-hosted-2.12.12-why-backlight.bin'
manifest_path = base / 'manifest.json'
if not manifest_path.exists():
    raise SystemExit('Run tools/build_radio.py first; radio manifest is missing.')
manifest = json.loads(manifest_path.read_text())
for name in ('bootloader.bin', 'partition-table.bin', image.name):
    expected = manifest['sha256'].get(name)
    if not expected or hashlib.sha256((base / name).read_bytes()).hexdigest() != expected:
        raise SystemExit('Radio firmware checksum mismatch: ' + name)
s = serial.Serial()
s.port = args.port
s.baudrate = 460800
s.timeout = 1
s.dtr = False
s.rts = False
try:
    s.open()
    esp = esptool.detect_chip(s, 460800, connect_mode='no-reset', connect_attempts=15)
    if esp.CHIP_NAME != 'ESP32-C6':
        raise RuntimeError('Unexpected radio chip: ' + esp.CHIP_NAME)
    cmds.attach_flash(esp)
    cmds.flash_id(esp)
    cmds.write_flash(esp, [(0, str(base / 'bootloader.bin')),
                          (0x8000, str(base / 'partition-table.bin')),
                          (0x9000, b'\xff' * 16384),  # C6 NVS
                          (0xd000, b'\xff' * 8192),   # matched table's OTA data
                          (0x10000, str(image))],
                     flash_size='4MB', flash_freq='80m', flash_mode='dio')
    print('C6 written and verified. Restore the whybadge.yaml P4 firmware now.')
finally:
    s.close()
