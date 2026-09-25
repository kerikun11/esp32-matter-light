import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

COMPONENT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ota', COMPONENT / 'tools/ota/ota.py')
ota = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = ota
spec.loader.exec_module(ota)


def image_bytes(project='test-device', version='v2', digest=b'\x12' * 32):
    data = bytearray(512)
    data[0:2] = bytes((0xE9, 1))
    struct.pack_into('<H', data, 12, 13)
    struct.pack_into('<I', data, 32, 0xABCD5432)
    data[48:48 + len(version)] = version.encode()
    data[80:80 + len(project)] = project.encode()
    data[176:208] = digest
    return bytes(data)


class OtaTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / 'app.bin'
        self.path.write_bytes(image_bytes())
        self.image = ota.Image.read(self.path)
        self.info = {'project_name': 'test-device', 'version': 'v1',
                     'target': 'esp32c6', 'elf_sha256': '00' * 32, 'ota_state': 'valid'}
        self.uploads = []
        self.reject = False
        self.reboot = True
        parent = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def do_GET(self):
                self.send_response(200)
                self.end_headers()
                self.wfile.write(json.dumps(parent.info).encode())

            def do_POST(self):
                data = self.rfile.read(int(self.headers['Content-Length']))
                parent.uploads.append(data)
                self.send_response(400 if parent.reject else 200)
                self.end_headers()
                self.wfile.write(b'rejected' if parent.reject else b'OK, rebooting')
                if parent.reboot and not parent.reject:
                    parent.info.update(version='v2', elf_sha256=parent.image.elf_sha256)

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.addCleanup(self.close_server)
        self.device = ota.Device('127.0.0.1:' + str(self.server.server_port), timeout=1)

    def close_server(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()

    def update(self, wait=2):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return ota.update(self.device, self.image, wait)

    def test_upload_and_verify_actual_http(self):
        info = self.update()
        self.assertEqual(self.uploads, [self.image.data])
        self.assertEqual(info['elf_sha256'], self.image.elf_sha256)

    def test_wrong_project_never_uploads(self):
        self.info['project_name'] = 'other-device'
        with self.assertRaisesRegex(ota.OtaError, 'Project mismatch'):
            self.update()
        self.assertEqual(self.uploads, [])

    def test_wrong_chip_never_uploads(self):
        self.info['target'] = 'esp32s3'
        with self.assertRaisesRegex(ota.OtaError, 'Chip target mismatch'):
            self.update()
        self.assertEqual(self.uploads, [])

    def test_identical_image_skips_upload(self):
        self.info['elf_sha256'] = self.image.elf_sha256
        self.update()
        self.assertEqual(self.uploads, [])

    def test_old_or_rolled_back_image_is_not_success(self):
        self.reboot = False
        with self.assertRaises(ota.VerificationError):
            self.update(.03)
        self.assertEqual(len(self.uploads), 1)

    def test_pending_image_is_not_success(self):
        self.info.update(elf_sha256=self.image.elf_sha256, ota_state='pending_verify')
        with self.assertRaises(ota.VerificationError):
            ota.verify(self.device, self.image, .03, .01)

    def test_rejection_is_not_retried(self):
        self.reject = True
        with self.assertRaisesRegex(ota.OtaError, 'HTTP 400'):
            self.update()
        self.assertEqual(len(self.uploads), 1)

    def test_existing_firmware_without_digest_can_be_updated(self):
        del self.info['elf_sha256']
        del self.info['target']
        self.update()
        self.assertEqual(len(self.uploads), 1)

    def test_rejects_non_application_images(self):
        for data in (b'', b'\xff' * 512, bytes((0xE9, 1)) + bytes(510)):
            self.path.write_bytes(data)
            with self.assertRaises(ota.OtaError):
                ota.Image.read(self.path)

    def test_build_metadata_selects_app_not_bootloader(self):
        root = Path(self.tmp.name)
        (root / 'project_description.json').write_text(json.dumps({'app_bin': 'app.bin'}))
        self.assertEqual(ota.find_image(root), self.path)

    def test_invalid_origins(self):
        for value in ('host/update', 'http://user:password@host', 'ftp://host', 'host?skip_check=1'):
            with self.assertRaises(ota.OtaError):
                ota.Device(value)


if __name__ == '__main__':
    unittest.main()
