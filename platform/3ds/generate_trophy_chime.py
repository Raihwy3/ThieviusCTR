#!/usr/bin/env python3
"""Convert the supplied achievement WAV to embedded mono trophy audio."""
from pathlib import Path
import argparse,array,sys,wave
ROOT=Path(__file__).resolve().parents[2]
def convert(source):
    with wave.open(str(source),'rb') as f:
        rate=f.getframerate();channels=f.getnchannels()
        if f.getsampwidth()!=2 or channels not in (1,2) or f.getcomptype()!='NONE':
            raise SystemExit('Expected mono/stereo PCM16 WAV')
        pcm=array.array('h',f.readframes(f.getnframes()))
    if sys.byteorder!='little':pcm.byteswap()
    if channels==2:pcm=array.array('h',((int(pcm[i])+int(pcm[i+1]))//2 for i in range(0,len(pcm),2)))
    if not pcm or not any(pcm):raise SystemExit('Achievement audio is empty or silent')
    text='// Supplied achievement sound, converted by generate_trophy_chime.py.\n'
    text+=f'static constexpr unsigned kTrophyChimeRate={rate};\nstatic constexpr int16_t kTrophyChimePcm[]={{\n'
    text+=''.join('    '+','.join(map(str,pcm[i:i+16]))+',\n' for i in range(0,len(pcm),16))+'};\n'
    (ROOT/'platform/3ds/trophy_chime.inc').write_text(text)
    preview=ROOT/'local/validation/trophy-chime.wav';preview.parent.mkdir(parents=True,exist_ok=True)
    if sys.byteorder!='little':pcm.byteswap()
    with wave.open(str(preview),'wb') as f:
        f.setnchannels(1);f.setsampwidth(2);f.setframerate(rate);f.writeframes(pcm.tobytes())
    print(f'Trophy audio: {len(pcm)} mono samples, {rate} Hz ({len(pcm)/rate:.3f}s)')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--input',type=Path,default=ROOT/'platform/3ds/achievement.wav')
    convert(p.parse_args().input)
