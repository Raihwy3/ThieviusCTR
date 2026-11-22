#!/usr/bin/env python3
"""Extract Sly 1 music, sound banks, commentary, ambient/prompt and dialog WAVs from the US disc.

Descriptor tables and VAG decoding follow ProjectCane's media extractor.
Generated audio belongs in local/data, never in a source/release package.
"""
from pathlib import Path
import argparse, array, re, struct, subprocess, sys, wave

ROOT = Path(__file__).resolve().parents[2]
COEFF = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))
EPISODES = {
    1: 'Tide of Terror',
    2: 'Sunset Snake Eyes',
    3: 'Vicious Voodoo',
    4: 'Fire in the sky',
    5: 'The Cold Heart of Hate',
}
BANK_NAMES = ('Common', 'Intro', 'Underwater', 'Muggshot', 'Voodoo', 'Snow', 'Clockwerk')
MUSIC = (
    ('Paris, France', 0x55, 0xC8E0E, 0x7A6A0),
    ('The Hideout', 0x43, 0xD3411, 0x6F7A0),
    ('A Cunning Disguise', 0x47, 0xC4602, 0x72324),
    ('A Stealthy Approach', 0x51, 0xD44ED, 0x745F8),
    ('High Class Heist', 0x59, 0xCDE0B, 0x74674),
    ('Into the Machine', 0x3F, 0xC839D, 0x7B414),
    ('Prowling the Grounds', 0x44, 0xBFA16, 0x746D4),
    ('The Eye of the Storm', 0x57, 0xC354B, 0x7A0C4),
    ('The Fire Down Below', 0x46, 0xC130C, 0x775E0),
    ('The Gunboat Graveyard', 0x42, 0xCAC3D, 0x70D4C),
    ('Treasure in the Depths', 0x3E, 0xD4E9D, 0x748F8),
    ('A Rocky Start', 0x50, 0xBC6B4, 0x64A34),
    ("At the Dog Track", 0x4E, 0xCFBE5, 0x726F8),
    ('Back Alley Heist', 0x4C, 0xC1B99, 0x77978),
    ('Boneyard Casino', 0x49, 0xCF341, 0x71A18),
    ('Last Call', 0x48, 0xC02DE, 0x731B8),
    ("Muggshot's Turf", 0x40, 0xCE9BB, 0x5DB18),
    ("Murray's Big Gamble", 0x4A, 0xCD592, 0x767B8),
    ('Straight to the Top', 0x4C, 0xD04EF, 0x77978),
    ('Two to Tango', 0x4D, 0xD5A8B, 0x788FC),
    ('A Deadly Dance', 0x68, 0xBE35F, 0x771E8),
    ('A Ghastly Voyage', 0x65, 0xC6338, 0x76D9C),
    ('A Grave Undertaking', 0x55, 0xCCD68, 0x7A6A0),
    ('Descent into Danger', 0x64, 0xC25C6, 0x6EBB8),
    ('Down Home Cooking', 0x62, 0xC50BB, 0x73978),
    ('Piranha Lake', 0x67, 0xD4AFA, 0x73978),
    ('The Dread Swamp Path', 0x60, 0xC6D86, 0x76A60),
    ('The Lair of the Beast', 0x64, 0xCB6A5, 0x6EBB8),
    ("The Swamp's Dark Center", 0x64, 0xD1852, 0x6EBB8),
    ('A Desperate Race', 0x4E, 0xCBD9B, 0x726F8),
    ('A Perilous Ascent', 0x53, 0xD2424, 0x773CC),
    ('Duel by the Dragon', 0x5B, 0xBD2A0, 0x6DA90),
    ('Flame Fu!', 0x56, 0xC4B48, 0x6EA6C),
    ('Flaming Temple of Flame', 0x54, 0xBEF0F, 0x7737C),
    ('Inside the Stronghold', 0x5B, 0xC7870, 0x6DA90),
    ('Rapid Fire Assault', 0x5D, 0xC2F89, 0x6C2BC),
    ('The King of the Hill', 0x5C, 0xD2D42, 0x78064),
    ('The Unseen Foe', 0x44, 0xC5A33, 0x746D4),
    ('A Daring Rescue', 0x38, 0xBD8B1, 0x788F8),
    ('A Hazardous Path', 0x35, 0xC3CB5, 0x77C7C),
    ('A Strange Reunion', 0x34, 0xC0AAC, 0x95774),
    ('A Temporary Truce', 0x39, 0xD0BD7, 0x74DAC),
    ('Bentley Comes Through', 0x36, 0xD50F1, 0x8C528),
    ('Burning Rubber', 0x3A, 0xCC1CA, 0x77614),
    ('Sinking Peril', 0x37, 0xD3A9E, 0x77F08),
)


def va_off(va):
    return va - 0x00100000 + 0x1000


