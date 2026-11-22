#!/usr/bin/env python3
"""Select the upstream 989 music parser, AME, MIDI and envelope code."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def prepare(out):
    source=(ROOT/'Sly1-Release/sound.cpp').read_text()
    sections=[('uint16_t MusicLe16(', 'bool DecodeMusicAdpcm('),
              ('bool LoadMusic989Mus(', 'void LoadMusicAdpcmLoopPoints('),
              ('bool ReadMusicVlq(', 'void SetGlobalExciteValue('),
              ('static void StopMusicVoicesOnChannel(', 'void SetMusicVoicePan('),
              ('static void RefreshMusicVoiceControllerState(', 'static bool ReserveMusicHardwareVoice('),
              ('void StopMusicTone(', 'bool DecodeEffectAdpcm('),
              ('static void ResetMusicSegmentForLoop(MusicSegment& segment)\n{', 'void ContinuePamb('),
              ('static void PrimeMusicSequencerStartup()\n{', 'void RefreshMidiRegister(')]
    text='// Generated from ProjectCane sound.cpp; native output is separate.\n'
    for begin,end in sections:
        a=source.index(begin);part=source[a:source.index(end,a)]
        part=part.replace('static thread_local int recursionDepth','static int recursionDepth')
        part=part.replace('(std::max)(1u, counterStep)','std::max<uint32_t>(1, counterStep)').replace('(std::max)(1, segment.ppq)','std::max<int32_t>(1, segment.ppq)')
        text+=part+'\n'
    target=out/'native_music_shared.inc'
    if not target.exists() or target.read_text()!=text:target.write_text(text)
    return target
if __name__=='__main__':
    out=ROOT/'build/3ds/core';out.mkdir(parents=True,exist_ok=True);print(prepare(out))
