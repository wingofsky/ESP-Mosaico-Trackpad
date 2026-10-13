"""Offline safety tests: fake flash memory, never a USB connection."""
import hashlib
import json
from pathlib import Path
import stat
import struct
import sys
import tempfile
import unittest
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import flash_app as f
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from idf_ext import block_unsafe_actions


def partition_table():
    entries = b''.join(struct.pack('<HBBII16sI', 0x50AA, kind, subtype, offset, size,
                                   name.encode(), 0)
                       for kind, subtype, offset, size, name in f.PARTITIONS)
    return (entries + b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(entries).digest()).ljust(f.TABLE_SIZE, b'\xff')


class Device:
    CHIP_NAME = 'ESP32-S31'
    def __init__(self, data):
        self.data = bytearray(data)
        self.secure = self.encrypted = False
        self.mac = bytes.fromhex('020000000001')
        self.flash_capacity = 24
    def read_mac(self): return self.mac
    def get_secure_boot_enabled(self): return self.secure
    def get_flash_encryption_enabled(self): return self.encrypted
    def flash_id(self): return self.flash_capacity << 16
    def flash_set_parameters(self, size): pass
    def read_flash(self, start, size): return bytes(self.data[start:start + size])
    def flash_md5sum(self, start, size): return hashlib.md5(self.data[start:start + size]).hexdigest()


class API:
    def __init__(self):
        self.writes = []
        self.verified = self.reset = False
        self.corrupt_preserved = self.fail_verify = False
    def attach_flash(self, esp): pass
    def write_flash(self, esp, images, **kwargs):
        for offset, data in images:
            if not isinstance(data, bytes): raise TypeError('Pass immutable image bytes to esptool')
            self.writes.append((offset, len(data)))
            esp.data[offset:offset + len(data)] = data
        if self.corrupt_preserved: esp.data[0] ^= 1
    def verify_flash(self, esp, images):
        if self.fail_verify: raise RuntimeError('verification failed')
        for offset, data in images:
            if not isinstance(data, bytes): raise TypeError('Pass immutable image bytes to esptool')
            if esp.read_flash(offset, len(data)) != data: raise RuntimeError('mismatch')
        self.verified = True
    def reset_chip(self, esp, mode): self.reset = True


class FlashSafety(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.table = partition_table()
        data = bytearray(b'\xff' * f.FLASH_SIZE)
        data[f.TABLE_OFFSET:f.TABLE_OFFSET + f.TABLE_SIZE] = cls.table
        cls.data = bytes(data)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.manifest = root / 'device.json'
        self.record = {'format': 1, 'chip': 'ESP32-S31', 'mac': '020000000001',
                       'sha256': hashlib.sha256(self.data).hexdigest()}
        self.manifest.write_text(json.dumps(self.record))
        self.manifest.with_suffix('.bin').write_bytes(self.data)
        self.image = root / 'app.bin'
        self.image.write_bytes(b'\xe9' + b'\0' * 255)
        self.table_file = root / 'table.bin'
        self.table_file.write_bytes(self.table[:0xC00])
        self.esp, self.api = Device(self.data), API()

    def flash(self):
        return f.flash_device(self.esp, self.api, self.manifest, self.image, self.table_file)

    def refused(self):
        with self.assertRaises((ValueError, FileNotFoundError)):
            self.flash()
        self.assertEqual(self.api.writes, [])
        self.assertFalse(self.api.reset)

    def test_only_app_written_and_all_other_bytes_preserved(self):
        result = self.flash()
        self.assertEqual(self.api.writes, [(f.APP_OFFSET, 256)])
        self.assertTrue(self.api.verified and self.api.reset)
        self.assertEqual(result['preserved_regions'], 'passed')
        for off, size in f.PRESERVED:
            self.assertEqual(self.esp.read_flash(off, size), self.data[off:off + size])

    def test_wrong_chip(self):
        self.esp.CHIP_NAME = 'ESP32-S3'; self.refused()
    def test_wrong_device(self):
        self.esp.mac = bytes.fromhex('020000000002'); self.refused()
    def test_secure_boot(self):
        self.esp.secure = True; self.refused()
    def test_encryption(self):
        self.esp.encrypted = True; self.refused()
    def test_wrong_capacity(self):
        self.esp.flash_capacity = 23; self.refused()
    def test_missing_backup(self):
        self.manifest.with_suffix('.bin').unlink(); self.refused()
    def test_damaged_backup(self):
        self.manifest.with_suffix('.bin').write_bytes(b'incomplete'); self.refused()
    def test_wrong_backup_hash(self):
        self.record['sha256'] = '0' * 64
        self.manifest.write_text(json.dumps(self.record)); self.refused()
    def test_missing_backup_identity(self):
        del self.record['mac']
        self.manifest.write_text(json.dumps(self.record)); self.refused()
    def test_bad_device_partition(self):
        self.esp.data[f.TABLE_OFFSET] ^= 1; self.refused()
    def test_bad_build_partition(self):
        self.table_file.write_bytes(b'bad table'); self.refused()
    def test_stale_preserved_region(self):
        self.esp.data[0x9000] ^= 1; self.refused()
    def test_invalid_image(self):
        self.image.write_bytes(b'invalid'); self.refused()
    def test_oversized_image(self):
        self.image.write_bytes(b'\xe9' + b'\0' * f.APP_SIZE); self.refused()
    def test_verify_failure_leaves_downloader(self):
        self.api.fail_verify = True
        with self.assertRaises(RuntimeError): self.flash()
        self.assertFalse(self.api.reset)
    def test_preservation_failure_leaves_downloader(self):
        self.api.corrupt_preserved = True
        with self.assertRaisesRegex(ValueError, 'Preserved flash changed'): self.flash()
        self.assertFalse(self.api.reset)
    def test_backup_is_verified_private_and_exclusive(self):
        dest = self.manifest.with_name('new-device.json')
        result = f.backup_device(self.esp, self.api, dest)
        self.assertEqual(result['backup'], 'verified')
        self.assertEqual(stat.S_IMODE(dest.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(dest.with_suffix('.bin').stat().st_mode), 0o600)
        with self.assertRaises(ValueError): f.backup_device(self.esp, self.api, dest)
        self.assertEqual(self.api.writes, [])
    def test_corrupt_backup_read_does_not_create_manifest(self):
        dest = self.manifest.with_name('bad-device.json')
        self.esp.flash_md5sum = lambda *args: '0' * 32
        with self.assertRaises(ValueError): f.backup_device(self.esp, self.api, dest)
        self.assertFalse(dest.exists())
        self.assertFalse(dest.with_suffix('.bin').exists())
    def test_partition_checksum_and_fixed_layout(self):
        table = bytearray(self.table)
        table[5] ^= 1
        with self.assertRaises(ValueError): f.validate_table(bytes(table))
        table[256 + 16:256 + 32] = hashlib.md5(table[:256]).digest()
        with self.assertRaises(ValueError): f.validate_table(bytes(table))
    def test_generic_flash_commands_blocked(self):
        for action in ('flash', 'app-flash', 'bootloader-flash', 'partition-table-flash',
                       'encrypted-flash', 'erase-flash', 'erase-otadata', 'merge-bin', 'dfu', 'uf2'):
            with self.subTest(action=action), self.assertRaises(RuntimeError):
                block_unsafe_actions(None, None, [SimpleNamespace(name=action)])
        block_unsafe_actions(None, None, [SimpleNamespace(name='build')])


if __name__ == '__main__':
    unittest.main()
