#!/usr/bin/env python3
"""Verify locally installed component bytes against the reviewed manifest."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def tree_hash(directory):
    digest = hashlib.sha256()
    for file in sorted(directory.rglob('*')):
        if file.is_symlink():
            raise ValueError('Symlink in vendored component')
        if file.is_file():
            data = file.read_bytes()
            digest.update(file.relative_to(directory).as_posix().encode() + b'\0')
            digest.update(str(len(data)).encode() + b'\0' + data)
    return digest.hexdigest()


def main():
    lock = json.loads((ROOT / 'dependencies.lock.json').read_text())
    for name, info in lock['components'].items():
        if not (ROOT / 'components' / name).is_dir():
            raise SystemExit('Missing local dependency: ' + name + '; see THIRD_PARTY.md')
        if tree_hash(ROOT / 'components' / name) != info['tree_sha256']:
            raise SystemExit('Dependency content changed: ' + name)
    print('PASS: all local component hashes match')


if __name__ == '__main__':
    main()
