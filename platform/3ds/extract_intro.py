#!/usr/bin/env python3
"""Prepare the user's US-disc cutscenes as compact GPU_ETC1 CMV2 + 16kHz mono WAV.

PSS demultiplexing follows ProjectCane's Sly1AudioCodec. Movie frames are
320x240 at 15 FPS encoded to PICA200 ETC1 tiles (38,400 bytes/frame).
"""
from pathlib import Path
import argparse, array, struct, subprocess, sys, tempfile, wave
from extract_audio import COEFF

ROOT = Path(__file__).resolve().parents[2]


def ensure_encoder():
    exe = ROOT / 'build/host-tools/cmov_encode'
    src = ROOT / 'platform/3ds/cmov_encode.cpp'
    if not exe.exists() or exe.stat().st_mtime < src.stat().st_mtime:
        exe.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['g++', '-std=c++20', '-O3', '-pthread', '-Wall', str(src), '-o', str(exe)], check=True)
    return exe


def demux(data):
    video = bytearray()
    audio = bytearray()
    direct = bytearray()
    rate = 0
    channels = 0
    offset = 0
    while offset + 9 <= len(data):
        tag = int.from_bytes(data[offset:offset + 4], 'big')
        if tag == 0x1B9:
            break
        if tag == 0x1BA:
            offset += 14 + (data[offset + 13] & 7)
            continue
        if tag in (0x1BB, 0x1BE):
            offset += 6 + int.from_bytes(data[offset + 4:offset + 6], 'big')
            continue
        if tag not in (0x1E0, 0x1BD):
            offset += 1
            continue
        size = int.from_bytes(data[offset + 4:offset + 6], 'big')
        start = offset + 9 + data[offset + 8]
        end = offset + 6 + size
        if end > len(data) or start > end:
            raise ValueError('Truncated PSS')
        packet = data[start:end]
        if tag == 0x1E0:
            video.extend(packet)
        elif len(packet) > 4:
            audio.extend(packet[4:])
            if packet[3] == 1:
                payload = packet[4:]
                if not rate:
                    if len(payload) < 0x800:
                        raise ValueError('Truncated first PSS audio packet')
                    rate = int.from_bytes(payload[6:8], 'big')
                    channels = payload[8]
                    payload = payload[0x800:]
                direct.extend(payload)
        offset = end
    if not direct:
        h = audio.find(b'SShd')
        b = audio.find(b'SSbd', h + 8) if h >= 0 else -1
        if h < 0 or b < 0 or h + 32 > len(audio) or b + 8 > len(audio):
            raise ValueError('Missing PSS audio')
        fmt, rate, channels, interleave = struct.unpack_from('<4I', audio, h + 8)
        if fmt not in (0, 1, 2, 0x10):
            raise ValueError(f'Unsupported audio encoding {fmt}')
        body_sz = min(struct.unpack_from('<I', audio, b + 4)[0], len(audio) - b - 8)
        direct = audio[b + 8:b + 8 + body_sz]
        interleave = interleave or 0x800
    else:
        interleave = 0x800
        fmt = 2
    if channels not in (1, 2) or not rate:
        raise ValueError('Invalid audio format')
    streams = [bytearray() for _ in range(channels)]
    if channels == 1:
        streams[0] = direct
    else:
        for i, start in enumerate(range(0, len(direct), interleave)):
            streams[i % channels].extend(direct[start:start + interleave])
    decoded = []
    for stream in streams:
        out = array.array('h')
        h1 = h2 = 0
        if fmt in (0, 1):
            out.frombytes(stream[:len(stream) // 2 * 2])
            if (sys.byteorder == 'little') != (fmt == 1):
                out.byteswap()
            decoded.append(out)
            continue
        for start in range(0, len(stream) - 15, 16):
            frame = stream[start:start + 16]
            predictor = frame[0] >> 4
            shift = frame[0] & 15
            if predictor >= 5:
                continue
            if frame[:2] == b'\0\1' and frame[2:] == b'\x77' * 14:
                break
            a, b = COEFF[predictor]
            for i in range(28):
                n = (frame[2 + i // 2] >> (4 * (i % 2))) & 15
                n = n - 16 if n >= 8 else n
                v = max(-32768, min(32767, ((n * 4096) >> shift) + ((a * h1 + b * h2 + 32) >> 6)))
                h2, h1 = h1, v
                out.append(v)
        decoded.append(out)
    pcm = array.array('h', (sum(values) // channels for values in zip(*decoded)))
    if sys.byteorder != 'little':
        pcm.byteswap()
    return video, pcm.tobytes(), rate


def is_valid_cmv2(path: Path) -> bool:
    if not path.is_file() or path.stat().st_size < 24:
        return False
    with path.open('rb') as f:
        hdr = f.read(24)
    if len(hdr) != 24:
        return False
    magic, w, h, fps, nf, fbytes = struct.unpack('<4s5I', hdr)
    expected = (w // 8) * (h // 8) * 32
    fps_valid = (1 <= fps <= 60) or (1000 <= fps <= 60000)
    return (
        magic == b'CMV2'
        and 64 <= w <= 800 and (w % 8) == 0
        and 64 <= h <= 240 and (h % 8) == 0
        and fps_valid
        and nf > 0
        and fbytes == expected
        and path.stat().st_size == 24 + nf * fbytes
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'local/data/Cutscenes')
    parser.add_argument('--index', action='append', type=int, help='repeatable cutscene index (0..17)')
    parser.add_argument('--all', action='store_true', help='extract all 18 cutscenes (0..17)')
    parser.add_argument('--force', action='store_true', help='re-encode cutscenes even if valid CMV2 + WAV already exist')
    args = parser.parse_args()

    elf = subprocess.check_output(['7z', 'e', '-so', str(args.iso), 'SCUS_971.98'], stderr=subprocess.DEVNULL)
    if elf[:4] != b'\x7fELF':
        raise ValueError('US retail disc required')

    encoder = ensure_encoder()
    selected = set(range(18)) if args.all else set(args.index if args.index else (17, 0))
    if any(i < 0 or i > 17 for i in selected):
        raise ValueError('Cutscene index must be between 0 and 17')

    descriptors = {}
    base = 0x248490 - 0x100000 + 0x1000
    for i in range(17):
        row = struct.unpack_from('<8I', elf, base + i * 32)
        out_idx = 16 if i == 0 else i - 1
        descriptors[out_idx] = (row[0] ^ row[5], row[1] ^ row[7])
    descriptors[17] = (0xC8F03, 0xAB4004)

    output = args.output
    output.mkdir(parents=True, exist_ok=True)
    iso_size = args.iso.stat().st_size

    with args.iso.open('rb') as iso, tempfile.TemporaryDirectory() as temp:
        for index in sorted(selected):
            stem = f'Cutscene_{index:02d}'
            cmov_path = output / (stem + '.cmov')
            wav_path = output / (stem + '.wav')
            if not args.force and is_valid_cmv2(cmov_path) and wav_path.is_file() and wav_path.stat().st_size > 44:
                print('Existing CMV2:', cmov_path, flush=True)
                continue

            sector, size = descriptors[index]
            if not 0 < size < 512 * 1024 * 1024 or sector * 2048 + size > iso_size:
                raise ValueError(f'Invalid movie descriptor for {stem}: sector={sector:#x} size={size:#x}')
            print('Demuxing:', stem, f'({size / 1048576:.1f} MiB)...', flush=True)
            iso.seek(sector * 2048)
            video, audio, rate = demux(iso.read(size))

            movie = Path(temp) / 'movie.m2v'
            movie.write_bytes(video)
            wav = Path(temp) / 'audio.wav'
            with wave.open(str(wav), 'wb') as f:
                f.setnchannels(1)
                f.setsampwidth(2)
                f.setframerate(rate)
                f.writeframes(audio)
            subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(wav), '-ar', '16000', str(wav_path)], check=True)

            cmov_tmp = cmov_path.with_suffix('.cmov.tmp')
            ff = subprocess.Popen(
                ['ffmpeg', '-v', 'error', '-i', str(movie), '-vf', 'scale=320:240,fps=15',
                 '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'],
                stdout=subprocess.PIPE,
            )
            enc = subprocess.run([str(encoder), str(cmov_tmp)], stdin=ff.stdout, check=True)
            ff.stdout.close()
            if ff.wait():
                raise ValueError('ffmpeg movie conversion failed for ' + stem)
            cmov_tmp.replace(cmov_path)
            nf = struct.unpack('<4s5I', cmov_path.open('rb').read(24))[4]
            print(f'{stem}: {nf} frames ({cmov_path.stat().st_size / 1048576:.1f} MiB CMV2)', flush=True)


if __name__ == '__main__':
    main()
