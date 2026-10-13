#!/usr/bin/env python3
"""Fail on private paths, likely secrets, local home paths or private commit emails.

Scans every blob reachable from any Git ref, including files deleted later.
This complements a dedicated secret scanner; it is not a proof of absence.
Findings print only rule names and paths, never matched values.
"""
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATTERNS = {
    'private-key': rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----',
    'provider-token': rb'(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}|sk-(?:proj-|svcacct-)?[A-Za-z0-9_-]{32,}|AKIA[0-9A-Z]{16}|xox[baprs]-[A-Za-z0-9-]{20,})',
    'local-home': rb'/(?:Users|home)/[A-Za-z0-9_.-]+/',
    'credential-url': rb'https?://[^\s/:]+:[^\s/@]+@',
}
PRIVATE_PATH = re.compile(r'^(?:private|artifacts|build[^/]*|\.env(?:\.[^/]*)?)(?:/|$)|(?:^|/)[^/]*\.dSYM/|\.(?:bundle|pem|key)$')
LOCAL_DEPENDENCY_PATH = re.compile(r'^components/(?!esp_lcd_touch_cst9220(?:/|$))')


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)


def scan():
    findings = set()
    objects = git('rev-list', '--objects', '--all').decode().splitlines()
    # Check every tree name as well: one shared blob can have several historical paths.
    commits = git('rev-list', '--all').decode().splitlines()
    for commit in commits:
        paths = git('ls-tree', '-r', '--name-only', '-z', commit).decode().split('\0')
        for path in paths:
            if PRIVATE_PATH.search(path): findings.add(('private-path', path))
            if LOCAL_DEPENDENCY_PATH.search(path): findings.add(('local-dependency', path))
        metadata = git('show', '-s', '--format=%ae%n%ce', commit).decode().splitlines()
        for email in metadata:
            if not (email.endswith('@users.noreply.github.com') or email.endswith('@example.invalid') or email == 'noreply@github.com'):
                findings.add(('commit-email', commit[:12]))
    process = subprocess.Popen(['git', 'cat-file', '--batch'], cwd=ROOT,
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    blobs = 0
    try:
        for item in objects:
            oid, _, path = item.partition(' ')
            process.stdin.write((oid + '\n').encode()); process.stdin.flush()
            header = process.stdout.readline().decode().split()
            if len(header) != 3: raise RuntimeError('Cannot read Git object')
            _, kind, length = header
            data = process.stdout.read(int(length))
            if len(data) != int(length) or process.stdout.read(1) != b'\n':
                raise RuntimeError('Incomplete Git object')
            if kind not in ('blob', 'commit', 'tag'): continue
            if kind == 'blob': blobs += 1
            for name, pattern in PATTERNS.items():
                if re.search(pattern, data): findings.add((name, path or oid[:12]))
    finally:
        process.stdin.close(); process.stdout.close()
        process.wait()
    if findings:
        for name, path in sorted(findings): print(f'FAIL {name}: {path}')
        raise SystemExit(1)
    if not commits: raise SystemExit('No committed history to audit')
    print(f'PASS: {len(commits)} commits, {blobs} reachable blobs; no matching private paths, secrets, home paths or private commit emails')


if __name__ == '__main__':
    scan()
