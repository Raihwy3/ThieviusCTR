#!/usr/bin/env python3
"""Extract all US Sly 1 levels, audio, and 3DS cutscenes from ISO and stage 3ds/ThieviusCTR/."""
from pathlib import Path
import argparse
import json
import os
import shutil
import subprocess
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SCRIPT_DIR = Path(__file__).resolve().parent


def fmt_duration(seconds: float) -> str:
    total = int(round(seconds))
    mins, secs = divmod(total, 60)
    hours, mins = divmod(mins, 60)
    if hours:
        return f"{hours}h {mins:02d}m {secs:02d}s"
    if mins:
        return f"{mins}m {secs:02d}s"
    return f"{seconds:.1f}s"


def fmt_size(num_bytes: int) -> str:
    if num_bytes >= 1024 ** 3:
        return f"{num_bytes / (1024 ** 3):.2f} GiB"
    if num_bytes >= 1024 ** 2:
        return f"{num_bytes / (1024 ** 2):.1f} MiB"
    return f"{num_bytes / 1024:.1f} KiB"


def sync_tree(src_root: Path, dst_root: Path) -> None:
    if not src_root.is_dir():
        return
    expected = set()
    for src in src_root.rglob('*'):
        rel = src.relative_to(src_root)
        expected.add(rel)
        dst = dst_root / rel
        if src.is_dir():
            dst.mkdir(parents=True, exist_ok=True)
        elif src.is_file():
            dst.parent.mkdir(parents=True, exist_ok=True)
            src_st = src.stat()
            if dst.exists() and not dst.is_symlink():
                dst_st = dst.stat()
                if dst_st.st_ino == src_st.st_ino and dst_st.st_dev == src_st.st_dev:
                    continue
                if dst_st.st_size == src_st.st_size and int(dst_st.st_mtime) == int(src_st.st_mtime):
                    try:
                        dst.unlink()
                        os.link(src, dst)
                        continue
                    except OSError:
                        continue
                dst.unlink()
            elif dst.is_symlink():
                dst.unlink()
            try:
                os.link(src, dst)
            except OSError:
                shutil.copy2(src, dst)
    if dst_root.exists():
        for existing in sorted(dst_root.rglob('*'), reverse=True):
            rel = existing.relative_to(dst_root)
            if rel not in expected:
                if existing.is_file() or existing.is_symlink():
                    existing.unlink()
                elif existing.is_dir():
                    shutil.rmtree(existing, ignore_errors=True)


def run_step(label: str, cmd: list[str]) -> float:
    print(f"\n=== {label} ===", flush=True)
    t0 = time.monotonic()
    subprocess.run(cmd, check=True)
    dt = time.monotonic() - t0
    print(f"--- {label} finished in {fmt_duration(dt)} ---", flush=True)
    return dt


def count_valid_cutscenes(cutscenes_dir: Path) -> int:
    if not cutscenes_dir.is_dir():
        return 0
    count = 0
    for i in range(18):
        cmov = cutscenes_dir / f"Cutscene_{i:02d}.cmov"
        wav = cutscenes_dir / f"Cutscene_{i:02d}.wav"
        if cmov.is_file() and cmov.stat().st_size > 24 and wav.is_file() and wav.stat().st_size > 44:
            count += 1
    return count


def audit_output(output: Path, expect_cutscenes: bool) -> dict:
    worlds = output / 'Worlds'
    brx_files = sorted(worlds.rglob('*.brx'))
    bnk_files = sorted((worlds / 'Sound Banks').glob('*.bnk'))
    mus_files = sorted((worlds / 'Music').glob('*.mus'))
    dialog_files = sorted(worlds.rglob('Dialog/*.wav'))
    comm_files = sorted((worlds / 'Sounds/Commentary').glob('*.wav'))
    amb_files = sorted((worlds / 'Sounds/JLO Ambient').glob('*.wav'))
    prompt_file = worlds / 'Sounds/Prompt/PromptPrk18.wav'
    cutscene_count = count_valid_cutscenes(output / 'Cutscenes')

    errors = []
    if len(brx_files) != 46:
        errors.append(f"expected 46 BRX levels, found {len(brx_files)}")
    if len(bnk_files) != 7:
        errors.append(f"expected 7 sound banks, found {len(bnk_files)}")
    if len(mus_files) != 38:
        errors.append(f"expected 38 music tracks, found {len(mus_files)}")
    if not (worlds / 'Music/MusicMap.txt').is_file():
        errors.append("missing Worlds/Music/MusicMap.txt")
    if len(dialog_files) != 1368:
        errors.append(f"expected 1368 dialog WAVs, found {len(dialog_files)}")
    if len(comm_files) != 33:
        errors.append(f"expected 33 commentary WAVs, found {len(comm_files)}")
    if not (worlds / 'Sounds/Commentary/CommentaryMap.txt').is_file():
        errors.append("missing Worlds/Sounds/Commentary/CommentaryMap.txt")
    if len(amb_files) != 6:
        errors.append(f"expected 6 JLO Ambient WAVs, found {len(amb_files)}")
    if not prompt_file.is_file():
        errors.append("missing Worlds/Sounds/Prompt/PromptPrk18.wav")
    if expect_cutscenes and cutscene_count != 18:
        errors.append(f"expected 18 cutscenes, found {cutscene_count}")
    if errors:
        raise SystemExit("Asset audit failed:\n  - " + "\n  - ".join(errors))

    worlds_bytes = sum(p.stat().st_size for p in worlds.rglob('*') if p.is_file())
    cutscene_bytes = sum(p.stat().st_size for p in (output / 'Cutscenes').rglob('*') if p.is_file()) if (output / 'Cutscenes').is_dir() else 0
    return {
        'levels': len(brx_files),
        'banks': len(bnk_files),
        'music': len(mus_files),
        'dialog': len(dialog_files),
        'commentary': len(comm_files),
        'ambient': len(amb_files),
        'prompt': 1,
        'cutscenes': cutscene_count,
        'worlds_bytes': worlds_bytes,
        'cutscene_bytes': cutscene_bytes,
    }


