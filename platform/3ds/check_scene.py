#!/usr/bin/env python3
"""Validate a generated scene against the native loader's memory/layout limits."""
from pathlib import Path
import argparse,hashlib,json,math,struct
ROOT=Path(__file__).resolve().parents[2]
def check(path):
 data=path.read_bytes();h=struct.unpack_from('<6I9f',data);magic,version,nv,nb,nt,skipped=h[:6]
 assert magic==0x454e4143 and version==1
 assert 0<nv<=700000 and 0<nb<=65536 and 0<nt<=2048
 assert all(math.isfinite(x) for x in h[6:])
 commands=[];at=60
 for i in range(nb):
  row=struct.unpack_from('<4I4f',data,at);at+=32;first,count,tex,priority,*sphere=row
  assert first<=nv and count<=nv-first and count%3==0 and tex<nt
  assert all(math.isfinite(v) for v in sphere) and sphere[3]>=0
  assert first==(0 if not commands else commands[-1][0]+commands[-1][1])
  commands.append(row)
 assert commands[-1][0]+commands[-1][1]==nv
 vertex_base=at;at+=nv*36
 for first,count,tex,priority,x,y,z,radius in commands:
  for index in range(first,first+count):
   vertex=struct.unpack_from('<9f',data,vertex_base+index*36)
   assert all(math.isfinite(x) for x in vertex)
   # The culling sphere must contain every vertex it controls.
   assert math.dist(vertex[:3],(x,y,z))<=radius+max(.02,radius*1e-5)
   assert all(h[9+i]-.1<=vertex[i]<=h[12+i]+.1 for i in range(3))
 texture_bytes=0
 for i in range(nt):
  w,height,size,flags=struct.unpack_from('<4I',data,at);at+=16
  assert 8<=w<=128 and 8<=height<=128 and not w&(w-1) and not height&(height-1)
  assert size==w*height*2 and at+size<=len(data)
  at+=size;texture_bytes+=size
 assert at==len(data) and texture_bytes<=12*1024*1024
 # Export instrumentation must not change the converted world payload.
 raw=ROOT/'build/raw-levels/Paris/Paris_Rebuilt.brx';baseline=ROOT/'local/data/Worlds/Paris/Paris.brx'
 assert hashlib.sha256(raw.read_bytes()).digest()==hashlib.sha256(baseline.read_bytes()).digest()
 report=dict(triangles=nv//3,batches=nb,textures=nt,skipped_batches=skipped,vertex_bytes=nv*36,texture_bytes=texture_bytes,scene_bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),upstream_conversion_byte_identical=True,hardware_tested=False)
 out=ROOT/'build/3ds/scene-validation.json';out.write_text(json.dumps(report,indent=2)+'\n')
 print('PASS scene: finite vertices, complete triangle ranges, conservative visibility spheres, texture bounds, memory limits and byte-identical upstream conversion')
 print(json.dumps(report,indent=2))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('scene',nargs='?',type=Path,default=ROOT/'local/data/Paris.cscene');check(p.parse_args().scene)
