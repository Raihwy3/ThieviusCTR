#!/usr/bin/env python3
"""Build a standalone installed title, sharing the existing 3DSX data folder.

Package validation adapted from Conker3DSRecomp's CIA build checks.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, struct, subprocess, sys, wave, zipfile
ROOT=Path(__file__).resolve().parents[2]
TITLE_ID=0x000400000E054C00

def run(args):subprocess.run(list(map(str,args)),check=True)
def tool(name,override):
    value=override or os.environ.get(name.upper()) or shutil.which(name)
    if not value:
        local=ROOT/'local/cia-tools'/name
        if local.is_file():value=str(local)
    if not value:raise SystemExit('Install '+name+' or pass --'+name+' /path/to/'+name)
    return value

def validate(cia,elf,smdh,banner):
    from elftools.elf.elffile import ELFFile
    raw=cia.read_bytes();header,kind,version,cert,ticket,tmd,meta,content=struct.unpack_from('<IHHIIIIQ',raw)
    align=lambda x:(x+63)&~63
    at=align(header)+align(cert)+align(ticket)+align(tmd)
    ncch=raw[at:at+content];assert ncch[0x100:0x104]==b'NCCH'
    assert struct.unpack_from('<Q',ncch,0x118)[0]==TITLE_ID
    assert ncch[0x18f]&4
    assert ncch[0x40f]==0x30, 'Invalid native main-thread priority'
    assert struct.unpack_from('<II',ncch,0x1b0)==(0,0), 'Game assets must not be bundled in RomFS'
    start,blocks=struct.unpack_from('<II',ncch,0x1a0);exefs=ncch[start*512:(start+blocks)*512];entries={}
    for i in range(10):
        name,offset,size=struct.unpack_from('<8sII',exefs,i*16);name=name.rstrip(b'\0').decode()
        if name:
            data=exefs[512+offset:512+offset+size];assert len(data)==size
            assert hashlib.sha256(data).digest()==exefs[0xc0+(9-i)*32:0xc0+(10-i)*32];entries[name]=data
    assert set(entries)=={'.code','icon','banner','logo'}
    assert entries['icon']==smdh.read_bytes() and entries['banner']==banner.read_bytes()
    assert hashlib.sha256(entries['logo']).hexdigest()=='62a5a1f9091aefb46b52e31fbeca2fdba9a99fe2473237e21e35b8d2e5659dff'
    with elf.open('rb') as f:
        e=ELFFile(f);loads=[s for s in e.iter_segments() if s['p_type']=='PT_LOAD'];base=min(s['p_vaddr'] for s in loads)
        for s in loads:
            offset=s['p_vaddr']-base;assert entries['.code'][offset:offset+s['p_filesz']]==s.data()
    assert b'sdmc:/3ds/ThieviusCTR' in entries['.code']
    assert b'CIA UPDATE READY' in entries['.code'], 'CIA update download flow is missing'
    return {'title_id':f'{TITLE_ID:016X}','sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw),'payload_matches_elf':True,'romfs_bundled':False,'console_tested':False}

def main():
    p=argparse.ArgumentParser();p.add_argument('--makerom');p.add_argument('--bannertool');p.add_argument('--skip-build',action='store_true');a=p.parse_args()
    makerom=tool('makerom',a.makerom);bannertool=tool('bannertool',a.bannertool)
    if not a.skip_build:run([sys.executable,ROOT/'platform/3ds/build_runtime.py','--cia'])
    assets=ROOT/'platform/3ds/cia';build=ROOT/'build/3ds';out=build/'cia';out.mkdir(parents=True,exist_ok=True)
    elf=build/'ThieviusCTR-cia.elf'
    if not elf.is_file():raise SystemExit('Build the CIA ELF first.')
    from PIL import Image
    banner_cgfx=assets/'banner.cgfx';jingle=assets/'jingle.wav'
    if not banner_cgfx.is_file():raise SystemExit('Generate platform/3ds/cia/banner.cgfx with make_banner.py first.')
    cgfx=banner_cgfx.read_bytes();assert cgfx[:4]==b'CGFX' and len(cgfx)<=0x80000 and b'CANM' in cgfx,'Expected an animated CGFX under 512 KiB'
    with wave.open(str(jingle),'rb') as f:
        assert f.getsampwidth()==2 and f.getnchannels() in (1,2) and f.getnframes()/f.getframerate()<=3
        assert any(f.readframes(f.getnframes())), 'Jingle must not be silent'
    with Image.open(assets/'icon-source.png') as image:image.convert('RGB').resize((48,48),Image.Resampling.LANCZOS).save(out/'icon.png')
    smdh=out/'ThieviusCTR.smdh';banner=out/'banner.bnr'
    dk=Path(os.environ.get('DEVKITPRO','/opt/devkitpro'))
    run([dk/'tools/bin/smdhtool','--create','ThieviusCTR','Sly Cooper 3DS launcher & port','Raihwy3 / ProjectCane',out/'icon.png',smdh])
    run([bannertool,'makebanner','-ci',banner_cgfx,'-a',jingle,'-o',banner])
    version=re.search(r'#define CANE_ENGINE_VERSION "([0-9.]+)"',(ROOT/'platform/3ds/version_3ds.h').read_text()).group(1)
    release_tag=re.search(r'#define CANE_RELEASE_TAG "([^"]+)"',(ROOT/'platform/3ds/version_3ds.h').read_text()).group(1)
    assert re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+(?:-r[0-9]+)?',release_tag), 'Invalid release tag'
    major,minor,micro=map(int,version.split('.'))
    dest=ROOT/'dist'/('ThieviusCTR-'+release_tag+'.cia');dest.parent.mkdir(exist_ok=True)
    run([makerom,'-f','cia','-o',dest,'-elf',elf,'-rsf',assets/'launcher.rsf','-icon',smdh,'-banner',banner,'-exefslogo','-target','t','-ver',(major<<10)|(minor<<4)|micro])
    report=validate(dest,elf,smdh,banner);report['version']=version;report['release_tag']=release_tag
    run([sys.executable,ROOT/'platform/3ds/vendor/OpenDSP/tools/check_cia.py',dest,version])
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    package=ROOT/'dist'/('cia-ThieviusCTR-'+release_tag+'.zip')
    with zipfile.ZipFile(package,'w',zipfile.ZIP_DEFLATED) as z:
        z.write(dest,dest.name)
        z.write(assets/'README.txt','README.txt')
        z.write(ROOT/'platform/3ds/CREDITS.txt','CREDITS.txt')
        z.write(assets/'makerom-logo-LICENSE.txt','LICENSE-makerom-logo.txt')
        z.write(assets/'pycgfx-LICENSE.txt','LICENSE-pycgfx.txt')
        for source,name in [('vendor/OpenDSP/LICENSE','LICENSE-OpenDSP.txt'),('LICENSE-FFmpeg-LGPL2.1.txt','LICENSE-FFmpeg-LGPL2.1.txt'),('LICENSE-Mozilla-MPL-2.0.txt','LICENSE-Mozilla-MPL-2.0.txt')]:z.write(ROOT/'platform/3ds'/source,name)
    with zipfile.ZipFile(package) as z:assert z.testzip() is None
    print(json.dumps(report,indent=2));print(package)
if __name__=='__main__':main()
