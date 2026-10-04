#!/usr/bin/env python3
"""Build the C6 ESP-Hosted/backlight firmware; never flash hardware."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from esphome.espidf.framework import get_framework_env, get_idf_tools_path

HOSTED_COMMIT = '471900d45c3b9208da10a01beb200b97f15a161a'
OVERLAY_COMMIT = 'e4c39e0187db8fb6f9921cf8fa875463a792ce7f'
IDF_VERSION = '5.5.5'


def run(*args, **kwargs):
    subprocess.run(args, check=True, **kwargs)


def checkout(url, directory, revision):
    if not directory.exists():
        run('git', 'clone', '--no-checkout', url, str(directory))
    run('git', '-C', str(directory), 'checkout', '--detach', revision)
    actual = subprocess.check_output(['git', '-C', str(directory), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != revision:
        raise RuntimeError(f'Unexpected upstream revision: {actual}')


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', type=Path, default=root / '.cache/radio-build')
    args = parser.parse_args()
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    source = work / 'esp-hosted-mcu'
    overlay = work / 'esp-hosted-firmware'
    checkout('https://github.com/espressif/esp-hosted-mcu.git', source, HOSTED_COMMIT)
    checkout('https://github.com/esphome/esp-hosted-firmware.git', overlay, OVERLAY_COMMIT)
    project = source / 'slave'
    run('bash', str(overlay / 'apply-overlay.sh'), str(project))
    shutil.copyfile(root / 'radio/backlight.c', project / 'main/why_backlight.c')
    cmake = project / 'main/CMakeLists.txt'
    text = cmake.read_text()
    if '"why_backlight.c"' not in text:
        cmake.write_text(text.replace('set(COMPONENT_SRCS', 'set(COMPONENT_SRCS\n\t"why_backlight.c"', 1))
    main_file = project / 'main/esp_hosted_coprocessor.c'
    text = main_file.read_text()
    if 'why_backlight_init();' not in text:
        text = text.replace('void app_main(void)', 'extern void why_backlight_init(void);\nvoid app_main(void)', 1)
        text = text.replace('void app_main(void)\n{', 'void app_main(void)\n{\n\twhy_backlight_init();', 1)
        main_file.write_text(text)
    idf = Path(get_idf_tools_path())
    framework = idf / f'frameworks/{IDF_VERSION}'
    python_env = idf / f'penvs/{IDF_VERSION}'
    if not framework.exists() or not python_env.exists():
        raise SystemExit('Compile whybadge.yaml with ESPHome first to install the pinned ESP-IDF toolchain.')
    env = os.environ.copy()
    env.update(get_framework_env(framework, python_env, env))
    env['IDF_TARGET'] = 'esp32c6'
    run(str(python_env / 'bin/python'), str(framework / 'tools/idf.py'),
        '-DPYTHON_DEPS_CHECKED=1', 'build', cwd=project, env=env)
    # Export the matched bootloader and partition table, not unrelated old binaries.
    output = root / 'assets/radio'
    output.mkdir(parents=True, exist_ok=True)
    artifacts = {
        'bootloader.bin': project / 'build/bootloader/bootloader.bin',
        'partition-table.bin': project / 'build/partition_table/partition-table.bin',
        'esp-hosted-2.12.12-why-backlight.bin': project / 'build/network_adapter.bin',
    }
    hashes = {}
    for name, built in artifacts.items():
        shutil.copyfile(built, output / name)
        hashes[name] = hashlib.sha256((output / name).read_bytes()).hexdigest()
    manifest = {'esp_hosted_commit': HOSTED_COMMIT, 'overlay_commit': OVERLAY_COMMIT,
                'idf_version': IDF_VERSION, 'sha256': hashes}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Built and checksummed radio files in:', output)


if __name__ == '__main__':
    main()
