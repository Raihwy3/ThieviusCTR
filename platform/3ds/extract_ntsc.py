#!/usr/bin/env python3
"""Extract selected US Sly 1 levels using ProjectCane's native BRX rebuilder.
Level-table and LZSS logic follows Services/Sly1ExtractionService.cs.
Only the user's ISO is read; no disc data is stored in source files.
"""
from pathlib import Path
import argparse,concurrent.futures,hashlib,json,shutil,struct,subprocess
ROOT=Path(__file__).resolve().parents[2]
TOOLS=ROOT/'File Extractor GUI/NativeTools/BrxRebuilder'
EPISODES={1:'Tide of Terror',2:'Sunset Snake Eyes',3:'Vicious Voodoo',4:'Fire in the sky',5:'The Cold Heart of Hate'}
def decompress(data):
    work=bytearray(8202);result=bytearray();ip=op=flags=0
    while ip<len(data):
        flags>>=1
        if not flags&0x100:
            flags=0xff00|data[ip];ip+=1
        if ip>=len(data):break
        first=data[ip];ip+=1
        if flags&1:work[op]=first;op+=1
        else:
            if ip>=len(data):raise ValueError('truncated compressed match')
            token=first|(data[ip]<<8);ip+=1;source=token&8191
            for _ in range((token>>13)+3):
                work[op]=work[source&8191];op+=1;source+=1
        if op>=8192:
            result.extend(work[:8192]);spill=op-8192;work[:spill]=work[8192:op];op=spill
            if len(result)>256*1024*1024:raise ValueError('decompressed level too large')
    result.extend(work[:op]);return result

def build_rebuilder():
    out=ROOT/'build/host-tools';out.mkdir(parents=True,exist_ok=True)
    sources=sorted(TOOLS.glob('*.cpp'));headers=list(TOOLS.glob('*.h'))+list(TOOLS.glob('*.inc'))
    def compile_one(source):
        obj=out/(source.stem+'.o')
        if not obj.exists() or obj.stat().st_mtime<max(p.stat().st_mtime for p in [source,*headers]):
            subprocess.run(['g++','-std=c++20','-O2','-Wall','-c',str(source),'-o',str(obj)],check=True)
        return obj
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:objects=list(pool.map(compile_one,sources))
    exe=out/'Sly1BrxRebuilder'
    if not exe.exists() or exe.stat().st_mtime<max(p.stat().st_mtime for p in objects):
        subprocess.run(['g++',*[str(p) for p in objects],'-o',str(exe)],check=True)
    return exe

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('iso',type=Path)
    parser.add_argument('--output',type=Path,default=ROOT/'local/data')
    parser.add_argument('--level-id',action='append',type=lambda s:int(s,0),help='repeatable; defaults to Splash and Paris')
    parser.add_argument('--all',action='store_true',help='extract all 46 levels from the ISO')
    parser.add_argument('--force',action='store_true',help='re-extract levels even if valid BRX already exists')
    parser.add_argument('--list',action='store_true');args=parser.parse_args()
    iso=args.iso.resolve()
    cnf=subprocess.check_output(['7z','e','-so',str(iso),'SYSTEM.CNF'],stderr=subprocess.DEVNULL)
    if b'SCUS_971.98' not in cnf:raise ValueError('This extractor currently requires the US SCUS_971.98 disc')
    elf=subprocess.check_output(['7z','e','-so',str(iso),'SCUS_971.98'],stderr=subprocess.DEVNULL)
    if elf[:4]!=b'\x7fELF':raise ValueError('Not an ELF executable')
    levels=[]
    for i in range(46):
        row=struct.unpack_from('<11I',elf,0x148af0+i*44);at=row[9]-0x100000+0x1000
        if at<0 or at>=len(elf):raise ValueError('Invalid level-name pointer')
        end=elf.index(b'\0',at,min(at+256,len(elf)));name=elf[at:end].decode('ascii')
        levelid=row[8];name='Paris' if levelid&0xffff==3 else name
        if any(c in name for c in '/\\') or name in ('.','..'):raise ValueError('Unsafe level name')
        offset=(row[0]^row[5])*2048;size=row[1]^row[7]
        if not 0<size<=256*1024*1024 or offset+size>iso.stat().st_size:raise ValueError('Level outside ISO')
        levels.append((levelid,name,offset,size))
    if args.list:
        for ident,name,offset,size in levels:print(f'{ident:#06x} {name} ({size} compressed bytes)')
        return
    selected={v[0] for v in levels} if args.all else set(args.level_id if args.level_id else (2,3))
    if selected-{v[0] for v in levels}:raise ValueError('Unknown level ID')
    tool=build_rebuilder();report=[]
    todo=[]
    for ident,name,offset,size in levels:
        if ident not in selected:continue
        folder=args.output/'Worlds'/EPISODES.get(ident>>8,'')/name;folder.mkdir(parents=True,exist_ok=True)
        target=folder/(name+'.brx')
        if target.is_file() and not args.force:
            with target.open('rb') as f:magic,version=struct.unpack('<II',f.read(8))
            if magic==0x52424350 and version==17:
                data=target.read_bytes()
                report.append(dict(id=ident,name=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),file=str(target)))
                print('Existing:',target,'bytes:',len(data),flush=True)
                continue
        todo.append((ident,name,offset,size,target))
    def convert_one(item):
        ident,name,offset,size,target=item
        print('Extracting:',name,flush=True)
        with iso.open('rb') as disc:
            disc.seek(offset);compressed=disc.read(size)
        raw=ROOT/'build/raw-levels'/name
        shutil.rmtree(raw,ignore_errors=True)
        raw.mkdir(parents=True,exist_ok=True)
        try:
            source=raw/(name+'.brx');source.write_bytes(decompress(compressed))
            with (raw/'conversion.log').open('w') as log:
                subprocess.run([str(tool),str(source),'ntsc'],stdout=log,stderr=subprocess.STDOUT,check=True)
            rebuilt=source.with_name(name+'_Rebuilt.brx')
            with rebuilt.open('rb') as f:magic,version=struct.unpack('<II',f.read(8))
            if magic!=0x52424350 or version!=17:raise ValueError('Unexpected converted BRX header for '+name)
            data=rebuilt.read_bytes()
            target.write_bytes(data)
            print('Converted:',target,'bytes:',len(data),flush=True)
            return dict(id=ident,name=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),file=str(target))
        finally:
            shutil.rmtree(raw,ignore_errors=True)
    if todo:
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            report.extend(pool.map(convert_one,todo))
    report.sort(key=lambda r:r['id'])
    args.output.mkdir(parents=True,exist_ok=True)
    (args.output/'extraction-report.json').write_text(json.dumps(dict(serial='SCUS_971.98',executable_sha256=hashlib.sha256(elf).hexdigest(),levels=report),indent=2)+'\n')
if __name__=='__main__':main()
