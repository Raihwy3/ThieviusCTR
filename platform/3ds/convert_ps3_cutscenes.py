#!/usr/bin/env python3
"""Convert PS3 HD Collection Sly 1 .bik cutscenes into 3DS CMV2 + 16 kHz mono WAV."""
from pathlib import Path
import argparse
import shutil
import struct
import subprocess
import sys
import time

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
from extract_intro import ensure_encoder, is_valid_cmv2

ROOT = Path(__file__).resolve().parents[2]

# Cutscene index mapping matching Sly1-Release/fmv.cpp (00..17).
# Widescreen 16:9 movies sit inside 640x480 letterboxed frames (crop 640:360:0:60)
# and encode at 640x224 for 3DS 800x240 wide mode (1:1 horizontal source pixels).
# Full-frame 4:3 extras/credits/secret_origin_alt encode at 640x240 (exact 4:3 in wide mode).
CUTSCENE_MAP = {
    0:  ('secret_origin.bik',     True),
    1:  ('secret_origin_alt.bik', False),
    2:  ('making_of.bik',         False),
    3:  ('outtakes.bik',          False),
    4:  ('underwater_intro.bik',  True),
    5:  ('underwater_outro.bik',  True),
    6:  ('muggshot_intro.bik',    True),
    7:  ('muggshot_outro.bik',    True),
    8:  ('voodoo_intro.bik',      True),
    9:  ('voodoo_outro.bik',      True),
    10: ('snow_intro.bik',        True),
    11: ('snow_outro.bik',        True),
    12: ('clockwerk_intro.bik',   True),
    13: ('ending_1.bik',          True),
    14: ('ending_2.bik',          True),
    15: ('credits.bik',           False),
    16: ('splash.bik',            True),
    17: ('splash.bik',            True),
}


def probe_native_fps(src: Path) -> int:
    raw = subprocess.check_output(
        ['ffprobe', '-v', 'error', '-select_streams', 'v:0',
         '-show_entries', 'stream=r_frame_rate', '-of', 'default=nw=1:nk=1', str(src)],
        text=True,
    ).strip()
    num_s, _, den_s = raw.partition('/')
    num = int(num_s)
    den = int(den_s) if den_s else 1
    if den <= 0 or num <= 0:
        raise RuntimeError(f'Invalid source frame rate {raw!r} for {src.name}')
    if num % den == 0:
        fps = num // den
    else:
        fps = round(num * 1000 / den)
    if not ((1 <= fps <= 60) or (1000 <= fps <= 60000)):
        raise RuntimeError(f'Unsupported native FPS {fps} ({raw}) for {src.name}')
    return fps


def format_fps(fps: int) -> str:
    return f'{fps / 1000:.4g}fps' if fps >= 1000 else f'{fps}fps'


