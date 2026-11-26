#!/usr/bin/env python3
"""Use the supplied achievement sound as the HOME Menu banner jingle."""
from pathlib import Path
import argparse,shutil,wave
if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--input',type=Path,default=Path(__file__).resolve().parents[1]/'achievement.wav')
    p.add_argument('--output',type=Path,default=Path(__file__).with_name('jingle.wav'))
    a=p.parse_args()
    with wave.open(str(a.input),'rb') as f:
        if f.getsampwidth()!=2 or f.getnchannels() not in (1,2) or f.getcomptype()!='NONE' or f.getnframes()/f.getframerate()>3:
            raise SystemExit('Banner jingle requires mono/stereo PCM16 WAV, up to 3 seconds')
    if a.input.resolve()!=a.output.resolve():shutil.copy2(a.input,a.output)
    print(f'HOME Menu jingle: {a.output}')
