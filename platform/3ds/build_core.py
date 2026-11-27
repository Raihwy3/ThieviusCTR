#!/usr/bin/env python3
"""Cross-compile original gameplay units, independently of the viewer.

--all checks the runtime beyond the initial character/collision subset.
Desktop entry, audio/video and Windows XAPO are platform-adapter boundaries.
Compilation does not establish a linked or playable 3DS runtime.
"""
from pathlib import Path
import concurrent.futures
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/3ds/core'
OUT.mkdir(parents=True, exist_ok=True)
DK = Path(os.environ.get('DEVKITPRO', '/opt/devkitpro'))
CC = DK / 'devkitARM/bin/arm-none-eabi-g++'
EXCLUDED = {'main', 'sound', 'fmv', 'spu_reverb_xapo'}
INITIAL = ['math', 'util', 'bis', 'bsp', 'geom', 'step', 'jt', 'jump',
           'steprun', 'aseg', 'asega', 'actseg', 'acp', 'acr', 'so', 'alo']
arguments = sys.argv[1:]
replace_archive = bool(arguments and arguments[0] == '--replace')
if replace_archive:
    arguments = arguments[1:]
units = (sorted(p.stem for p in (ROOT / 'Sly1-Release').glob('*.cpp')
                if p.stem not in EXCLUDED)
         if arguments == ['--all'] else arguments or INITIAL)
if arguments == ['--all']:
    units.extend(['input_3ds', 'graphics_loader_3ds', 'audio_ndsp_3ds', 'video_unavailable_3ds'])
flags = ['-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft',
         '-std=gnu++20', '-O3', '-g', '-fomit-frame-pointer', '-flto=auto',
         '-fno-math-errno', '-fno-trapping-math',
         '-ffunction-sections', '-fdata-sections',
         '-fno-strict-aliasing', '-fpermissive', '-D__3DS__', '-DGLM_FORCE_PURE',
         '-DGLFW_INCLUDE_NONE', '-I' + str(ROOT / 'Libraries/include'),
         '-I' + str(ROOT / 'Libraries/include/SDL2'),
         '-I' + str(ROOT / 'platform'),
         '-iquote' + str(ROOT / 'Sly1-Release')]
headers = [ROOT / 'platform/platform.h'] + [ROOT / 'Sly1-Release' / h for h in
    ('sw.h', 'save.h', 'freeze.h', 'act.h', 'wr.h', 'shadow.h', 'fader.h',
     'splicemap.h', 'gc.h')]
pch = OUT / 'core_pch.h'
pch_text = '#include <limits>\n' + ''.join('#include "' + str(h) + '"\n' for h in headers)
if not pch.exists() or pch.read_text() != pch_text:
    pch.write_text(pch_text)
header_hash = hashlib.sha256(repr(flags).encode())
for directory in (ROOT / 'Sly1-Release', ROOT / 'platform', ROOT / 'Libraries/include'):
    for path in sorted(directory.rglob('*')):
        if path.relative_to(ROOT).as_posix().startswith('platform/3ds/vendor/PICASR/'):
            continue  # runtime-only filter dependency; does not affect gameplay objects/PCH
        if path.is_file() and path.suffix in ('.h', '.hpp', '.inl'):
            header_hash.update(str(path.relative_to(ROOT)).encode())
            header_hash.update(path.read_bytes())
