#!/usr/bin/env python3
"""Build a 3DS platform probe linking ProjectCane's unmodified binary reader."""
from pathlib import Path
import os,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/3ds';OUT.mkdir(parents=True,exist_ok=True)
DK=Path(os.environ.get('DEVKITPRO','/opt/devkitpro'))
CC=DK/'devkitARM/bin/arm-none-eabi-g++'
def run(*args):subprocess.run([str(a) for a in args],check=True,cwd=OUT)
flags=['-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-std=gnu++20','-O2','-g','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-D__3DS__','-DGLM_FORCE_PURE','-include','limits','-I'+str(DK/'libctru/include'),'-iquote'+str(ROOT/'Sly1-Release'),'-I'+str(ROOT/'Libraries/include')]
run(DK/'tools/bin/picasso','-o','basic.shbin',ROOT/'platform/3ds/basic.v.pica')
(OUT/'shader.s').write_text('.section .rodata.cane_shader,"a",%progbits\n.balign 4\n.global cane_shader\n.global cane_shader_end\ncane_shader:\n.incbin "basic.shbin"\ncane_shader_end:\n')
run(CC,*flags,'-c',OUT/'shader.s','-o','shader.o')
run(CC,*flags,'-c',ROOT/'Sly1-Release/bis.cpp','-o','bis.o')
run(CC,*flags,'-c',ROOT/'platform/3ds/main.cpp','-o','main.o')
run(CC,*flags,'-c',ROOT/'platform/3ds/scene_3ds.cpp','-o','scene.o')
run(CC,*flags,'-specs=3dsx.specs','-Wl,--gc-sections,-Map,ThieviusCTR-viewer.map','main.o','scene.o','bis.o','shader.o','-L'+str(DK/'libctru/lib'),'-lcitro3d','-lctru','-lm','-o','ThieviusCTR-viewer.elf')
run(DK/'tools/bin/3dsxtool','ThieviusCTR-viewer.elf','ThieviusCTR-viewer.3dsx')
run(DK/'tools/bin/smdhtool','--create','ThieviusCTR Viewer','Static Paris scene viewer','Raihwy3 / ProjectCane',DK/'libctru/default_icon.png','ThieviusCTR-viewer.smdh')
DIST=ROOT/'dist';DIST.mkdir(exist_ok=True)
with zipfile.ZipFile(DIST/'ThieviusCTR-viewer-test.zip','w',zipfile.ZIP_DEFLATED) as z:
 for name in ['ThieviusCTR-viewer.3dsx','ThieviusCTR-viewer.smdh']:
  z.write(OUT/name,'3ds/ThieviusCTR/'+name)
 z.write(ROOT/'platform/3ds/README.txt','3ds/ThieviusCTR/README.txt')
print('Build:',OUT/'ThieviusCTR-viewer.3dsx')
print('Install:',DIST/'ThieviusCTR-viewer-test.zip')