def read_cstr(elf, va):
    if va < 0x00100000:
        return ''
    at = va_off(va)
    if at < 0 or at >= len(elf):
        return ''
    end = elf.find(b'\0', at, min(at + 256, len(elf)))
    if end < 0:
        return ''
    return elf[at:end].decode('ascii', errors='replace')


def decode_desc(raw):
    d = struct.unpack_from('<8I', raw)
    return d[0] ^ d[5], d[1] ^ d[7], d[2] ^ d[6]


def sanitize(name):
    out = ''.join('_' if c in '<>:"/\\|?*' or ord(c) < 32 else c for c in name).rstrip(' .')
    return out or 'Unnamed Level'


def write_wav(dest, pcm, rate):
    dest.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(dest), 'wb') as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(pcm)


def vag_pcm(data):
    if len(data) < 48 or data[:4] != b'VAGp':
        raise ValueError('Missing VAGp header')
    size, rate = struct.unpack_from('>II', data, 12)
    if not 4000 <= rate <= 192000:
        raise ValueError('Invalid VAG rate')
    end = min(len(data), 48 + (size or len(data) - 48))
    pcm = array.array('h')
    h1 = h2 = 0
    for block in range(48, end - 15, 16):
        shift = data[block] & 15
        predictor = data[block] >> 4
        a, b = COEFF[predictor] if predictor < len(COEFF) else (0, 0)
        for sample in range(28):
            packed = data[block + 2 + sample // 2]
            nibble = (packed >> (4 * (sample % 2))) & 15
            nibble = nibble - 16 if nibble >= 8 else nibble
            value = max(-32768, min(32767, ((nibble * 4096) >> shift) + ((a * h1 + b * h2 + 32) >> 6)))
            h2, h1 = h1, value
            pcm.append(value)
        if data[block + 1] & 1:
            break
    if not pcm:
        raise ValueError('Empty VAG stream')
    if sys.byteorder != 'little':
        pcm.byteswap()
    return pcm.tobytes(), rate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'local/data')
    parser.add_argument('--all', action='store_true', help='extract full game music, banks, commentary, ambient, prompt and all dialog')
    parser.add_argument('--force', action='store_true', help='overwrite existing extracted audio')
    args = parser.parse_args()

    cnf = subprocess.check_output(['7z', 'e', '-so', str(args.iso), 'SYSTEM.CNF'], stderr=subprocess.DEVNULL)
    if b'SCUS_971.98' not in cnf:
        raise ValueError('Requires US SCUS_971.98')
    elf = subprocess.check_output(['7z', 'e', '-so', str(args.iso), 'SCUS_971.98'], stderr=subprocess.DEVNULL)
    if elf[:4] != b'\x7fELF':
        raise ValueError('Invalid executable')

    level_names = {}
    for i in range(46):
        row = struct.unpack_from('<11I', elf, 0x148af0 + i * 44)
        ident = row[8]
        name = sanitize(read_cstr(elf, row[9]))
        if name and ident not in level_names:
            level_names[ident] = name
    level_names[3] = 'Paris'
    level_names[4] = 'The Hideout'

    length = args.iso.stat().st_size
    with args.iso.open('rb') as disc:
        def read(sector, size):
            start = sector * 2048
            if not 0 < size <= 8 * 1024 * 1024 or start + size > length:
                raise ValueError(f'Audio outside disc (sector={sector:#x}, size={size:#x})')
            disc.seek(start)
            result = disc.read(size)
            if len(result) != size:
                raise ValueError('Truncated audio')
            return result

        # Music
        music_dir = args.output / 'Worlds/Music'
        music_dir.mkdir(parents=True, exist_ok=True)
        (music_dir / 'MusicMap.txt').write_text(
            '\n'.join(f'{lvl} | MUSID 0x{mid:X} | sector 0x{sec:X} | bytes 0x{sz:X}' for lvl, mid, sec, sz in MUSIC) + '\n'
        )
        music_items = {mid: (sec, sz) for _, mid, sec, sz in MUSIC} if args.all else {0x55: (0xC8E0E, 0x7A6A0)}
        for mid, (sec, sz) in music_items.items():
            dest = music_dir / f'MUSID_{mid:02X}.mus'
            if args.force or not dest.is_file() or dest.stat().st_size != sz:
                dest.write_bytes(read(sec, sz))
                print('Extracted:', dest, flush=True)

        # Sound banks
        bank_dir = args.output / 'Worlds/Sound Banks'
        bank_dir.mkdir(parents=True, exist_ok=True)
        bank_indices = range(len(BANK_NAMES)) if args.all else (0, 1)
        for i in bank_indices:
            sec, sz, _ = decode_desc(elf[va_off(0x00244F40) + i * 32:])
            dest = bank_dir / (BANK_NAMES[i] + '.bnk')
            if args.force or not dest.is_file() or dest.stat().st_size != sz:
                data = read(sec, sz)
                if struct.unpack_from('<I', data)[0] != 3:
                    raise ValueError('Invalid 989 effect bank: ' + BANK_NAMES[i])
                dest.write_bytes(data)
                print('Extracted:', dest, flush=True)

        # Dialogs
        table = (ROOT / 'File Extractor GUI/Data/DialogAudioTable.inc').read_text()
        rows = re.findall(r'\{\s*(0x[0-9A-F]+|\d+),\s*(0x[0-9A-F]+|\d+),\s*(0x[0-9A-F]+|\d+),\s*(0x[0-9A-F]+|\d+),\s*DialogAudioSource::(\w+)', table, re.I)
        dialog_maps = {}
        dialog_count = 0
        for row in dict.fromkeys((int(r[0], 0), int(r[1], 0), int(r[2], 0), int(r[3], 0), r[4]) for r in rows):
            level, sector, size, identity, source = row
            if not args.all and level != 3:
                continue
            if not size:
                header = read(sector, 48)
                size = 48 + struct.unpack_from('>I', header, 12)[0]
            episode = EPISODES.get(level >> 8, '')
            lname = level_names.get(level, f'Level_{level}')
            ddir = args.output / 'Worlds' / episode / lname / 'Dialog'
            ddir.mkdir(parents=True, exist_ok=True)
            name = f'VAG_{identity:04X}_{sector:06X}.wav'
            dest = ddir / name
            if args.force or not dest.is_file() or dest.stat().st_size < 44:
                pcm, rate = vag_pcm(read(sector, size))
                write_wav(dest, pcm, rate)
                dialog_count += 1
            dialog_maps.setdefault(ddir, []).append(
                f'{name} | levelId 0x{level:X} | identity 0x{identity:X} | sector 0x{sector:X} | bytes 0x{size:X} | source {source}'
            )
        for ddir, lines in dialog_maps.items():
            (ddir / 'DialogMap.txt').write_text('\n'.join(lines) + '\n')
        print('Decoded dialog voice clips:', dialog_count, flush=True)

        if args.all:
            # Commentary (33 entries x 44 bytes)
            comm_dir = args.output / 'Worlds/Sounds/Commentary'
            comm_dir.mkdir(parents=True, exist_ok=True)
            comm_lines = []
            comm_count = 0
            comm_base = va_off(0x00248748)
            for i in range(33):
                off = comm_base + i * 44
                level_id, _dur = struct.unpack_from('<If', elf, off)
                sec, sz, ident = decode_desc(elf[off + 8:off + 40])
                speaker_va = struct.unpack_from('<I', elf, off + 40)[0]
                if not sec or not sz:
                    continue
                name = f'VAG_{ident:04X}_{sec:06X}.wav'
                dest = comm_dir / name
                if args.force or not dest.is_file() or dest.stat().st_size < 44:
                    pcm, rate = vag_pcm(read(sec, sz))
                    write_wav(dest, pcm, rate)
                    comm_count += 1
                speakers = (read_cstr(elf, speaker_va) or 'Unknown').replace('\r\n', ' ').replace('\n', ' ')
                comm_lines.append(
                    f'{name} | levelId 0x{level_id:X} | duration {_dur} | identity 0x{ident:X} | sector 0x{sec:X} | bytes 0x{sz:X} | speakers {speakers}'
                )
            (comm_dir / 'CommentaryMap.txt').write_text('\n'.join(comm_lines) + '\n')
            print('Decoded commentary clips:', comm_count, flush=True)

            # JLO Ambient (6 entries)
            amb_dir = args.output / 'Worlds/Sounds/JLO Ambient'
            amb_dir.mkdir(parents=True, exist_ok=True)
            amb_base = va_off(0x002482D8)
            for i in range(6):
                sec, sz, _ = decode_desc(elf[amb_base + i * 32:])
                dest = amb_dir / f'JloAmbient_{i + 1}.wav'
                if args.force or not dest.is_file() or dest.stat().st_size < 44:
                    pcm, rate = vag_pcm(read(sec, sz))
                    write_wav(dest, pcm, rate)
                    print('Extracted:', dest, flush=True)

            # Prompt (1 entry at 0x0026A118)
            prompt_dir = args.output / 'Worlds/Sounds/Prompt'
            prompt_dir.mkdir(parents=True, exist_ok=True)
            sec, sz, _ = decode_desc(elf[va_off(0x0026A118):])
            dest = prompt_dir / 'PromptPrk18.wav'
            if args.force or not dest.is_file() or dest.stat().st_size < 44:
                pcm, rate = vag_pcm(read(sec, sz))
                write_wav(dest, pcm, rate)
                print('Extracted:', dest, flush=True)


if __name__ == '__main__':
    main()
