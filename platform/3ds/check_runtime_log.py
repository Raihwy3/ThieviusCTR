#!/usr/bin/env python3
"""Check the isolated native gameplay test's original-engine state changes."""
from pathlib import Path
import argparse,json,math,re
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('log',nargs='?',type=Path,default=Path(__file__).resolve().parents[2]/'build/3ds/emulator-runtime/data/azaharplus-emu/sdmc/3ds/ThieviusCTR/runtime.log')
args=parser.parse_args();text=args.log.read_text()
if 'Native runtime probe exited' not in text:raise SystemExit('FAIL: no clean exit')
if 'probe stopped' in text or 'abort code=' in text:raise SystemExit('FAIL: runtime stopped unexpectedly')
points=[];events=[]
body=text.rsplit('Original title menu:',1)[-1]
for line in body.splitlines():
    match=re.fullmatch(r'(?:updates=|jump update=)(\d+) pos=([\d.,-]+)',line)
    if match:points.append((int(match[1]),tuple(map(float,match[2].split(',')))))
    match=re.fullmatch(r'state=(\d+)->(\d+) update=(\d+) pos=([\d.,-]+)',line)
    if match:events.append((int(match[1]),int(match[2]),int(match[3])))
if not points or not all(math.isfinite(v) for _,pos in points for v in pos):raise SystemExit('FAIL: invalid position samples')
motion=next((tick for old,new,tick in events if old==0 and new==1),float('inf'))
jump=next((tick for old,new,tick in reversed(events) if old==0 and new==2 and points[0][0]<tick<motion),None)
land=next((tick for old,new,tick in events if old==2 and new==0 and jump is not None and tick>jump),None)
if jump is None or land is None:raise SystemExit('FAIL: missing original jump/stand transition')
before=[pos for tick,pos in points if tick<jump][-1]
airborne=[pos for tick,pos in points if jump<=tick<land]
after=[pos for tick,pos in points if tick>land][0]
if not airborne or max(p[2] for p in airborne)<before[2]+80:raise SystemExit('FAIL: no jump rise')
if math.dist(before,after)>10:raise SystemExit('FAIL: did not settle back on the starting roof')
if math.dist(before,points[-1][1])<100:raise SystemExit('FAIL: movement did not advance Sly')
report=dict(jump_update=jump,landing_update=land,jump_rise=max(p[2] for p in airborne)-before[2],landing_error=math.dist(before,after),movement_distance=math.dist(before,points[-1][1]),clean_exit=True)
print('PASS: original jump, landing, movement and clean exit')
print(json.dumps(report,indent=2))
