#!/usr/bin/env python3
"""Inspect and update an ESP-IDF device through /version and /update."""
import argparse
from dataclasses import dataclass
import http.client
import json
from pathlib import Path
import struct
import sys
import time
from urllib.parse import urlsplit


class OtaError(Exception):
    pass


class VerificationError(OtaError):
    pass


@dataclass(frozen=True)
class Image:
    data: bytes
    project_name: str
    version: str
    elf_sha256: str
    chip_id: int

    @classmethod
    def read(cls, path):
        data = Path(path).read_bytes()
        # esp_image_header_t (24) + first esp_image_segment_header_t (8).
        if len(data) < 288 or data[0] != 0xE9 or not 1 <= data[1] <= 16:
            raise OtaError('Not an ESP application image (use the app .bin, not a merged image).')
        if struct.unpack_from('<I', data, 32)[0] != 0xABCD5432:
            raise OtaError('ESP application descriptor is missing.')
        def string(offset):
            return data[offset:offset + 32].split(b'\0', 1)[0].decode('utf-8')
        project, version = string(80), string(48)
        digest = data[176:208].hex()
        if not project or digest == '00' * 32:
            raise OtaError('Application descriptor has no project name or ELF digest.')
        return cls(data, project, version, digest, struct.unpack_from('<H', data, 12)[0])


def find_image(build_dir):
    build_dir = Path(build_dir)
    description = json.loads((build_dir / 'project_description.json').read_text())
    app_bin = description.get('app_bin')
    if not app_bin:
        raise OtaError('Build metadata has no app_bin; run idf.py build first.')
    path = Path(app_bin)
    return path if path.is_absolute() else build_dir / path


class Device:
    def __init__(self, host, timeout=20):
        url = urlsplit(host if '://' in host else 'http://' + host)
        if url.scheme not in ('http', 'https') or not url.hostname or url.path not in ('', '/') or url.query or url.fragment or url.username or url.password:
            raise OtaError('Specify a hostname or an HTTP(S) origin without a path or credentials.')
        self.host, self.port, self.scheme, self.timeout = url.hostname, url.port, url.scheme, timeout

    def connection(self, timeout=None):
        cls = http.client.HTTPSConnection if self.scheme == 'https' else http.client.HTTPConnection
        return cls(self.host, self.port, timeout=self.timeout if timeout is None else timeout)

    def info(self, timeout=None):
        conn = self.connection(timeout)
        try:
            conn.request('GET', '/version', headers={'Accept': 'application/json'})
            response = conn.getresponse()
            body = response.read(65536)
            if response.status != 200:
                raise OtaError(f'GET /version: HTTP {response.status}: {body.decode(errors="replace")}')
            info = json.loads(body)
            if not isinstance(info, dict) or not isinstance(info.get('project_name'), str):
                raise OtaError('Invalid /version response.')
            return info
        finally:
            conn.close()

    def upload(self, image):
        conn = self.connection()
        try:
            conn.putrequest('POST', '/update')
            conn.putheader('Content-Type', 'application/octet-stream')
            conn.putheader('Content-Length', str(len(image.data)))
            conn.endheaders()
            for offset in range(0, len(image.data), 65536):
                chunk = image.data[offset:offset + 65536]
                conn.send(chunk)
                print(f'\rUploading: {(offset + len(chunk)) * 100 // len(image.data):3d}%', end='', file=sys.stderr, flush=True)
            print(file=sys.stderr)
            response = conn.getresponse()
            message = response.read(65536).decode(errors='replace').strip()
            if response.status != 200:
                raise OtaError(f'POST /update: HTTP {response.status}: {message}')
            print(message)
        finally:
            conn.close()


def verify(device, image, wait_seconds, interval=1):
    deadline = time.monotonic() + wait_seconds
    last_result = 'Device has not responded.'
    while time.monotonic() < deadline:
        try:
            info = device.info(timeout=min(device.timeout, max(.1, deadline - time.monotonic())))
            if info.get('project_name') == image.project_name and info.get('elf_sha256') == image.elf_sha256:
                if info.get('ota_state') == 'pending_verify':
                    last_result = 'New image is running but has not been confirmed.'
                else:
                    return info
            else:
                last_result = 'Device reports a different image (old firmware or rollback).'
        except (OSError, http.client.HTTPException, ValueError, OtaError) as error:
            last_result = str(error)
        time.sleep(min(interval, max(0, deadline - time.monotonic())))
    raise VerificationError(f'Upload accepted, but boot verification timed out: {last_result}')


def update(device, image, wait_seconds=60):
    before = device.info()
    print(f'Device: {before["project_name"]} {before.get("version", "unknown")}')
    print(f'Image:  {image.project_name} {image.version} ({image.elf_sha256[:16]})')
    if before['project_name'] != image.project_name:
        raise OtaError('Project mismatch; no firmware was sent.')
    chip_ids = {'esp32': 0, 'esp32s2': 2, 'esp32c3': 5, 'esp32s3': 9, 'esp32c6': 13}
    target = before.get('target')
    if target in chip_ids and chip_ids[target] != image.chip_id:
        raise OtaError('Chip target mismatch; no firmware was sent.')
    if before.get('elf_sha256') == image.elf_sha256 and before.get('ota_state') != 'pending_verify':
        print('The requested image is already running.')
        return before
    device.upload(image)
    info = verify(device, image, wait_seconds)
    print(f'Boot verified: {info["project_name"]} {info.get("version", "")} ({image.elf_sha256[:16]})')
    return info


def positive(value):
    result = float(value)
    if not 0 < result < float('inf'):
        raise argparse.ArgumentTypeError('must be a positive finite number')
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest='command', required=True)
    for name in ('info', 'update'):
        sub = subparsers.add_parser(name)
        sub.add_argument('--host', required=True)
        sub.add_argument('--timeout', type=positive, default=20, help='HTTP timeout in seconds')
        if name == 'update':
            sub.add_argument('--file', type=Path)
            sub.add_argument('--build-dir', type=Path, default=Path('build'))
            sub.add_argument('--wait', type=positive, default=60, help='Boot verification timeout in seconds')
    args = parser.parse_args(argv)
    try:
        device = Device(args.host, args.timeout)
        if args.command == 'info':
            print(json.dumps(device.info(), ensure_ascii=False, indent=2))
        else:
            image = Image.read(args.file or find_image(args.build_dir))
            update(device, image, args.wait)
        return 0
    except VerificationError as error:
        print(f'OTA: {error}', file=sys.stderr)
        return 3
    except (OtaError, OSError, http.client.HTTPException, ValueError, KeyError) as error:
        print(f'OTA: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
