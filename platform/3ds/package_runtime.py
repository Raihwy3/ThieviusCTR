#!/usr/bin/env python3
"""Package and sync the native 3DS runtime; converted game data stays in local/."""
from pathlib import Path
import argparse,os,shutil,zipfile
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/3ds'
BASE='3ds/ThieviusCTR/'

def sync_local_sd():
    sd=ROOT/'local/3ds/ThieviusCTR'
    sd.mkdir(parents=True,exist_ok=True)
    for src,dst_name in [
        (OUT/'ThieviusCTR.3dsx','ThieviusCTR.3dsx'),
        (OUT/'ThieviusCTR.smdh','ThieviusCTR.smdh'),
        (ROOT/'platform/3ds/README-runtime.txt','README-runtime.txt'),
        (ROOT/'platform/3ds/CREDITS.txt','CREDITS.txt'),
        (ROOT/'platform/3ds/vendor/OpenDSP/LICENSE','LICENSE-OpenDSP.txt'),
        (ROOT/'platform/3ds/LICENSE-FFmpeg-LGPL2.1.txt','LICENSE-FFmpeg-LGPL2.1.txt'),
        (ROOT/'platform/3ds/LICENSE-Mozilla-MPL-2.0.txt','LICENSE-Mozilla-MPL-2.0.txt'),
    ]:
        if not src.is_file():raise SystemExit('Missing: '+str(src))
        shutil.copy2(src,sd/dst_name)
    for opt_name in ('extraction_manifest.json','assets-ready.stamp'):
        opt_src=ROOT/'local/data'/opt_name
        if opt_src.is_file():shutil.copy2(opt_src,sd/opt_name)
    for tree in ('Worlds','Cutscenes'):
        src_root=ROOT/'local/data'/tree
        dst_root=sd/tree
        if not src_root.exists():continue
        expected=set()
        for src in src_root.rglob('*'):
            rel=src.relative_to(src_root)
            expected.add(rel)
            dst=dst_root/rel
            if src.is_dir():
                dst.mkdir(parents=True,exist_ok=True)
            elif src.is_file():
                dst.parent.mkdir(parents=True,exist_ok=True)
                src_st=src.stat()
                if dst.exists() and not dst.is_symlink():
                    dst_st=dst.stat()
                    if dst_st.st_ino==src_st.st_ino and dst_st.st_dev==src_st.st_dev:continue
                    if dst_st.st_size==src_st.st_size and int(dst_st.st_mtime)==int(src_st.st_mtime):
                        try:
                            dst.unlink();os.link(src,dst);continue
                        except OSError:
                            continue
                    dst.unlink()
                elif dst.is_symlink():
                    dst.unlink()
                try:
                    os.link(src,dst)
                except OSError:
                    shutil.copy2(src,dst)
        if dst_root.exists():
            for existing in sorted(dst_root.rglob('*'),reverse=True):
                rel=existing.relative_to(dst_root)
                if rel not in expected:
                    if existing.is_file() or existing.is_symlink():existing.unlink()
                    elif existing.is_dir():shutil.rmtree(existing,ignore_errors=True)
    print('Synced SD folder:',sd)
    share=Path.home()/'.local/share'
    for app in ('azaharplus-emu','azahar-emu','citra-emu'):
        app_dir=share/app
        if not app_dir.is_dir():continue
        emu_sd=app_dir/'sdmc/3ds/ThieviusCTR'
        emu_sd.mkdir(parents=True,exist_ok=True)
        for name in ('ThieviusCTR.3dsx','ThieviusCTR.smdh'):
            src=OUT/name
            if src.is_file():shutil.copy2(src,emu_sd/name)
        for opt_name in ('extraction_manifest.json','assets-ready.stamp'):
            opt_src=ROOT/'local/data'/opt_name
            if opt_src.is_file():shutil.copy2(opt_src,emu_sd/opt_name)
        for tree in ('Worlds','Cutscenes'):
            src=ROOT/'local/data'/tree
            if not src.is_dir():continue
            dst=emu_sd/tree
            if dst.is_symlink():
                if dst.resolve()==src.resolve():continue
                dst.unlink()
            elif dst.is_dir():shutil.rmtree(dst)
            elif dst.exists():dst.unlink()
            dst.symlink_to(src)
        print('Synced emulator SD:',emu_sd)

def package(path,assets):
    files=[(OUT/name,BASE+name) for name in ('ThieviusCTR.3dsx','ThieviusCTR.smdh')]
    files.append((ROOT/'platform/3ds/README-runtime.txt',BASE+'README-runtime.txt'))
    files.extend([
        (ROOT/'platform/3ds/CREDITS.txt',BASE+'CREDITS.txt'),
        (ROOT/'platform/3ds/vendor/OpenDSP/LICENSE',BASE+'LICENSE-OpenDSP.txt'),
        (ROOT/'platform/3ds/LICENSE-FFmpeg-LGPL2.1.txt',BASE+'LICENSE-FFmpeg-LGPL2.1.txt'),
        (ROOT/'platform/3ds/LICENSE-Mozilla-MPL-2.0.txt',BASE+'LICENSE-Mozilla-MPL-2.0.txt'),
    ])
    if assets:
        for opt_name in ('extraction_manifest.json','assets-ready.stamp'):
            opt_src=ROOT/'local/data'/opt_name
            if opt_src.is_file():files.append((opt_src,BASE+opt_name))
        for tree in ('Worlds','Cutscenes'):
            root=ROOT/'local/data'/tree
            if not root.exists():raise SystemExit('Missing asset tree: '+str(root))
            for item in sorted(root.rglob('*')):
                if item.is_file():files.append((item,BASE+str(item.relative_to(ROOT/'local/data'))))
    for source,_ in files:
        if not source.is_file():raise SystemExit('Missing: '+str(source))
    path.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as archive:
        for source,name in files:archive.write(source,name)
    with zipfile.ZipFile(path) as archive:
        if archive.testzip():raise SystemExit('Package checksum failure')
    print(('Personal full package (contains your converted game data): ' if assets else 'Asset-free package: ')+str(path))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zip-assets',action='store_true',help='also create local/ThieviusCTR-full.zip containing all assets')
    args=parser.parse_args()
    package(ROOT/'dist/ThieviusCTR-test.zip',False)
    sync_local_sd()
    if args.zip_assets:package(ROOT/'local/ThieviusCTR-full.zip',True)
