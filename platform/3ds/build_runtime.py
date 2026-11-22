#!/usr/bin/env python3
"""Build the native 3DS runtime & on-console ISO launcher separately from the viewer."""
from pathlib import Path
import concurrent.futures,json,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'platform/3ds'))
import updater_deps
OUT=ROOT/'build/3ds';CORE=OUT/'core';BRX_OUT=OUT/'brx-rebuilder'
BRX_SRC=ROOT/'File Extractor GUI/NativeTools/BrxRebuilder'
DK=Path(os.environ.get('DEVKITPRO','/opt/devkitpro'))
DSP=ROOT/'platform/3ds/vendor/OpenDSP/library'
subprocess.run(['make','-C',str(DSP),'-j4'],env={**os.environ,'DEVKITPRO':str(DK)},check=True)
CC=DK/'devkitARM/bin/arm-none-eabi-g++'
refresh=subprocess.run([sys.executable,str(ROOT/'platform/3ds/build_core.py'),'--all'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(CORE/'backend-build.log').write_text(refresh.stdout)
if refresh.returncode:
 print(refresh.stdout[-6000:]);raise SystemExit(refresh.returncode)

BRX_OUT.mkdir(parents=True,exist_ok=True)
brx_flags=[
 '-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft',
 '-std=gnu++20','-O3','-ffunction-sections','-fdata-sections',
 '-fno-math-errno','-fno-trapping-math','-D__3DS__','-DBRX_REBUILDER_3DS',
 '-DCBinaryInputStream=BrxRebuilderStream','-I'+str(BRX_SRC),
]
brx_sources=[p for p in sorted(BRX_SRC.glob('*.cpp')) if p.name!='main.cpp']
brx_deps=list(BRX_SRC.glob('*.h'))+list(BRX_SRC.glob('*.inc'))
def compile_brx(source):
 obj=BRX_OUT/(source.stem+'.o')
 latest=max(p.stat().st_mtime for p in [source,*brx_deps])
 if not obj.exists() or obj.stat().st_mtime<latest:
  res=subprocess.run([str(CC),*brx_flags,'-Wall','-Wno-misleading-indentation','-c',str(source),'-o',str(obj)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
  if res.returncode:raise RuntimeError(res.stdout[-4000:])
 return obj
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 brx_objs=list(pool.map(compile_brx,brx_sources))

launcher_src=ROOT/'platform/3ds/launcher_extract_3ds.cpp'
launcher_obj=CORE/'launcher_extract_3ds.o'
launcher_deps=[launcher_src,ROOT/'platform/3ds/launcher_extract_3ds.h',ROOT/'platform/3ds/launcher_ui_3ds.h',ROOT/'platform/3ds/fmv_decode_3ds.h',ROOT/'File Extractor GUI/Data/DialogAudioTable.inc',*brx_deps]
if not launcher_obj.exists() or launcher_obj.stat().st_mtime<max(p.stat().st_mtime for p in launcher_deps):
 res=subprocess.run([str(CC),*brx_flags,'-Wall','-Wextra','-I'+str(DK/'libctru/include'),'-I'+str(ROOT/'platform/3ds'),'-I'+str(ROOT/'File Extractor GUI'),'-c',str(launcher_src),'-o',str(launcher_obj)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 (CORE/'launcher-compile.log').write_text(res.stdout)
 if res.returncode:
  print(res.stdout[-6000:]);raise SystemExit(res.returncode)

subprocess.run([sys.executable,str(ROOT/'platform/3ds/build_ffmpeg_3ds.py')],check=True)
ffmpeg_dir=OUT/'ffmpeg/install'
fmv_src=ROOT/'platform/3ds/fmv_decode_3ds.cpp'
fmv_hdr=ROOT/'platform/3ds/fmv_decode_3ds.h'
fmv_obj=CORE/'fmv_decode_3ds.o'
fmv_flags=['-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-std=gnu++20','-O3','-ffunction-sections','-fdata-sections','-fno-math-errno','-fno-trapping-math','-D__3DS__','-Wall','-Wextra','-I'+str(DK/'libctru/include'),'-I'+str(ROOT/'platform/3ds'),'-I'+str(ffmpeg_dir/'include')]
if not fmv_obj.exists() or fmv_obj.stat().st_mtime<max(fmv_src.stat().st_mtime,fmv_hdr.stat().st_mtime):
 res=subprocess.run([str(CC),*fmv_flags,'-c',str(fmv_src),'-o',str(fmv_obj)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 (CORE/'fmv-compile.log').write_text(res.stdout)
 if res.returncode:
  print(res.stdout[-6000:]);raise SystemExit(res.returncode)

config=json.loads((CORE/'build-config.json').read_text())
flags=config['flags']
compiled=subprocess.run([str(CC),*flags,'-O3','-fno-math-errno','-fno-trapping-math','-I'+str(DK/'libctru/include'),'-include',str(CORE/'core_pch.h'),'-c',str(ROOT/'platform/3ds/runtime_probe.cpp'),'-o',str(CORE/'runtime_probe.o')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(CORE/'runtime-compile.log').write_text(compiled.stdout)
if compiled.returncode:
 print(compiled.stdout[-6000:]);raise SystemExit(compiled.returncode)
# Build the shader independently, including on a clean checkout.
subprocess.run([str(DK/'tools/bin/picasso'),'-o',str(OUT/'basic.shbin'),str(ROOT/'platform/3ds/runtime.v.pica')],check=True)
(OUT/'runtime-shader.s').write_text('.section .rodata.cane_shader,"a",%progbits\n.balign 4\n.global cane_shader\n.global cane_shader_end\ncane_shader:\n.incbin "basic.shbin"\ncane_shader_end:\n')
subprocess.run([str(CC),'-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-c','runtime-shader.s','-o','runtime-shader.o'],cwd=OUT,check=True)
subprocess.run([str(CC),*flags,'-I'+str(DK/'libctru/include'),'-c',str(ROOT/'platform/3ds/scene_3ds.cpp'),'-o',str(CORE/'scene_native.o')],check=True)
updater_objs=updater_deps.objects()
updater_libs=updater_deps.libraries()
command=[str(CC),'-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-specs=3dsx.specs','-Wl,--wrap=abort,--wrap=exit,--wrap=_exit,--gc-sections,-Map,'+str(OUT/'ThieviusCTR.map'),str(CORE/'runtime_probe.o'),*[str(p) for p in updater_objs],str(fmv_obj),str(launcher_obj),*[str(p) for p in brx_objs],str(CORE/'scene_native.o'),str(OUT/'runtime-shader.o'),str(CORE/'libcane-gameplay.a'),str(ffmpeg_dir/'lib/libavformat.a'),str(ffmpeg_dir/'lib/libavcodec.a'),str(ffmpeg_dir/'lib/libavutil.a'),*updater_libs,'-L'+str(DK/'libctru/lib'),'-lcitro3d',str(DSP/'lib/libopendsp.a'),'-lctru','-lm','-o',str(OUT/'ThieviusCTR.elf')]
result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(CORE/'runtime-link.log').write_text(result.stdout)
if result.returncode:
 print(result.stdout[-12000:]);raise SystemExit(result.returncode)
subprocess.run([str(DK/'tools/bin/3dsxtool'),str(OUT/'ThieviusCTR.elf'),str(OUT/'ThieviusCTR.3dsx')],check=True)
subprocess.run([str(DK/'tools/bin/smdhtool'),'--create','ThieviusCTR','Sly Cooper 3DS launcher & port','Raihwy3 / ProjectCane',str(DK/'libctru/default_icon.png'),str(OUT/'ThieviusCTR.smdh')],check=True)
print('Built native runtime & launcher:',OUT/'ThieviusCTR.3dsx')
