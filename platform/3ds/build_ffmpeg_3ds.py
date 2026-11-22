#!/usr/bin/env python3
"""Download and cross-compile minimal LGPL FFmpeg 7.1.1 (Bink + MPEG-2) for devkitARM / 3DS."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
FFMPEG_VERSION = '7.1.1'
FFMPEG_URL = f'https://ffmpeg.org/releases/ffmpeg-{FFMPEG_VERSION}.tar.xz'
STAMP_VERSION = 'ffmpeg-3ds-v2'


def ensure_tarball(tarball: Path) -> None:
    if tarball.is_file() and tarball.stat().st_size > 1_000_000:
        return
    tarball.parent.mkdir(parents=True, exist_ok=True)
    tmp = tarball.with_suffix('.xz.tmp')
    tmp.unlink(missing_ok=True)
    print(f'Downloading {FFMPEG_URL}...', flush=True)
    if shutil.which('curl'):
        subprocess.run(['curl', '-fL', '--retry', '3', '-o', str(tmp), FFMPEG_URL], check=True)
    else:
        urllib.request.urlretrieve(FFMPEG_URL, tmp)
    tmp.replace(tarball)


def ensure_source(tarball: Path, src_dir: Path) -> None:
    if (src_dir / 'configure').is_file():
        return
    tmp_dir = src_dir.with_name(src_dir.name + '.tmp')
    shutil.rmtree(tmp_dir, ignore_errors=True)
    tmp_dir.mkdir(parents=True, exist_ok=True)
    print(f'Extracting {tarball.name}...', flush=True)
    subprocess.run(['tar', '-xf', str(tarball), '--strip-components=1', '-C', str(tmp_dir)], check=True)
    if not (tmp_dir / 'configure').is_file():
        raise RuntimeError(f'Missing configure in extracted FFmpeg source: {tmp_dir}')
    shutil.rmtree(src_dir, ignore_errors=True)
    tmp_dir.replace(src_dir)


def patch_bink_demuxer(src_dir: Path) -> None:
    bink_c = src_dir / 'libavformat/bink.c'
    text = bink_c.read_text()
    if 'discard >= AVDISCARD_ALL' in text:
        return
    target = (
        '        if (audio_size >= 4) {\n'
        '            /* get one audio packet per track */\n'
        '            if ((ret = av_get_packet(pb, pkt, audio_size)) < 0)\n'
        '                return ret;'
    )
    replacement = (
        '        if (audio_size >= 4) {\n'
        '            if (s->streams[bink->current_track]->discard >= AVDISCARD_ALL) {\n'
        '                avio_skip(pb, audio_size);\n'
        '                continue;\n'
        '            }\n'
        '            /* get one audio packet per track */\n'
        '            if ((ret = av_get_packet(pb, pkt, audio_size)) < 0)\n'
        '                return ret;'
    )
    if target not in text:
        raise RuntimeError(f'Expected bink.c audio skip target not found in {bink_c}')
    bink_c.write_text(text.replace(target, replacement, 1))


def main() -> Path:
    dk = Path(os.environ.get('DEVKITPRO', '/opt/devkitpro'))
    cross_prefix = str(dk / 'devkitARM/bin/arm-none-eabi-')
    base = ROOT / 'build/3ds/ffmpeg'
    tarball = base / f'ffmpeg-{FFMPEG_VERSION}.tar.xz'
    src_dir = base / 'src'
    obj_dir = base / 'obj'
    install_dir = base / 'install'
    stamp = install_dir / '.ffmpeg-3ds-v2'
    libs = [install_dir / 'lib' / f'lib{name}.a' for name in ('avformat', 'avcodec', 'avutil')]

    if (
        stamp.is_file()
        and stamp.read_text().strip() == STAMP_VERSION
        and all(lib.is_file() for lib in libs)
        and (install_dir / 'include/libavcodec/avcodec.h').is_file()
    ):
        return install_dir

    ensure_tarball(tarball)
    ensure_source(tarball, src_dir)
    patch_bink_demuxer(src_dir)

    shutil.rmtree(obj_dir, ignore_errors=True)
    shutil.rmtree(install_dir, ignore_errors=True)
    obj_dir.mkdir(parents=True, exist_ok=True)
    install_dir.mkdir(parents=True, exist_ok=True)

    env = {
        **os.environ,
        'PATH': f"{dk / 'devkitARM/bin'}:{os.environ.get('PATH', '')}",
    }

    extra_cflags = (
        '-march=armv6k -mtune=mpcore -mfpu=vfp -mfloat-abi=hard -mtp=soft '
        '-O3 -ffunction-sections -fdata-sections -Wno-error=incompatible-pointer-types '
        '-D__3DS__ '
        f'-I{dk}/libctru/include'
    )
    extra_ldflags = (
        '-march=armv6k -mtune=mpcore -mfpu=vfp -mfloat-abi=hard -mtp=soft '
        f'-specs=3dsx.specs -L{dk}/libctru/lib'
    )

    configure_cmd = [
        str(src_dir / 'configure'),
        f'--prefix={install_dir}',
        '--target-os=none',
        '--arch=arm',
        '--cpu=mpcore',
        '--enable-cross-compile',
        f'--cross-prefix={cross_prefix}',
        '--enable-static',
        '--disable-shared',
        '--disable-programs',
        '--disable-doc',
        '--disable-everything',
        '--disable-network',
        '--disable-protocols',
        '--disable-pthreads',
        '--disable-w32threads',
        '--disable-os2threads',
        '--disable-avdevice',
        '--disable-avfilter',
        '--disable-swscale',
        '--disable-swresample',
        '--disable-autodetect',
        '--disable-bzlib',
        '--disable-iconv',
        '--disable-lzma',
        '--disable-zlib',
        '--disable-debug',
        '--enable-avutil',
        '--enable-avcodec',
        '--enable-avformat',
        '--enable-decoder=bink,binkaudio_dct,binkaudio_rdft,mpeg2video',
        '--enable-demuxer=bink,mpegvideo',
        '--enable-parser=mpegvideo',
        '--enable-armv6',
        '--disable-armv6t2',
        '--disable-vfpv3',
        '--disable-neon',
        '--enable-vfp',
        '--enable-asm',
        f'--extra-cflags={extra_cflags}',
        f'--extra-ldflags={extra_ldflags}',
        '--extra-libs=-lctru -lm',
    ]

    print('Configuring minimal LGPL FFmpeg for 3DS ARMv6K...', flush=True)
    res = subprocess.run(configure_cmd, cwd=obj_dir, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (base / 'configure.log').write_text(res.stdout)
    if res.returncode != 0:
        print(res.stdout[-6000:])
        raise SystemExit(res.returncode)

    jobs = str(os.cpu_count() or 4)
    print(f'Building FFmpeg libs (-j{jobs})...', flush=True)
    res = subprocess.run(['make', f'-j{jobs}'], cwd=obj_dir, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (base / 'build.log').write_text(res.stdout)
    if res.returncode != 0:
        print(res.stdout[-6000:])
        raise SystemExit(res.returncode)

    subprocess.run(['make', 'install-libs', 'install-headers'], cwd=obj_dir, env=env, check=True)
    missing = [str(lib) for lib in libs if not lib.is_file()]
    if missing:
        raise RuntimeError(f'Missing built FFmpeg libraries: {missing}')

    stamp.write_text(STAMP_VERSION + '\n')
    print(f'Built minimal FFmpeg libraries in {install_dir}', flush=True)
    return install_dir


if __name__ == '__main__':
    main()