def write_manifest_and_stamp(output: Path, counts: dict) -> None:
    manifest = {
        'schemaVersion': 1,
        'serial': 'SCUS_971.98',
        'region': 'NTSC-U',
        'levelFormat': 'PCBRX',
        'levelFormatVersion': 17,
        'languages': ['English'],
        'voiceLanguages': ['English'],
    }
    (output / 'extraction_manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    stamp_lines = [
        'serial=SCUS_971.98',
        f"levels={counts['levels']}",
        f"banks={counts['banks']}",
        f"music={counts['music']}",
        f"dialog={counts['dialog']}",
        f"commentary={counts['commentary']}",
        f"ambient={counts['ambient']}",
        f"prompt={counts['prompt']}",
        f"cutscenes={counts['cutscenes']}",
    ]
    (output / 'assets-ready.stamp').write_text('\n'.join(stamp_lines) + '\n')


def stage_sd_folder(data_dir: Path, sd_root: Path, include_cutscenes: bool) -> Path:
    dest = sd_root / '3ds/ThieviusCTR'
    dest.mkdir(parents=True, exist_ok=True)
    out = ROOT / 'build/3ds'
    runtime_files = [
        (out / 'ThieviusCTR.3dsx', 'ThieviusCTR.3dsx'),
        (out / 'ThieviusCTR.smdh', 'ThieviusCTR.smdh'),
    ]
    missing_bin = [str(src) for src, _ in runtime_files if not src.is_file()]
    if missing_bin:
        print("Note: build/3ds/ThieviusCTR.3dsx not found yet; staging assets and docs without binary.", flush=True)
    else:
        for src, name in runtime_files:
            shutil.copy2(src, dest / name)

    for src, name in [
        (ROOT / 'platform/3ds/README-runtime.txt', 'README-runtime.txt'),
        (ROOT / 'platform/3ds/CREDITS.txt', 'CREDITS.txt'),
        (ROOT / 'platform/3ds/vendor/OpenDSP/LICENSE', 'LICENSE-OpenDSP.txt'),
        (ROOT / 'platform/3ds/LICENSE-FFmpeg-LGPL2.1.txt', 'LICENSE-FFmpeg-LGPL2.1.txt'),
        (ROOT / 'platform/3ds/LICENSE-Mozilla-MPL-2.0.txt', 'LICENSE-Mozilla-MPL-2.0.txt'),
        (data_dir / 'extraction_manifest.json', 'extraction_manifest.json'),
        (data_dir / 'assets-ready.stamp', 'assets-ready.stamp'),
    ]:
        if src.is_file():
            shutil.copy2(src, dest / name)

    sync_tree(data_dir / 'Worlds', dest / 'Worlds')
    if include_cutscenes and (data_dir / 'Cutscenes').is_dir():
        sync_tree(data_dir / 'Cutscenes', dest / 'Cutscenes')
    return dest


