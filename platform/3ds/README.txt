ThieviusCTR - static Paris viewer

A native Citro3D scene viewer built from ProjectCane's real extracted meshes,
triangle-strip conversion, vertex colours, object transforms and textures.
This is an early porting milestone. Gameplay, Sly's animation and collision
are not connected yet; the camera flies freely through the level.

Build from the repository root:
  python3 platform/3ds/build.py
Requires devkitARM, libctru, Citro3D, picasso, 3dsxtool and smdhtool.
No desktop GLFW, SDL, FFmpeg or OpenGL libraries are linked in the static
viewer target (build.py); the full runtime (build_runtime.py) statically links
minimal LGPL v2.1 FFmpeg 7.1.1 libraries (built by build_ffmpeg_3ds.py) for
on-hardware Y2R cutscene decoding.
The upstream desktop CMake build is unchanged.

Prepare game data from your US disc on the development PC:
  python3 platform/3ds/extract_ntsc.py /path/to/Slygame.iso
  python3 platform/3ds/export_scene.py build/raw-levels/Paris/Paris.brx local/data/Paris.cscene
The extractor requires g++, Python 3 and 7z. It currently supports the US
SCUS_971.98 release. --list shows IDs; --level-id selects individual levels.
The exporter builds an instrumented copy of the upstream BRX converter;
its normal converted BRX output remains byte-identical. Generated game data
stays in local/ and build/, both ignored by Git.

Installation:
Copy the ZIP's 3ds directory to your SD card root. Then copy your generated
local/data/Paris.cscene to /3ds/ThieviusCTR/Paris.cscene.
Start ThieviusCTR Viewer through Homebrew Launcher.
The dist/ThieviusCTR-viewer-test.zip package contains no extracted game assets.
The local/ThieviusCTR-paris-test.zip package, when generated, includes
YOUR converted Paris scene for personal testing; do not publish that ZIP.

Controls:
  Circle Pad: move horizontally
  C-stick or D-pad: look
  Touch drag: look
  A / B: move up / down
  R: move faster
  X: reset the camera
  START: exit

Emulator (normal desktop use):
  python3 platform/3ds/run_emulator.py
This installs the viewer AND Paris.cscene into the SD folder configured by
Azahar before opening it. --install-only stages files without launching;
--sd-root overrides the destination. Opening a .3dsx beside Paris.cscene in
a normal PC folder does not make that file visible inside the emulator.

Isolated automated emulator check:
  python3 platform/3ds/emulator_probe.py
Uses an isolated emulator SD/configuration, copies locally extracted data,
and captures boot.png and after-start.png under build/3ds/emulator/.
Azahar alone cannot establish real-console performance.

Current limitations:
Static meshes only. Skeletal/posed hierarchies, stock templates, hidden
objects, unresolved instances and non-mesh effects are omitted. Scripted
activation, original lighting, sky, material animation, blending order and
cel outlines are not complete. The renderer uses authored vertex colours
and the first diffuse texture per material, capped at 128 pixels per axis
and converted to tiled RGBA5551. No game data is compiled into the launcher.

Based on theclub654/ProjectCane:
  https://github.com/theclub654/ProjectCane
Upstream revision: f77825d6bc9be3f795ead9d140b410fb9c9c22c1
GLM has a 3DS-only integer typedef fix for newlib. -iquote keeps the engine's
math.h separate from the system math header. The original engine binary
reader is compiled unmodified and checked at startup.

Native Paris gameplay prototype:
  python3 platform/3ds/build_core.py --all
Cross-compiles the original runtime and native platform adapters into an ARM
archive. Desktop window, audio and movie libraries are not linked.

  python3 platform/3ds/build_runtime.py
Builds ThieviusCTR.3dsx and its Homebrew Launcher descriptor. The
runtime uses the original world/controller, collision, animation and render
submissions. Static meshes are cached in GPU memory; animated meshes use
original skeleton and pose data. This target no longer needs Paris.cscene.
The viewer still uses that cache.

Prepare original effects and Paris dialogue:
  python3 platform/3ds/extract_audio.py /path/to/Slygame.iso
The extractor follows upstream's media descriptors and VAG decoder. It
writes two sound banks, the Paris .mus bank and 65 voice clips into local/data/.
Sound banks are parsed with upstream code; ADPCM effects are decoded when
requested and mixed by the DSP. Dialogue PCM is loaded directly into DSP
memory. OpenDSP is vendored with its source, firmware, provenance and BSD
license under platform/3ds/vendor/OpenDSP/. No firmware dump is needed.
The original MIDI/AME sequencer, note envelopes and instrument ADPCM now
feed native DSP voices. PCM instruments are shared in linear memory rather
than retained on the application heap or copied for every note. Native
instrument decoding retains the original loop-history stabilization.
Several SPU effects remain unfinished.

Stage the native build and its local game files into your emulator with:
  python3 platform/3ds/run_emulator.py --runtime --install-only
Or omit --install-only to launch it. The viewer remains separate.

Prepare all cutscenes & assets on PC (optional, requires ffmpeg for CMV2):
  python3 platform/3ds/extract_all.py /path/to/Slygame.iso
Or convert PS3 HD Collection .bik files to CMV2:
  python3 platform/3ds/convert_ps3_cutscenes.py /path/to/PS3_GAME/USRDIR/Sly1
On real hardware, ThieviusCTR also extracts PS2 .m2v+.wav cutscenes directly
from SCUS_971.98 on-console and decodes both PS3 .bik and PS2 .m2v in real
time using stripped LGPL v2.1 FFmpeg 7.1.1 + 3DS hardware Y2R. See the root
README.md and THIRD_PARTY_LICENSES.md for full details.

  python3 platform/3ds/emulator_probe.py --runtime --wait 40 --menu
Checks original title/options, movie entry/skipping, NDSP-to-OpenDSP fallback
and clean exit with missing Nintendo DSP firmware.

  python3 platform/3ds/emulator_probe.py --runtime --wait 40
Runs an isolated startup test, waits for the opening conversation to return
control, then tests jumping, landing, movement and exit. Screenshots and
logs go under build/3ds/emulator-runtime/. Native memory budgets and timing
are logged too. Emulator results do not establish console performance.

  python3 platform/3ds/package_runtime.py
Creates an asset-free ZIP in dist/ and a personal test ZIP containing your
converted data in local/. Never publish the latter.
See README-runtime.txt for controls and remaining limitations.

  python3 platform/3ds/link_core.py
Checks original world/controller linkage and writes link-report.json.
This separate link-only diagnostic is never launched or packaged.
