#!/usr/bin/env python3
"""Build-only 3DS HTTPS/ZIP updater dependencies; never download during build."""
from pathlib import Path
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/3ds'
UPDATER_OUT = OUT / 'updater'
PLATFORM_3DS = ROOT / 'platform/3ds'
REQUIRED_LIBS = (
    'libcurl.a',
    'libmbedtls.a',
    'libmbedx509.a',
    'libmbedcrypto.a',
    'libminizip.a',
    'libz.a',
)


def prefix() -> Path:
    dk = Path(os.environ.get('DEVKITPRO', '/opt/devkitpro'))
    candidates = []
    override = os.environ.get('THIEVIUS_UPDATE_PORTLIBS')
    if override:
        candidates.append(Path(override))
    candidates.extend([
        dk / 'portlibs/3ds',
        ROOT / 'local/update-deps/opt/devkitpro/portlibs/3ds',
    ])
    for cand in candidates:
        if all((cand / 'lib' / lib).is_file() for lib in REQUIRED_LIBS):
            return cand
    raise RuntimeError(
        'Updater libraries missing. Install devkitPro packages 3ds-curl, '
        '3ds-mbedtls, and 3ds-zlib, unpack them into '
        'local/update-deps/opt/devkitpro/portlibs/3ds, or set '
        'THIEVIUS_UPDATE_PORTLIBS.'
    )


def ensure_ca_header() -> Path:
    UPDATER_OUT.mkdir(parents=True, exist_ok=True)
    ca_pem = PLATFORM_3DS / 'vendor/updater/cacert.pem'
    ca_hdr = UPDATER_OUT / 'update_ca.h'
    if ca_hdr.is_file() and ca_hdr.stat().st_mtime >= ca_pem.stat().st_mtime:
        return ca_hdr
    ca_text = ca_pem.read_text()
    content = (
        'static const char cane_update_ca[] =\n'
        + '\n'.join(json.dumps(line + '\n') for line in ca_text.splitlines())
        + ';\n'
    )
    ca_hdr.write_text(content)
    return ca_hdr


def compile_if_needed(compiler: Path, flags: list[str], src: Path, obj: Path, deps: list[Path]) -> None:
    latest = max(p.stat().st_mtime for p in [src, *deps])
    if obj.is_file() and obj.stat().st_mtime >= latest:
        return
    res = subprocess.run(
        [str(compiler), *flags, '-c', str(src), '-o', str(obj)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    (UPDATER_OUT / f'{src.name}.log').write_text(res.stdout)
    if res.returncode != 0:
        print(res.stdout[-6000:])
        raise SystemExit(res.returncode)


def objects() -> list[Path]:
    dk = Path(os.environ.get('DEVKITPRO', '/opt/devkitpro'))
    p = prefix()
    ca_hdr = ensure_ca_header()
    common_flags = [
        '-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft',
        '-O2', '-Wall', '-Wextra',
        '-ffunction-sections', '-fdata-sections',
        '-D__3DS__', '-DCURL_STATICLIB',
        '-I' + str(PLATFORM_3DS),
        '-I' + str(PLATFORM_3DS / 'vendor/updater'),
        '-I' + str(UPDATER_OUT),
        '-I' + str(p / 'include'),
        '-I' + str(dk / 'libctru/include'),
    ]
    c_flags = [*common_flags, '-std=gnu11', '-Werror']
    cpp_flags = [*common_flags, '-std=gnu++20']

    cc = dk / 'devkitARM/bin/arm-none-eabi-gcc'
    cxx = dk / 'devkitARM/bin/arm-none-eabi-g++'

    c_deps = [
        PLATFORM_3DS / 'update_core.h',
        PLATFORM_3DS / 'update_http.h',
        PLATFORM_3DS / 'vendor/updater/jsmn.h',
    ]
    cpp_deps = [
        PLATFORM_3DS / 'update_3ds.h',
        PLATFORM_3DS / 'update_core.h',
        PLATFORM_3DS / 'update_http.h',
        PLATFORM_3DS / 'version_3ds.h',
        PLATFORM_3DS / 'launcher_ui_3ds.h',
        PLATFORM_3DS / 'launcher_graphics.inc',
        ca_hdr,
    ]

    result = []
    for name in ('update_core.c', 'update_http.c'):
        src = PLATFORM_3DS / name
        obj = UPDATER_OUT / (src.stem + '.o')
        compile_if_needed(cc, c_flags, src, obj, c_deps)
        result.append(obj)

    cpp_src = PLATFORM_3DS / 'update_3ds.cpp'
    cpp_obj = UPDATER_OUT / 'update_3ds.o'
    compile_if_needed(cxx, cpp_flags, cpp_src, cpp_obj, cpp_deps)
    result.append(cpp_obj)
    return result


def libraries() -> list[str]:
    p = prefix()
    return [
        '-L' + str(p / 'lib'),
        '-lminizip', '-lcurl', '-lmbedtls', '-lmbedx509', '-lmbedcrypto', '-lz',
    ]
