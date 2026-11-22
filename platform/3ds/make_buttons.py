#!/usr/bin/env python3
"""Generate original vector button artwork and its native texture overlay.
No game pixels are read or included. Coordinates match the controller font.
"""
from pathlib import Path
import subprocess
from PIL import Image
ROOT=Path(__file__).resolve().parent
OUT=ROOT/'assets'
# Original, simple monoline lettering; no external font assets needed.
LETTERS={
'A':'M0 7 L2.5 0 L5 7 M1 4.5 H4',
'B':'M0 7 V0 H3 Q5 0 5 1.7 Q5 3.5 3 3.5 H0 M3 3.5 Q5 3.5 5 5.2 Q5 7 3 7 H0',
'X':'M0 0 L5 7 M5 0 L0 7','Y':'M0 0 L2.5 3.5 L5 0 M2.5 3.5 V7',
'L':'M0.75 0 V7 H4.25','R':'M0 7 V0 H3 Q5 0 5 1.8 Q5 3.5 3 3.5 H0 M2.5 3.5 L5 7',
'Z':'M0 0 H5 L0 7 H5','S':'M5 0 H2 Q0 0 0 1.8 Q0 3.5 2 3.5 H3 Q5 3.5 5 5.2 Q5 7 3 7 H0',
'E':'M5 0 H0 V7 H5 M0 3.5 H4','C':'M5 0 H2 Q0 0 0 2 V5 Q0 7 2 7 H5',
'T':'M0 0 H5 M2.5 0 V7',
'e':'M0 4.5 H5 V4 Q5 2.5 2.5 2.5 Q0 2.5 0 4.5 V5 Q0 7 2.5 7 H5',
'l':'M2 0 V6 Q2 7 4 7',
'c':'M5 2.5 H2 Q0 2.5 0 4.5 V5 Q0 7 2 7 H5',
't':'M2 1 V5.5 Q2 7 4 7 M0 2.5 H4',
'a':'M0 3 H3 Q5 3 5 5 V7 M5 5 H2 Q0 5 0 6 Q0 7 2 7 H5',
'r':'M0 7 V3 M0 4 Q2 2.5 5 3'}
RECTS=[(1,1,32,'B'),(34,1,32,'A'),(67,1,32,'Y'),(100,1,32,'X'),
(133,1,32,'PAD'),(166,1,32,'NUB'),(1,34,36,'L'),(38,34,36,'R'),
(75,34,36,'ZL'),(112,34,36,'ZR'),(149,34,13,'LEFT'),(163,34,13,'RIGHT'),
(177,34,12,'UP'),(190,34,12,'DOWN'),(203,34,32,'DPAD'),(1,67,112,'SELECT'),(114,67,96,'START')]
def lettering(text,x,y,w,h,color="#a8d8f4",weight=1.25):
    width=len(text)*7-2;scale=min(w/width,h/7)
    paths=''.join(f'<path transform="translate({i*7} 0)" d="{LETTERS[ch]}"/>' for i,ch in enumerate(text))
    weight*=1.2
    return (f'<g transform="translate({x+(w-width*scale)/2:.3f} {y+(h-7*scale)/2:.3f}) scale({scale:.4f})" fill="none" stroke-width="{weight}" stroke-linejoin="bevel" stroke-linecap="butt">'
            f'<g transform="translate(.25 .35)" stroke="#15212c">{paths}</g>'
            f'<g stroke="{color}">{paths}</g></g>')
