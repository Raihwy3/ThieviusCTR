#!/usr/bin/env python3
"""Reuse upstream's bank parser, ADPCM decoder and effect command scheduler."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def prepare(out):
    source=(ROOT/'Sly1-Release/sound.cpp').read_text()
    sections=[('template <typename T>\nbool ReadBankValue','std::filesystem::path ExecutableDirectory'),
              ('bool DecodeEffectAdpcm(', 'void SetEffectVoicePan('),
              ('int16_t EffectControlParam(', 'void ApplyEffectPitchBend('),
              ('void ExecuteEffectGrain(', 'void StartSound(')]
    text='// Generated from the original ProjectCane sound.cpp; do not edit.\n'
    for start,end in sections:
        begin=source.index(start);finish=source.index(end,begin)
        text+=source[begin:finish]+'\n'
    target=out/'native_audio_shared.inc'
    if not target.exists() or target.read_text()!=text:target.write_text(text)
    return target
if __name__=='__main__':
    out=ROOT/'build/3ds/core';out.mkdir(parents=True,exist_ok=True);print(prepare(out))
