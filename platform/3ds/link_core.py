#!/usr/bin/env python3
"""Check linkage of the actual native runtime, without launching it."""
from pathlib import Path
import json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/3ds';CORE=OUT/'core'
DK=Path(os.environ.get('DEVKITPRO','/opt/devkitpro'))
DSP=ROOT/'platform/3ds/vendor/OpenDSP/library/lib/libopendsp.a'
objects=[CORE/'runtime_probe.o',OUT/'runtime-shader.o',CORE/'libcane-gameplay.a',DSP]
if any(not path.is_file() for path in objects):raise SystemExit('Build with build_runtime.py before checking linkage.')
report=json.loads((CORE/'compile-report.json').read_text())
if len(report)<250 or not all(row['compiled'] for row in report):raise SystemExit('The original runtime must compile successfully first.')
command=[str(DK/'devkitARM/bin/arm-none-eabi-g++'),'-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-specs=3dsx.specs',
         '-Wl,--wrap=abort,--wrap=exit,--wrap=_exit,--gc-sections',*[str(path) for path in objects],
         '-L'+str(DK/'libctru/lib'),'-lcitro3d','-lctru','-lm','-o',str(CORE/'link-probe.elf')]
result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(CORE/'link-probe.log').write_text(result.stdout)
missing=sorted(set(re.findall(r"undefined reference to [`‘'](.+?)[’']",result.stdout)))
summary=dict(linked=result.returncode==0,linker_exit_code=result.returncode,executable_tested=False,
             missing_count=len(missing),missing_symbols=missing)
(CORE/'link-report.json').write_text(json.dumps(summary,indent=2)+'\n')
print('Linked:',summary['linked']);print('Unresolved symbols:',len(missing))
if result.returncode:print(result.stdout[-6000:])
raise SystemExit(result.returncode)
