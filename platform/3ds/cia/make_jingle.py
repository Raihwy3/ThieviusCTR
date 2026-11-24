#!/usr/bin/env python3
"""Synthesize an original short HOME Menu motif. No sampled game audio."""
from pathlib import Path
import argparse, math, random, struct, wave

def synthesize(path):
    rate=32000; duration=2.8; size=round(rate*duration)
    channels=[[0.0]*size for _ in range(2)]
    def note(start,midi,level,decay,pan=0,bass=False):
        frequency=440*2**((midi-69)/12)
        for i in range(min(size-round(start*rate),round(1.9*rate))):
            t=i/rate; attack=min(1,t/.006)
            if bass:
                value=math.sin(2*math.pi*frequency*t)+.18*math.sin(4*math.pi*frequency*t)
            else:
                value=math.sin(2*math.pi*frequency*t)+.23*math.sin(2*math.pi*frequency*2.005*t)*math.exp(-5*t)+.08*math.sin(2*math.pi*frequency*3.98*t)*math.exp(-9*t)
            value*=attack*math.exp(-decay*t)*level
            at=round(start*rate)+i
            channels[0][at]+=value*(1-pan)*.5
            channels[1][at]+=value*(1+pan)*.5
    # A compact, original minor-key bell phrase over a muted plucked bass.
    for start,midi,level,pan in [(0,74,.45,-.15),(.16,77,.42,.1),(.32,80,.35,-.1),(.50,81,.40,.15),(.88,77,.34,-.15),(1.12,86,.47,.1)]:
        note(start,midi,level,4.2,pan)
    for start,midi in [(0,38),(.50,45),(1.12,50)]:note(start,midi,.28,5,0,True)
    for midi in (62,69,77):note(1.17,midi,.12,3.1,0)
    for channel in channels:
        dry=channel[:]
        for delay,gain in ((.073,.13),(.137,.08),(.211,.04)):
            offset=round(delay*rate)
            for i in range(offset,size):channel[i]+=dry[i-offset]*gain
    peak=max(abs(v) for channel in channels for v in channel)
    pcm=bytearray()
    for i in range(size):
        fade=min(1,i/(rate*.005),(size-1-i)/(rate*.18))
        for channel in channels:pcm.extend(struct.pack('<h',round(channel[i]/peak*.76*max(0,fade)*32767)))
    path.parent.mkdir(parents=True,exist_ok=True)
    with wave.open(str(path),'wb') as f:
        f.setparams((2,2,rate,0,'NONE','not compressed'));f.writeframes(pcm)
    print(f'Original jingle: {path} ({duration}s, stereo 32 kHz PCM16)')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=Path(__file__).with_name('jingle.wav'));a=p.parse_args();synthesize(a.output)