svg=['<svg xmlns="http://www.w3.org/2000/svg" width="256" height="128" viewBox="0 0 256 128">',
'<defs><linearGradient id="body" x2="0" y2="1"><stop stop-color="#afb2b4"/><stop offset=".16" stop-color="#6d7175"/><stop offset=".5" stop-color="#383c40"/><stop offset="1" stop-color="#15181c"/></linearGradient><radialGradient id="face" cx=".35" cy=".2" r=".8"><stop stop-color="#acafb1"/><stop offset=".38" stop-color="#707478"/><stop offset=".75" stop-color="#383c40"/><stop offset="1" stop-color="#14181b"/></radialGradient><radialGradient id="pad" cx=".35" cy=".25" r=".8"><stop stop-color="#c0c5ca"/><stop offset="1" stop-color="#707982"/></radialGradient></defs>']
for x,y,w,label in RECTS:
    if label in ('SELECT','START'):
        svg.append(lettering(label.title(),x+2,y+5,w-32,21))
        svg.append(f'<circle cx="{x+w-13}" cy="{y+17}" r="8" fill="#06090d"/><circle cx="{x+w-13}" cy="{y+15.5}" r="7" fill="url(#face)" stroke="#6b737c" stroke-width=".7"/>')
    elif label in ('DPAD','LEFT','RIGHT','UP','DOWN'):
        paths={'DPAD':'M11 2 H21 V11 H30 V21 H21 V30 H11 V21 H2 V11 H11 Z',
               'LEFT':'M1 11 H12 V21 H1 Z','RIGHT':'M1 11 H12 V21 H1 Z',
               'UP':'M1 10 H11 V22 H1 Z','DOWN':'M1 10 H11 V22 H1 Z'}
        grooves={'DPAD':'M16 4 V9 M23 16 H28 M16 23 V28 M4 16 H9',
                 'LEFT':'M3 16 H8','RIGHT':'M5 16 H10',
                 'UP':'M6 12 V17','DOWN':'M6 15 V20'}
        svg.append(f'<path transform="translate({x} {y})" d="{paths[label]}" fill="url(#body)" stroke="#77818b" stroke-width=".8" stroke-linejoin="miter"/>')
        svg.append(f'<path transform="translate({x} {y+.65})" d="{grooves[label]}" fill="none" stroke="#111720" stroke-width="1.6"/>')
        svg.append(f'<path transform="translate({x} {y})" d="{grooves[label]}" fill="none" stroke="#a0a9b2" stroke-width="1"/>')
    elif label in ('PAD','NUB'):
        r=13 if label=='PAD' else 9
        svg.append(f'<circle cx="{x+w/2}" cy="{y+16}" r="{r+1}" fill="#151b23"/><circle cx="{x+w/2}" cy="{y+15}" r="{r}" fill="url(#pad)" stroke="#d0d9df" stroke-width=".7"/>')
        if label=='PAD':svg.append(f'<circle cx="{x+w/2}" cy="{y+15}" r="10" fill="none" stroke="#626d78" stroke-width=".7"/>')
        else:svg.append(f'<circle cx="{x+w/2-2}" cy="{y+12}" r="1.4" fill="#d6e0e7"/>')
    else:
        circle=len(label)==1 and w==32
        if circle:svg.append(f'<circle cx="{x+16}" cy="{y+17}" r="14.5" fill="#05090e"/><circle cx="{x+16}" cy="{y+15.5}" r="14" fill="url(#face)" stroke="#161a1e" stroke-width="1"/>')
        else:svg.append(f'<rect x="{x+1}" y="{y+4}" width="{w-2}" height="25" rx="1.5" fill="#05090e"/><rect x="{x+1}" y="{y+2}" width="{w-2}" height="25" rx="1.5" fill="url(#body)" stroke="#4a525b" stroke-width=".7"/>')
        if circle:
            svg.append(f'<path d="M{x+5} {y+9} Q{x+12} {y+1} {x+22} {y+5}" fill="none" stroke="#d0d1d0" stroke-opacity=".32" stroke-width="1"/>')
        else:
            svg.append(f'<path d="M{x+2} {y+25} V{y+3} H{x+w-2}" fill="none" stroke="#b4b9bd" stroke-width="1"/><path d="M{x+2} {y+27} H{x+w-2} V{y+4}" fill="none" stroke="#090c11" stroke-width="1.5"/>')
        inset=8 if circle else 6
        color={'A':'#e74b42','B':'#f3cb36','X':'#3897d5','Y':'#62af55'}.get(label,'#a8d8f4')
        if label in ('L','R'):
            svg.append(lettering(label,x+inset,y+8,w-2*inset,14,color,1.25))
        else:
            svg.append(lettering(label,x+inset,y+(6.5 if circle else 6),w-2*inset,18,color,1.4 if circle else 1.25))
svg.append('</svg>');OUT.mkdir(exist_ok=True)
(OUT/'buttons-3ds.svg').write_text('\n'.join(svg)+'\n')
subprocess.run(['rsvg-convert','-o',str(OUT/'buttons-3ds.png'),str(OUT/'buttons-3ds.svg')],check=True)
img=Image.open(OUT/'buttons-3ds.png').convert('RGBA')
# Store only changed glyph rectangles; unrelated ROM glyphs remain untouched.
values=[];entries=[]
for x,y,w,label in RECTS:
    entries.append((x,y,w,len(values)))
    for py in range(y,y+32):
        for px in range(x,x+w):
            r,g,b,a=img.getpixel((px,py));values.append((r>>3)<<11|(g>>3)<<6|(b>>3)<<1|(a>=64))
text='// Generated by make_buttons.py; original 3DS button artwork only.\n'
text+='static constexpr struct {unsigned x,y,width,offset;} caneButtonRects[]={\n'+''.join('    {%d,%d,%d,%d},\n'%e for e in entries)+'};\n'
text+='static constexpr uint16_t caneButtonPixels[]={\n'+''.join('    '+','.join(hex(v) for v in values[i:i+24])+',\n' for i in range(0,len(values),24))+'};\n'
(OUT/'buttons-3ds.inc').write_text(text)
print('Generated 3DS buttons:',len(values)*2,'bytes; SVG, PNG and native include in',OUT)
