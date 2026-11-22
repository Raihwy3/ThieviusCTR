#!/usr/bin/env python3
"""Dump Sly 1's single PS2 controller button icon atlas (font[1] / g_pfontJoy) and an annotated bounding-box guide."""
from pathlib import Path
import argparse
import struct
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]


def load_pcbrx_payload(raw: bytes) -> bytes:
    if len(raw) < 8 or raw[:4] != b'PCBR':
        raise RuntimeError('Not a valid PCBRX file (missing PCBR magic)')
    version, = struct.unpack_from('<I', raw, 4)
    if version in (17, 18):
        return raw
    if version != 19:
        raise RuntimeError(f'Unsupported PCBRX version: {version}')
    if raw[8:12] != b'PUNI':
        raise RuntimeError('Invalid PCBRX v19 container (missing PUNI magic)')
    _schema, lang_count, default_lang, chunk_count = struct.unpack_from('<4I', raw, 12)
    off = 28
    languages = {}
    for _ in range(lang_count):
        lang_id, payload_size, ref_count = struct.unpack_from('<IQI', raw, off)
        off += 16
        refs = struct.unpack_from(f'<{ref_count}I', raw, off)
        off += 4 * ref_count
        languages[lang_id] = (payload_size, refs)
    chunks = []
    for _ in range(chunk_count):
        chunk_size, _crc32 = struct.unpack_from('<2I', raw, off)
        off += 8
        chunks.append(raw[off:off + chunk_size])
        off += chunk_size
    target_lang = 0 if 0 in languages else default_lang
    payload_size, refs = languages[target_lang]
    payload = b''.join(chunks[cid] for cid in refs)
    if len(payload) != payload_size:
        raise RuntimeError('Reconstructed PCBRX v19 payload size mismatch')
    return payload


def parse_bitmaps_and_fonts(raw: bytes):
    mpos = raw.find(b'MUSID_')
    if mpos < 0:
        raise RuntimeError('Could not locate MUSID_ field before bitmap table')
    off = mpos + 32
    cbmp, = struct.unpack_from('<I', raw, off)
    off += 4
    bmps = []
    for _ in range(cbmp):
        bmps.append(struct.unpack_from('<8I', raw, off))
        off += 32

    total_tex = max(b[5] + b[4] for b in bmps)
    data_start = len(raw) - total_tex
    if data_start < 0 or (data_start % 16) != 0:
        raise RuntimeError(f'Unexpected texture data_start={data_start}')

    font_bmps = [i for i, b in enumerate(bmps) if b[6] == 0 and b[7] == 0 and b[0] >= 128]
    for cand in font_bmps:
        pat = struct.pack('<i', cand)
        search_from = off
        while True:
            pos = raw.find(pat, search_from)
            if pos < 0:
                break
            search_from = pos + 1
            if pos < 2:
                continue
            nf, = struct.unpack_from('<H', raw, pos - 2)
            if not (1 <= nf <= 10):
                continue
            p = pos
            ok = True
            fonts = []
            for fi in range(nf):
                if p + 16 > len(raw):
                    ok = False
                    break
                im, dx_c, dx_s, dy, fk, rx, ng = struct.unpack_from('<iBBBBfI', raw, p)
                p += 16
                if not (0 <= im < cbmp and ng <= 2000) or p + ng * 8 > len(raw):
                    ok = False
                    break
                bw, bh = bmps[im][0], bmps[im][1]
                glyphs = []
                for g in range(ng):
                    wch, gx, gy, dx = struct.unpack_from('<hhhh', raw, p + g * 8)
                    if gx < 0 or gy < 0 or dx < 0 or gx + dx > bw or gy + dy > bh:
                        ok = False
                        break
                    glyphs.append((wch, gx, gy, dx))
                if not ok:
                    break
                p += ng * 8
                fonts.append({
                    'index': fi,
                    'image_index': im,
                    'width': bw,
                    'height': bh,
                    'dx_char': dx_c,
                    'dx_space': dx_s,
                    'dy': dy,
                    'fontk': fk,
                    'scale': rx,
                    'glyphs': glyphs,
                })
            if ok and len(fonts) == nf:
                return bmps, data_start, fonts
    raise RuntimeError('Could not locate font table in BRX payload')


