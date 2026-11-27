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
if '--skip-core-refresh' not in sys.argv:
 refresh=subprocess.run([sys.executable,str(ROOT/'platform/3ds/build_core.py'),'--all'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 (CORE/'backend-build.log').write_text(refresh.stdout)
 if refresh.returncode:
  print(refresh.stdout[-6000:]);raise SystemExit(refresh.returncode)
elif not (CORE/'libcane-gameplay.a').exists() or not (CORE/'build-config.json').exists():
 raise SystemExit('--skip-core-refresh requested, but no cached gameplay archive/config is available')

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
launcher_deps=[launcher_src,ROOT/'platform/3ds/launcher_extract_3ds.h',ROOT/'platform/3ds/launcher_ui_3ds.h',ROOT/'platform/3ds/launcher_graphics.inc',ROOT/'platform/3ds/fmv_decode_3ds.h',ROOT/'File Extractor GUI/Data/DialogAudioTable.inc',*brx_deps]
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
cia='--cia' in sys.argv
runtime_obj=CORE/('runtime_cia.o' if cia else 'runtime_probe.o')
elf=OUT/('ThieviusCTR-cia.elf' if cia else 'ThieviusCTR.elf')
if cia:flags=[*flags,'-DCANE_CIA']
compiled=subprocess.run([str(CC),*flags,'-O3','-fomit-frame-pointer','-fno-math-errno','-fno-trapping-math','-freciprocal-math','-fno-signed-zeros','-fassociative-math','-I'+str(DK/'libctru/include'),'-include',str(CORE/'core_pch.h'),'-c',str(ROOT/'platform/3ds/runtime_probe.cpp'),'-o',str(runtime_obj)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(CORE/'runtime-compile.log').write_text(compiled.stdout)
if compiled.returncode:
 print(compiled.stdout[-6000:]);raise SystemExit(compiled.returncode)
# Build the shader independently, including on a clean checkout.
subprocess.run([str(DK/'tools/bin/picasso'),'-o',str(OUT/'basic.shbin'),str(ROOT/'platform/3ds/runtime.v.pica')],check=True)
subprocess.run([str(DK/'tools/bin/picasso'),'-o',str(OUT/'movie.shbin'),str(ROOT/'platform/3ds/runtime_movie.v.pica')],check=True)
subprocess.run([str(DK/'tools/bin/picasso'),'-o',str(OUT/'stereo1.shbin'),str(ROOT/'platform/3ds/runtime_stereo1.v.pica')],check=True)
subprocess.run([str(DK/'tools/bin/picasso'),'-o',str(OUT/'flat1.shbin'),str(ROOT/'platform/3ds/runtime_flat1.v.pica')],check=True)
(OUT/'runtime-shader.s').write_text('.section .rodata.cane_shader,"a",%progbits\n.balign 4\n.global cane_shader\n.global cane_shader_end\ncane_shader:\n.incbin "basic.shbin"\ncane_shader_end:\n')
with (OUT/'runtime-shader.s').open('a') as asm:
 asm.write('.balign 4\n.global cane_movie_shader\n.global cane_movie_shader_end\ncane_movie_shader:\n.incbin "movie.shbin"\ncane_movie_shader_end:\n')
 asm.write('.balign 4\n.global cane_stereo1_shader\n.global cane_stereo1_shader_end\ncane_stereo1_shader:\n.incbin "stereo1.shbin"\ncane_stereo1_shader_end:\n')
 asm.write('.balign 4\n.global cane_flat1_shader\n.global cane_flat1_shader_end\ncane_flat1_shader:\n.incbin "flat1.shbin"\ncane_flat1_shader_end:\n')
subprocess.run([str(CC),'-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-c','runtime-shader.s','-o','runtime-shader.o'],cwd=OUT,check=True)
fsr_src=ROOT/'platform/3ds/vendor/PICASR/fsr3ds.c'
fsr_hdr=ROOT/'platform/3ds/vendor/PICASR/fsr3ds.inc'
fsr_obj=CORE/'fsr3ds.o'
if not fsr_obj.exists() or fsr_obj.stat().st_mtime<max(fsr_src.stat().st_mtime,fsr_hdr.stat().st_mtime):
 res=subprocess.run([str(CC),'-x','c','-std=gnu11','-O3','-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-ffunction-sections','-fdata-sections','-I'+str(DK/'libctru/include'),'-I'+str(ROOT/'platform/3ds/vendor/PICASR'),'-c',str(fsr_src),'-o',str(fsr_obj)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 (CORE/'picasr-compile.log').write_text(res.stdout)
 if res.returncode:
  print(res.stdout[-6000:]);raise SystemExit(res.returncode)
subprocess.run([str(CC),*flags,'-I'+str(DK/'libctru/include'),'-c',str(ROOT/'platform/3ds/scene_3ds.cpp'),'-o',str(CORE/'scene_native.o')],check=True)
updater_objs=updater_deps.objects()
updater_libs=updater_deps.libraries()
command=[str(CC),'-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mtp=soft','-O3','-fomit-frame-pointer','-flto=auto','-specs=3dsx.specs','-Wl,--wrap=abort,--wrap=exit,--wrap=_exit,--gc-sections,-Map,'+str(elf.with_suffix('.map')),str(runtime_obj),*[str(p) for p in updater_objs],str(fsr_obj),str(fmv_obj),str(launcher_obj),*[str(p) for p in brx_objs],str(CORE/'scene_native.o'),str(OUT/'runtime-shader.o'),str(CORE/'libcane-gameplay.a'),str(ffmpeg_dir/'lib/libavformat.a'),str(ffmpeg_dir/'lib/libavcodec.a'),str(ffmpeg_dir/'lib/libavutil.a'),*updater_libs,'-L'+str(DK/'libctru/lib'),'-lcitro3d',str(DSP/'lib/libopendsp.a'),'-lctru','-lm','-o',str(elf)]
result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
(CORE/'runtime-link.log').write_text(result.stdout)
if result.returncode:
 print(result.stdout[-12000:]);raise SystemExit(result.returncode)
smdh=OUT/'ThieviusCTR.smdh'
subprocess.run([str(DK/'tools/bin/smdhtool'),'--create','ThieviusCTR','Sly Cooper 3DS launcher & port','Raihwy3 / ProjectCane',str(ROOT/'platform/3ds/icon.png'),str(smdh)],check=True)
if not cia:
 subprocess.run([str(DK/'tools/bin/3dsxtool'),str(elf),str(OUT/'ThieviusCTR.3dsx'),'--smdh='+str(smdh)],check=True)
print('Built CIA ELF:' if cia else 'Built native runtime & launcher:',elf if cia else OUT/'ThieviusCTR.3dsx')
