#!/usr/bin/env python3
"""Install a native test and its local game data into Azahar's SD folder."""
from pathlib import Path
import argparse,os,re,shutil,subprocess
ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install-only',action='store_true')
    parser.add_argument('--runtime',action='store_true',help='use original-engine Paris gameplay instead of the free-camera viewer')
    parser.add_argument('--sd-root',type=Path,help='override Azahar SD directory')
    args=parser.parse_args()
    config=Path(os.environ.get('XDG_CONFIG_HOME',str(Path.home()/'.config')))
    data=Path(os.environ.get('XDG_DATA_HOME',str(Path.home()/'.local/share')))
    candidates=[]
    for app in ('azaharplus-emu','azahar-emu','citra-emu'):
        ini=config/app/'qt-config.ini'
        if not ini.exists():continue
        match=re.search(r'^sdmc_directory=(.+)$',ini.read_text(),re.M)
        sd=Path(match.group(1).strip().strip('"')) if match else data/app/'sdmc'
        candidates.append(sd)
    if args.sd_root:candidates=[args.sd_root]
    if not candidates:candidates=[data/'azaharplus-emu/sdmc']
    binary=ROOT/('build/3ds/ThieviusCTR.3dsx' if args.runtime else 'build/3ds/ThieviusCTR-viewer.3dsx');scene=ROOT/'local/data/Paris.cscene'
    files=[binary,binary.with_suffix('.smdh')]
    if not args.runtime:files.append(scene)
    missing=[str(path) for path in files if not path.is_file()]
    if args.runtime:
        for tree in ('Worlds','Cutscenes'):
            if not (ROOT/'local/data'/tree).is_dir():missing.append(str(ROOT/'local/data'/tree))
    if missing:raise SystemExit('Missing test files:\n'+'\n'.join(missing)+'\nSee platform/3ds/README.txt.')
    for sd in dict.fromkeys(candidates):
        dest=sd/'3ds/ThieviusCTR';dest.mkdir(parents=True,exist_ok=True)
        for path in files:
            relative=path.relative_to(ROOT/'local/data') if path.is_relative_to(ROOT/'local/data') else Path(path.name)
            target=dest/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
        if args.runtime:
            for tree in ('Worlds','Cutscenes'):
                src=ROOT/'local/data'/tree;dst=dest/tree
                if dst.is_symlink():
                    if dst.resolve()==src.resolve():continue
                    dst.unlink()
                elif dst.is_dir():shutil.rmtree(dst)
                elif dst.exists():dst.unlink()
                dst.symlink_to(src)
        print('Installed native gameplay test:' if args.runtime else 'Installed free-camera viewer:',dest,flush=True)
    if not args.install_only:subprocess.run(['azahar','-w',str(binary)],check=True)
if __name__=='__main__':main()