def write_zip_from_stage(sd_folder: Path, zip_path: Path) -> None:
    if not (sd_folder / 'ThieviusCTR.3dsx').is_file():
        raise SystemExit(f"Cannot build ZIP without {sd_folder / 'ThieviusCTR.3dsx'} (run build_runtime.py first).")
    files = []
    for p in sorted(sd_folder.rglob('*')):
        if not p.is_file():
            continue
        rel = p.relative_to(sd_folder)
        if rel.parts[0] == 'Saves' or p.name in ('runtime.log', 'runtime-stderr.log', 'native-options.cfg', 'audio-backend.cfg'):
            continue
        files.append((p, '3ds/ThieviusCTR/' + str(rel)))
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        for src, arcname in files:
            zf.write(src, arcname)
    with zipfile.ZipFile(zip_path) as zf:
        bad = zf.testzip()
        if bad:
            raise SystemExit(f"ZIP integrity check failed on: {bad}")
    print(f"Created full SD package: {zip_path} ({fmt_size(zip_path.stat().st_size)})", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso', type=Path, help='Path to US retail Sly Cooper ISO (SCUS_971.98)')
    parser.add_argument('--output', type=Path, default=ROOT / 'local/data',
                        help='Asset extraction output root (default: local/data)')
    parser.add_argument('--sd-out', type=Path,
                        help='Stage complete 3ds/ThieviusCTR/ folder under this SD root directory')
    parser.add_argument('--skip-cutscenes', action='store_true',
                        help='Extract levels and all audio only (skip 18 CMV2 cutscenes)')
    parser.add_argument('--force', action='store_true',
                        help='Re-extract even if valid assets already exist')
    parser.add_argument('--no-sync', action='store_true',
                        help='Do not sync local/3ds/ThieviusCTR or emulator SD folders')
    parser.add_argument('--zip', action='store_true',
                        help='Also build a personal full SD ZIP archive (do not share copyrighted assets)')
    args = parser.parse_args()

    iso = args.iso.resolve()
    if not iso.is_file():
        raise SystemExit(f"ISO not found: {iso}")

    missing_tools = [tool for tool in ('7z', 'g++') if not shutil.which(tool)]
    if not args.skip_cutscenes and not shutil.which('ffmpeg'):
        missing_tools.append('ffmpeg')
    if missing_tools:
        raise SystemExit("Missing required host tools: " + ", ".join(missing_tools))

    cnf = subprocess.check_output(['7z', 'e', '-so', str(iso), 'SYSTEM.CNF'], stderr=subprocess.DEVNULL)
    if b'SCUS_971.98' not in cnf:
        raise SystemExit("This extractor currently requires the US SCUS_971.98 disc.")

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    t_start = time.monotonic()
    timings = {}

    ntsc_cmd = [sys.executable, str(SCRIPT_DIR / 'extract_ntsc.py'), str(iso), '--output', str(output), '--all']
    if args.force:
        ntsc_cmd.append('--force')
    timings['levels'] = run_step('[1/3] Levels (46 BRX)', ntsc_cmd)

    audio_cmd = [sys.executable, str(SCRIPT_DIR / 'extract_audio.py'), str(iso), '--output', str(output), '--all']
    if args.force:
        audio_cmd.append('--force')
    timings['audio'] = run_step('[2/3] Audio (Banks, Music, Dialog, Commentary, Ambient)', audio_cmd)

    if not args.skip_cutscenes:
        intro_cmd = [sys.executable, str(SCRIPT_DIR / 'extract_intro.py'), str(iso),
                     '--output', str(output / 'Cutscenes'), '--all']
        if args.force:
            intro_cmd.append('--force')
        timings['cutscenes'] = run_step('[3/3] Cutscenes (18 CMV2 + WAV)', intro_cmd)
    else:
        print("\n=== [3/3] Cutscenes skipped (--skip-cutscenes) ===", flush=True)

    counts = audit_output(output, expect_cutscenes=not args.skip_cutscenes)
    write_manifest_and_stamp(output, counts)

    runtime_bin = ROOT / 'build/3ds/ThieviusCTR.3dsx'
    staged_dir = None
    if args.sd_out:
        staged_dir = stage_sd_folder(output, args.sd_out.resolve(), include_cutscenes=not args.skip_cutscenes)
        print(f"\nStaged SD layout: {staged_dir}", flush=True)
        if args.zip:
            write_zip_from_stage(staged_dir, args.sd_out.resolve() / 'ThieviusCTR-full.zip')
    elif not args.no_sync and output == (ROOT / 'local/data').resolve():
        if runtime_bin.is_file():
            pkg_cmd = [sys.executable, str(SCRIPT_DIR / 'package_runtime.py')]
            if args.zip:
                pkg_cmd.append('--zip-assets')
            subprocess.run(pkg_cmd, check=True)
            staged_dir = ROOT / 'local/3ds/ThieviusCTR'
        else:
            print("\nNote: build/3ds/ThieviusCTR.3dsx not built yet; skipping runtime sync.", flush=True)
    elif args.zip:
        raise SystemExit("--zip with a custom --output requires --sd-out to know where to stage and save the archive.")

    total_elapsed = time.monotonic() - t_start
    print("\n=== Extraction Summary ===", flush=True)
    print(f"  Output:      {output}")
    if staged_dir:
        print(f"  3DS folder:  {staged_dir}")
    print(f"  Levels:      {counts['levels']}/46")
    print(f"  Sound Banks: {counts['banks']}/7")
    print(f"  Music:       {counts['music']}/38")
    print(f"  Dialog WAVs: {counts['dialog']}")
    print(f"  Commentary:  {counts['commentary']}/33")
    print(f"  Ambient:     {counts['ambient']}/6 + Prompt ({counts['prompt']}/1)")
    print(f"  Cutscenes:   {counts['cutscenes']}/18")
    print(f"  Worlds size: {fmt_size(counts['worlds_bytes'])}")
    if counts['cutscene_bytes']:
        print(f"  Movies size: {fmt_size(counts['cutscene_bytes'])}")
    print(f"  Total size:  {fmt_size(counts['worlds_bytes'] + counts['cutscene_bytes'])}")
    print(f"  Elapsed:     {fmt_duration(total_elapsed)}")


if __name__ == '__main__':
    main()