def make_checkerboard(width: int, height: int, tile: int = 8) -> Image.Image:
    img = Image.new('RGBA', (width, height))
    px = img.load()
    c0 = (28, 30, 36, 255)
    c1 = (42, 46, 56, 255)
    for y in range(height):
        for x in range(width):
            px[x, y] = c0 if ((x // tile) + (y // tile)) % 2 == 0 else c1
    return img


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--brx', type=Path, default=ROOT / 'local/data/Worlds/Paris/Paris.brx')
    parser.add_argument('--out', type=Path, default=ROOT / 'local/ps2-buttons.png')
    parser.add_argument('--guide', type=Path, default=ROOT / 'local/ps2-buttons-guide.png')
    args = parser.parse_args()

    payload = load_pcbrx_payload(args.brx.read_bytes())
    bmps, data_start, fonts = parse_bitmaps_and_fonts(payload)

    joy_font = next((f for f in fonts if f['fontk'] == 1), None)
    if joy_font is None:
        if len(fonts) < 2:
            raise RuntimeError('Controller icon font (font[1]) not found')
        joy_font = fonts[1]

    im_idx = joy_font['image_index']
    bw, bh, _zon, _mips, cb, boff, _flags, fmt = bmps[im_idx]
    if fmt != 0:
        raise RuntimeError(f'Expected RGBA8 (fmt=0) for font atlas, got fmt={fmt}')
    pixels = payload[data_start + boff:data_start + boff + cb]
    if len(pixels) != bw * bh * 4:
        raise RuntimeError('Unexpected pixel byte count for font atlas')

    sheet = Image.frombytes('RGBA', (bw, bh), pixels)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.out)

    scale = 3
    gw, gh = bw * scale, bh * scale
    guide = make_checkerboard(gw, gh, tile=12)
    scaled_sheet = sheet.resize((gw, gh), Image.NEAREST)
    guide = Image.alpha_composite(guide, scaled_sheet)
    draw = ImageDraw.Draw(guide)

    palette = [
        (0, 220, 255, 255),
        (255, 190, 60, 255),
        (120, 255, 140, 255),
        (255, 110, 180, 255),
    ]

    dy = joy_font['dy']
    print(f"font[{joy_font['index']}] (bmp[{im_idx}], {bw}x{bh}, dy={dy}, scale={joy_font['scale']:.3f}):")
    for idx, (wch, gx, gy, dx) in enumerate(joy_font['glyphs']):
        ch = chr(wch) if 32 <= wch < 127 else f'0x{wch:04x}'
        print(f'  char={ch!r:6s} hex=0x{wch:02x}  x={gx:3d} y={gy:3d} w={dx:3d} h={dy:2d}')
        color = palette[idx % len(palette)]
        x0, y0 = gx * scale, gy * scale
        x1, y1 = (gx + dx) * scale - 1, (gy + dy) * scale - 1
        draw.rectangle([x0, y0, x1, y1], outline=color, width=1)
        label = chr(wch) if 32 <= wch < 127 else f'{wch:02x}'
        draw.rectangle([x0 + 1, y0 + 1, x0 + 12, y0 + 12], fill=(0, 0, 0, 200))
        draw.text((x0 + 3, y0 + 1), label, fill=color)

    args.guide.parent.mkdir(parents=True, exist_ok=True)
    guide.save(args.guide)
    print(f'Saved 1:1 sprite sheet: {args.out}')
    print(f'Saved 3x annotated guide: {args.guide}')


if __name__ == '__main__':
    main()
