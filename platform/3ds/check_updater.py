#!/usr/bin/env python3
"""Check release parsing, verified TLS transport, and transactional updates with ASan/UBSan."""
from pathlib import Path
import copy
import hashlib
import http.server
import json
import os
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import warnings
import zipfile

ROOT = Path(__file__).resolve().parents[2]
PLATFORM_3DS = ROOT / 'platform/3ds'
OUT = ROOT / 'build/3ds/updater-host'


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    binary = OUT / 'check'
    args = [
        'gcc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie',
        '-DCANE_UPDATE_HOST', '-DCANE_UPDATE_RENAME=cane_test_rename',
        '-include', str(PLATFORM_3DS / 'tests/update_rename.h'),
        '-I' + str(PLATFORM_3DS),
        '-I' + str(PLATFORM_3DS / 'vendor/updater'),
        str(PLATFORM_3DS / 'tests/update_host.c'),
        str(PLATFORM_3DS / 'update_core.c'),
        str(PLATFORM_3DS / 'update_http.c'),
        '-lminizip', '-lz', '-lcurl', '-lcrypto',
        '-o', str(binary),
    ]
    subprocess.run(args, check=True)

    def run(*cmd_args, ok: bool = True, cancel: bool = False) -> str:
        env = {**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'}
        if cancel:
            env['CANE_TEST_CANCEL'] = '1'
        p = subprocess.run([str(binary), *map(str, cmd_args)],
                           capture_output=True, text=True, env=env, timeout=20)
        assert p.returncode == (0 if ok else 1), (cmd_args, p.returncode, p.stdout, p.stderr)
        return p.stdout

    for remote, current, expected in [
        ('v0.1.0-r1', 'v0.1.0', True),
        ('v0.1.0-r1', 'v0.1.0-r1', False),
        ('v0.1.0', 'v0.1.0-r1', False),
        ('v0.2.0', 'v0.1.0', True),
        ('v0.2.0', 'v0.1.0-r9', True),
        ('v0.10.0', 'v0.2.0', True),
        ('v0.1.0', 'v0.2.0', False),
        ('v0.2.0-beta', 'v0.1.0', False),
        ('garbage', 'v0.1.0', False),
    ]:
        run('compare', remote, current, ok=expected)

    with tempfile.TemporaryDirectory(prefix='cane-updater-') as temporary:
        tmp = Path(temporary)
        fixture = tmp / 'release.json'
        release = {
            'tag_name': 'v0.2.0',
            'draft': False,
            'prerelease': False,
            'body': 'text containing "tag_name" and { nested-looking text }',
            'assets': [
                {
                    'name': 'ThieviusCTR-source.zip',
                    'size': 100,
                    'digest': 'sha256:' + 'c' * 64,
                    'browser_download_url': 'https://github.com/Raihwy3/ThieviusCTR/releases/download/v0.2.0/ThieviusCTR-source.zip',
                },
                {
                    'name': 'ThieviusCTR-v0.2.0.zip',
                    'size': 1024,
                    'digest': 'sha256:' + 'a' * 64,
                    'browser_download_url': 'https://github.com/Raihwy3/ThieviusCTR/releases/download/v0.2.0/ThieviusCTR-v0.2.0.zip',
                },
            ],
        }

        def parse(d: dict, ok: bool) -> str:
            fixture.write_text(json.dumps(d))
            return run('release', fixture, 'v0.1.0', ok=ok)

        out_lines = parse(release, True).splitlines()
        assert out_lines[0] == 'v0.2.0'
        assert out_lines[1].endswith('ThieviusCTR-v0.2.0.zip')

        mixed = copy.deepcopy(release)
        cia_asset = {'name': 'ThieviusCTR-v0.2.0.cia', 'size': 1024,
                     'digest': 'sha256:' + 'b' * 64,
                     'browser_download_url': 'https://github.com/Raihwy3/ThieviusCTR/releases/download/v0.2.0/ThieviusCTR-v0.2.0.cia'}
        mixed['assets'].insert(0, cia_asset)
        fixture.write_text(json.dumps(mixed))
        assert run('release-cia', fixture, 'v0.1.9-r1').splitlines()[1].endswith('.cia')
        assert run('release', fixture, 'v0.1.9-r1').splitlines()[1].endswith('.zip')
        fixture.write_text(json.dumps(release))
        run('release-cia', fixture, 'v0.1.9-r1', ok=False)
        for key, value in [('digest', 'sha256:bad'), ('size', 25 * 1024 * 1024),
                           ('browser_download_url', 'https://github.com/other/repo/update.cia')]:
            bad = copy.deepcopy(mixed); bad['assets'][0][key] = value
            fixture.write_text(json.dumps(bad)); run('release-cia', fixture, 'v0.1.9', ok=False)
        def cia_file(title):
            header = bytearray(0x2020)
            struct.pack_into('<IHHIIIIQ', header, 0, 0x2020, 0, 0, 64, 64, 64, 0, 512)
            ncch = bytearray(512); ncch[0x100:0x104] = b'NCCH'
            struct.pack_into('<Q', ncch, 0x118, title)
            return bytes(header) + bytes(224) + ncch
        good_cia = tmp / 'good.cia'; good_cia.write_bytes(cia_file(0x000400000E054C00))
        run('cia', good_cia)
        bad_cia = tmp / 'wrong-title.cia'; bad_cia.write_bytes(cia_file(0x000400000E064D00))
        run('cia', bad_cia, ok=False)
        for name, payload in [('truncated', good_cia.read_bytes()[:-1]), ('junk', b'x' * 1024)]:
            bad_cia.write_bytes(payload); run('cia', bad_cia, ok=False)

        for key in ('draft', 'prerelease'):
            bad = copy.deepcopy(release)
            bad[key] = True
            parse(bad, False)

        for key, value in [
            ('digest', 'sha256:bad'),
            ('size', 25 * 1024 * 1024),
            ('browser_download_url', 'http://github.com/evil.zip'),
            ('browser_download_url', 'https://github.com/other/repo/releases/download/v0.2.0/ThieviusCTR-v0.2.0.zip'),
        ]:
            bad = copy.deepcopy(release)
            bad['assets'][1][key] = value
            parse(bad, False)

        fixture.write_text('{"tag_name":')
        run('release', fixture, 'v0.1.0', ok=False)
        fixture.write_text('[]')
        run('release', fixture, 'v0.1.0', ok=False)
        fixture.write_text(json.dumps(release) + '{}')
        run('release', fixture, 'v0.1.0', ok=False)

        folder = '3ds/ThieviusCTR/'
        names = ['ThieviusCTR.3dsx', 'ThieviusCTR.smdh']
        old = [b'old-launcher', b'old-icon']
        launcher = bytearray(1024)
        struct.pack_into('<4sHH', launcher, 0, b'3DSX', 32, 8)
        icon = b'SMDH' + bytes(0x36c0 - 4)
        archive = tmp / 'update.zip'

        def zipwrite(payloads=(launcher, icon), extra=()):
            with warnings.catch_warnings():
                warnings.simplefilter('ignore')
                with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
                    for name, data in zip(names, payloads):
                        z.writestr(folder + name, data)
                    for name, data in extra:
                        z.writestr(name, data)

        keep_files = [
            'Slygame.iso',
            'native-options.cfg',
            'audio-backend.cfg',
            'extraction_manifest.json',
        ]

        def reset() -> Path:
            d = tmp / 'install'
            d.mkdir(exist_ok=True)
            for p in d.iterdir():
                p.unlink()
            for name, data in zip(names, old):
                (d / name).write_bytes(data)
            for name in keep_files:
                (d / name).write_bytes(b'keep ' + name.encode())
            return d

        def unchanged(d: Path) -> None:
            for name in keep_files:
                assert (d / name).read_bytes() == b'keep ' + name.encode()

        zipwrite(extra=[('../../outside', b'bad'), (folder + 'native-options.cfg', b'bad')])
        d = reset()
        run('install', archive, d)
        assert (d / names[0]).read_bytes() == launcher and (d / names[1]).read_bytes() == icon
        unchanged(d)
        assert not (tmp / 'outside').exists()

        for failure in range(1, 5):
            d = reset()
            run('install', archive, d, failure, ok=False)
            for name, data in zip(names, old):
                assert (d / name).read_bytes() == data
            unchanged(d)
            assert not (d / 'launcher-update.pending').exists()

        d = reset()
        run('install', archive, d, 0, 1, ok=False)
        assert (d / names[0]).read_bytes() == old[0]

        zipwrite(payloads=(b'not 3DSX' * 64, icon))
        d = reset()
        run('install', archive, d, ok=False)
        assert (d / names[0]).read_bytes() == old[0]

        zipwrite(extra=[(folder + names[0], launcher)])
        d = reset()
        run('install', archive, d, ok=False)

        d = reset()
        (d / 'launcher-update.pending').write_bytes(b'TCTU\x03')
        (d / (names[0] + '.update-old')).write_bytes(old[0])
        (d / names[0]).write_bytes(launcher)
        run('recover', d)
        for name, data in zip(names, old):
            assert (d / name).read_bytes() == data

        zipwrite()
        zip_bytes = archive.read_bytes()
        digest = hashlib.sha256(zip_bytes).hexdigest()

        cert = tmp / 'server.pem'
        key = tmp / 'server.key'
        subprocess.run(
            ['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
             '-keyout', str(key), '-out', str(cert), '-days', '1',
             '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost'],
            check=True, capture_output=True,
        )

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                if self.path == '/downgrade':
                    self.send_response(302)
                    self.send_header('Location', 'http://localhost/')
                    self.end_headers()
                    return
                data = b'x' * 8192 if self.path == '/oversize' else b'{}' if self.path == '/metadata' else zip_bytes
                self.send_response(200)
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)

        class Server(http.server.ThreadingHTTPServer):
            def handle_error(self, request, address):
                if not isinstance(sys.exception(), (ConnectionResetError, BrokenPipeError)):
                    super().handle_error(request, address)

        server = Server(('127.0.0.1', 0), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(str(cert), str(key))
        server.socket = context.wrap_socket(server.socket, server_side=True)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        url = f'https://localhost:{server.server_port}'
        try:
            assert '{}' in run('fetch', url + '/metadata', cert, '-', 0, '-')
            part = tmp / 'download.part'
            run('fetch', url + '/zip', cert, part, len(zip_bytes), digest)
            assert part.read_bytes() == zip_bytes
            run('fetch', url + '/zip', cert, part, len(zip_bytes), '0' * 64, ok=False)
            assert not part.exists()
            run('fetch', url + '/zip', cert, part, len(zip_bytes) - 1, digest, ok=False)
            assert not part.exists()
            run('fetch', url + '/downgrade', cert, '-', 0, '-', ok=False)
            run('fetch', url + '/metadata', PLATFORM_3DS / 'vendor/updater/cacert.pem', '-', 0, '-', ok=False)
            run('fetch', url + '/oversize', cert, '-', 0, '-', ok=False)
            run('fetch', url.replace('localhost', '127.0.0.1') + '/metadata', cert, '-', 0, '-', ok=False)
            run('fetch', url + '/zip', cert, part, len(zip_bytes), digest, ok=False, cancel=True)
            assert not part.exists()
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    print('PASS updater: revisions, GitHub asset selection, verified TLS, HTTPS-only redirects, SHA-256/size rejection, ZIP validation, cancellation, four replacement failures, recovery and preserved game files')


if __name__ == '__main__':
    main()
