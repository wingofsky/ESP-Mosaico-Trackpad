#!/usr/bin/env python3
"""Back up an explicitly selected device, then write only its existing ota_0."""
import argparse
import hashlib
from importlib.metadata import version
import json
import os
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parent.parent
FLASH_SIZE = 0x1000000
APP_OFFSET = 0x210000
APP_SIZE = 0xCF0000
TABLE_OFFSET = 0x8000
TABLE_SIZE = 0x1000
PRESERVED = ((0, APP_OFFSET), (APP_OFFSET + APP_SIZE, 0x100000))
PARTITIONS = (
    (1, 0, 0x9000, 0x2000, "otadata"),
    (1, 1, 0xB000, 0x1000, "phy_init"),
    (1, 2, 0xC000, 0x14000, "sysmeta"),
    (0, 0, 0x20000, 0x1C0000, "factory"),
    (1, 3, 0x1E0000, 0x20000, "coredump"),
    (1, 2, 0x200000, 0x10000, "nvs"),
    (0, 0x10, APP_OFFSET, APP_SIZE, "ota_0"),
    (1, 0x40, 0xF00000, 0x100000, "ui_apps"),
)


def require(condition, message):
    # Safety checks must survive python -O and PYTHONOPTIMIZE.
    if not condition:
        raise ValueError(message)


def validate_table(table):
    require(len(table) == TABLE_SIZE, "Expected a complete 4 KiB partition sector")
    entries = []
    for offset in range(0, TABLE_SIZE, 32):
        record = table[offset:offset + 32]
        magic = int.from_bytes(record[:2], "little")
        if magic == 0xEBEB:
            require(record[16:] == hashlib.md5(table[:offset]).digest(), "Invalid partition checksum")
            require(all(b == 0xFF for b in table[offset + 32:]), "Unexpected partition trailer")
            break
        require(magic == 0x50AA, "Missing partition checksum or invalid entry")
        _, kind, subtype, start, size, name, flags = struct.unpack("<HBBII16sI", record)
        require(flags == 0, "Unsupported partition flags")
        entries.append((kind, subtype, start, size, name.rstrip(b"\0").decode("ascii")))
    require(tuple(entries) == PARTITIONS, "Unsupported partition layout; no write performed")


def check_device(esp, api, expected_mac=None):
    require(esp.CHIP_NAME == "ESP32-S31", "Unexpected chip")
    mac = bytes(esp.read_mac()).hex()
    require(len(mac) == 12, "Invalid device identity")
    if expected_mac is not None:
        require(mac == expected_mac, "Device does not match this backup")
    require(not esp.get_secure_boot_enabled(), "Secure boot is enabled; unsupported")
    require(not esp.get_flash_encryption_enabled(), "Flash encryption is enabled; unsupported")
    api.attach_flash(esp)
    require((esp.flash_id() >> 16) & 0xFF == 24, "Expected 16 MiB flash")
    esp.flash_set_parameters(FLASH_SIZE)
    return mac


def read_table(esp):
    table = esp.read_flash(TABLE_OFFSET, TABLE_SIZE)
    validate_table(table)
    return table


def private_write(path, content):
    # Exclusive creation keeps a previous recovery backup intact.
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as stream:
        stream.write(content)
        stream.flush()
        os.fsync(stream.fileno())


def backup_device(esp, api, manifest):
    require(not manifest.exists() and not manifest.with_suffix(".bin").exists(), "Backup already exists; choose another name")
    mac = check_device(esp, api)
    table = read_table(esp)
    data = esp.read_flash(0, FLASH_SIZE)
    require(len(data) == FLASH_SIZE, "Incomplete flash backup")
    require(data[TABLE_OFFSET:TABLE_OFFSET + TABLE_SIZE] == table, "Partition table changed during backup")
    require(esp.flash_md5sum(0, FLASH_SIZE) == hashlib.md5(data).hexdigest(), "Backup verification failed")
    record = {"format": 1, "chip": "ESP32-S31", "mac": mac,
              "sha256": hashlib.sha256(data).hexdigest()}
    private_write(manifest.with_suffix(".bin"), data)
    private_write(manifest, (json.dumps(record, indent=2) + "\n").encode())
    return {"backup": "verified", "bytes": len(data)}


def flash_device(esp, api, manifest, image, table_file):
    record = json.loads(manifest.read_text())
    require(record.get("format") == 1 and record.get("chip") == "ESP32-S31", "Unsupported backup manifest")
    data = manifest.with_suffix(".bin").read_bytes()
    require(len(data) == FLASH_SIZE and hashlib.sha256(data).hexdigest() == record.get("sha256"), "Missing or damaged backup")
    table = data[TABLE_OFFSET:TABLE_OFFSET + TABLE_SIZE]
    validate_table(table)
    built_table = table_file.read_bytes()
    require(built_table and len(built_table) <= TABLE_SIZE, "Invalid build partition table")
    require(built_table.ljust(TABLE_SIZE, b"\xff") == table, "Build partition table does not match the backup")
    firmware = image.read_bytes()
    require(24 <= len(firmware) <= APP_SIZE and firmware[0] == 0xE9, "Invalid or oversized application image")
    require(isinstance(record.get("mac"), str) and len(record["mac"]) == 12, "Missing backup device identity")
    check_device(esp, api, record["mac"])
    require(read_table(esp) == table, "Device partition table does not match the backup")
    expected = {}
    for offset, size in PRESERVED:
        digest = hashlib.md5(data[offset:offset + size]).hexdigest()
        require(esp.flash_md5sum(offset, size) == digest, "Preserved flash differs from backup; create a fresh backup")
        expected[offset] = digest
    # Pass validated bytes, rather than reopening a mutable image path.
    images = [(APP_OFFSET, firmware)]
    api.write_flash(esp, images, flash_freq="keep", flash_mode="keep", flash_size="keep", compress=False)
    api.verify_flash(esp, [(APP_OFFSET, firmware)])
    for offset, size in PRESERVED:
        require(esp.flash_md5sum(offset, size) == expected[offset], "Preserved flash changed; device left in download mode")
    result = {"write": "passed", "independent_verify": "passed", "preserved_regions": "passed",
              "offset": hex(APP_OFFSET), "sha256": hashlib.sha256(firmware).hexdigest(), "bytes": len(firmware)}
    api.reset_chip(esp, "hard-reset")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("backup", "flash"))
    parser.add_argument("--port", required=True, help="Explicit ROM serial port; enter download mode manually first")
    parser.add_argument("--device", type=Path, default=ROOT / "private/device.json", help="Private backup manifest")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    args = parser.parse_args()
    private = (ROOT / "private").resolve()
    manifest = args.device.resolve()
    require(private in manifest.parents and manifest.suffix == ".json", "Store device backups under this repository's ignored private/ directory")
    manifest.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
    require(version("esptool") == "5.4.0", "Use the tested esptool version: python -m pip install -r requirements-tools.txt")
    from esptool import cmds
    esp = cmds.detect_chip(args.port, connect_mode="no-reset")
    try:
        if args.action == "backup":
            result = backup_device(esp, cmds, manifest)
        else:
            result = flash_device(esp, cmds, manifest, args.build_dir / "mosaico_trackpad.bin",
                                  args.build_dir / "partition_table/partition-table.bin")
        print(json.dumps(result, indent=2))
    finally:
        esp._port.close()


if __name__ == "__main__":
    main()