def convert_one(encoder: Path, src: Path, index: int, widescreen: bool,
                override_fps: int | None, audio_track: int, output: Path, force: bool) -> None:
    stem = f'Cutscene_{index:02d}'
    cmov_path = output / (stem + '.cmov')
    wav_path = output / (stem + '.wav')
    if not force and is_valid_cmv2(cmov_path) and wav_path.is_file() and wav_path.stat().st_size > 44:
        print(f'Existing: {stem}', flush=True)
        return

    if not src.is_file():
        raise SystemExit(f'Missing PS3 cutscene source: {src}')

    width, height = (640, 224) if widescreen else (640, 240)
    fps = override_fps if override_fps is not None else probe_native_fps(src)
    vf = f'crop=640:360:0:60,scale={width}:{height}:flags=lanczos' if widescreen else f'scale={width}:{height}:flags=lanczos'
    if override_fps is not None:
        fps_expr = f'{fps / 1000:.6f}' if fps >= 1000 else str(fps)
        vf += f',fps={fps_expr}'

    print(f'Converting {stem} <- {src.name} ({width}x{height} @ {format_fps(fps)})...', flush=True)

    wav_tmp = wav_path.with_suffix('.wav.tmp')
    wav_tmp.unlink(missing_ok=True)
    subprocess.run(
        ['ffmpeg', '-v', 'error', '-y', '-i', str(src),
         '-map', f'0:a:{audio_track}', '-vn', '-ac', '1', '-ar', '16000',
         '-c:a', 'pcm_s16le', '-f', 'wav', str(wav_tmp)],
        check=True,
    )
    if not wav_tmp.is_file() or wav_tmp.stat().st_size <= 44:
        wav_tmp.unlink(missing_ok=True)
        raise RuntimeError(f'Empty/invalid audio output for {stem}')
    wav_tmp.replace(wav_path)

    cmov_tmp = cmov_path.with_suffix('.cmov.tmp')
    cmov_tmp.unlink(missing_ok=True)
    ff_cmd = ['ffmpeg', '-v', 'error', '-i', str(src), '-vf', vf]
    if override_fps is None:
        ff_cmd += ['-fps_mode', 'passthrough']
    ff_cmd += ['-f', 'rawvideo', '-pix_fmt', 'rgb24', '-']
    ff = subprocess.Popen(ff_cmd, stdout=subprocess.PIPE)
    try:
        subprocess.run([str(encoder), str(cmov_tmp), str(width), str(height), str(fps)],
                       stdin=ff.stdout, check=True)
    finally:
        if ff.stdout:
            ff.stdout.close()
    if ff.wait():
        cmov_tmp.unlink(missing_ok=True)
        raise RuntimeError(f'ffmpeg decode failed for {stem}')
    if not is_valid_cmv2(cmov_tmp):
        cmov_tmp.unlink(missing_ok=True)
        raise RuntimeError(f'Generated invalid CMV2 for {stem}')
    cmov_tmp.replace(cmov_path)

    with cmov_path.open('rb') as fp:
        _, w, h, f, nf, _ = struct.unpack('<4s5I', fp.read(24))
    print(f'  {stem}: {nf} frames {w}x{h}@{format_fps(f)} ({cmov_path.stat().st_size / 1048576:.1f} MiB cmov, '
          f'{wav_path.stat().st_size / 1048576:.2f} MiB wav)', flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ps3_sly1_dir', type=Path, help='PS3_GAME/USRDIR/Sly1 directory containing .bik files')
    parser.add_argument('--output', type=Path, default=ROOT / 'local/data/Cutscenes',
                        help='Output Cutscenes directory (default: local/data/Cutscenes)')
    parser.add_argument('--fps', type=int, default=None,
                        help='Override FPS (1..60, or milli-fps 1000..60000; default: keep source FPS)')
    parser.add_argument('--audio-track', type=int, default=0, help='Audio track index inside .bik (0=English)')
    parser.add_argument('--index', action='append', type=int, help='Repeatable cutscene index (0..17)')
    parser.add_argument('--force', action='store_true', help='Overwrite existing Cutscene_XX files')
    args = parser.parse_args()

    if args.fps is not None and not ((1 <= args.fps <= 60) or (1000 <= args.fps <= 60000)):
        parser.error('--fps must be 1..60 or 1000..60000 (milli-fps)')
    if not shutil.which('ffmpeg') or not shutil.which('ffprobe'):
        raise SystemExit('Missing required tools: ffmpeg and ffprobe')

    src_dir = args.ps3_sly1_dir.resolve()
    if not src_dir.is_dir():
        raise SystemExit(f'PS3 Sly1 directory not found: {src_dir}')

    selected = sorted(set(args.index) if args.index else range(18))
    if any(i < 0 or i > 17 for i in selected):
        raise SystemExit('Cutscene index must be between 0 and 17')

    encoder = ensure_encoder()
    out_dir = args.output.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    t0 = time.monotonic()
    for idx in selected:
        if idx == 17 and 16 in selected and is_valid_cmv2(out_dir / 'Cutscene_16.cmov') and (out_dir / 'Cutscene_16.wav').is_file():
            c17 = out_dir / 'Cutscene_17.cmov'
            w17 = out_dir / 'Cutscene_17.wav'
            if not args.force and is_valid_cmv2(c17) and w17.is_file() and w17.stat().st_size > 44:
                print('Existing: Cutscene_17', flush=True)
            else:
                for ext in ('.cmov', '.wav'):
                    shutil.copy2(out_dir / ('Cutscene_16' + ext), out_dir / ('Cutscene_17' + ext))
                print('Copied Cutscene_17 from Cutscene_16 (splash.bik)', flush=True)
            continue
        bik_name, widescreen = CUTSCENE_MAP[idx]
        convert_one(encoder, src_dir / bik_name, idx, widescreen, args.fps, args.audio_track, out_dir, args.force)

    total_bytes = sum((out_dir / f'Cutscene_{i:02d}{ext}').stat().st_size
                      for i in selected for ext in ('.cmov', '.wav')
                      if (out_dir / f'Cutscene_{i:02d}{ext}').is_file())
    print(f'\nConverted {len(selected)} PS3 cutscenes to {out_dir} '
          f'({total_bytes / 1048576:.1f} MiB in {time.monotonic() - t0:.1f}s)', flush=True)


if __name__ == '__main__':
    main()