header_digest = header_hash.hexdigest()
pch_stamp = OUT / 'pch.sha256'
if not pch.with_suffix('.h.gch').exists() or not pch_stamp.exists() or pch_stamp.read_text() != header_digest:
    result = subprocess.run([str(CC), *flags, '-x', 'c++-header', str(pch), '-o', str(pch) + '.gch'],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (OUT / 'pch.log').write_text(result.stdout)
    if result.returncode:
        print(result.stdout)
        raise SystemExit(result.returncode)
    pch_stamp.write_text(header_digest)
    print('PASS shared precompiled headers', flush=True)

subprocess.run(['cmake', '-DSOURCE=' + str(ROOT / 'Sly1-Release'),
                '-DOUTPUT=' + str(OUT / 'embedded_shaders.h'),
                '-P', str(ROOT / 'cmake/embed-shaders.cmake')], check=True)

from prepare_audio import prepare
audio_shared=prepare(OUT)
from prepare_music import prepare as prepare_music
music_shared=prepare_music(OUT)

def compile_one(name):
    source = ROOT / ('platform/3ds' if name.endswith('_3ds') else 'Sly1-Release') / (name + '.cpp')
    command = [str(CC), *flags]
    if name in ('input_3ds','audio_ndsp_3ds'):
        command += ['-I' + str(DK / 'libctru/include')]
    if name == 'audio_ndsp_3ds':
        command += ['-I'+str(OUT),'-I'+str(ROOT/'platform/3ds/vendor/OpenDSP/library/include')]
    if name == 'glshaders':
        command += ['-DCANE_PORTABLE_SHADERS', '-I' + str(OUT)]
    if name == 'alo':
        # ALO_IMPLEMENTATION changes inline declarations and invalidates the common PCH.
        command += ['-DALO_IMPLEMENTATION', '-include', 'limits']
        for header in headers:
            command += ['-include', str(header)]
    else:
        command += ['-include', str(pch), '-Werror=invalid-pch']
    command += ['-c', str(source), '-o', str(OUT / (name + '.o'))]
    stamp = OUT / (name + '.sha256')
    source_hash = hashlib.sha256((header_digest + repr(command)).encode() + source.read_bytes())
    if name == 'glshaders':
        source_hash.update((OUT / 'embedded_shaders.h').read_bytes())
    if name == 'glob':
        source_hash.update((ROOT/'platform/3ds/model_decode_3ds.inc').read_bytes())
    if name == 'trophies':
        source_hash.update((ROOT/'Sly1-Release/trophy_record.inc').read_bytes())
        source_hash.update((ROOT/'Sly1-Release/trophy_ui_state.inc').read_bytes())
    if name == 'save':
        source_hash.update((ROOT/'platform/3ds/save_storage.inc').read_bytes())
    if name == 'input_3ds':
        source_hash.update((ROOT/'platform/3ds/safe_touch.h').read_bytes())
    if name == 'audio_ndsp_3ds':
        source_hash.update(audio_shared.read_bytes())
        source_hash.update(music_shared.read_bytes())
        source_hash.update((ROOT/"platform/3ds/music_3ds.inc").read_bytes())
        source_hash.update((ROOT/"platform/3ds/trophy_chime.inc").read_bytes())
    digest = source_hash.hexdigest()
    if (OUT / (name + '.o')).exists() and stamp.exists() and stamp.read_text() == digest:
        print('CACHED ' + name, flush=True)
        return dict(unit=name, compiled=True, errors=[])
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (OUT / (name + '.log')).write_text(result.stdout)
    if result.returncode == 0:
        stamp.write_text(digest)
    errors = [line for line in result.stdout.splitlines() if 'error:' in line]
    print(('PASS ' if not result.returncode else 'FAIL ') + name +
          (': ' + errors[0] if errors else ''), flush=True)
    return dict(unit=name, compiled=result.returncode == 0, errors=errors)

with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
    results = list(pool.map(compile_one, units))
(OUT / 'compile-report.json').write_text(json.dumps(results, indent=2) + '\n')
(OUT / 'build-config.json').write_text(json.dumps(dict(flags=flags, units=units, header_digest=header_digest), indent=2) + '\n')
if not all(r['compiled'] for r in results):
    raise SystemExit(1)
# Recreate the archive so a subset build never retains unrelated stale objects.
archive = OUT / 'libcane-gameplay.a'
if not replace_archive:
    archive.unlink(missing_ok=True)
subprocess.run([str(DK / 'devkitARM/bin/arm-none-eabi-gcc-ar'), 'rcs', str(archive),
                *[str(OUT / (r['unit'] + '.o')) for r in results]], check=True)
print('Original gameplay archive:', archive)
