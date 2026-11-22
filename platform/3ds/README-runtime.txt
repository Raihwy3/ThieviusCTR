ThieviusCTR - Nintendo 3DS port of Sly Cooper (based on ProjectCane)

This build runs the native ARM port of Sly Cooper and the Thievius Raccoonus
(based on ProjectCane), including the dual-screen on-console ISO extractor,
original controller/collision/animation/scripting runtime, Citro3D renderer,
OpenDSP/NDSP audio, and hardware-accelerated Y2R cutscene playback.

Controls (default Swap B/A On, Camera Invert X On, Binoc Invert Y On):
  Circle Pad: move
  C-stick: camera, where supported
  A: jump / accept in original menus
  Y: cane attack
  B: original Circle action
  X: original Triangle special move / cancel in original menus
  L / R: Binocucom controls
  START: play from the title / open the in-game pause menu
  SELECT: original title options
  SELECT + START: exit this application
All added settings use the game's own Options menu, grouped into Sound,
Interface and Controls. Interface > Button icons defaults to New 3DS;
select PS2 to use the original artwork. Controls contains Dialogue Skip
Controls, Swap B/A (defaults On; swaps physical B/A with PS Cross/Circle as
well as displayed B/A or X/O icons), Camera Invert X/Y (X defaults On) and
Binoc Invert X/Y (Y defaults On). Left/Right or accept changes these
settings; native-options.cfg saves them between launches.
During gameplay, the bottom screen shows a live FPS, CPU, GPU, and RAM
(HEAP/FCRAM/VRAM) system monitor.
The pause menu's Exit to Homebrew Launcher closes the application cleanly.
Open Options > Sound to choose the audio backend and adjust volumes;
Left/Right changes a value, accept selects and X goes back.

Installation:
Copy the package's 3ds directory to your SD card root. Launch
ThieviusCTR through Homebrew Launcher.

Option A - Prepare all levels, audio, and cutscenes on the 3DS console (no PC required):
Put your legally backed-up US retail Sly Cooper ISO (SCUS_971.98) at
/3ds/ThieviusCTR/Slygame.iso on your SD card and launch ThieviusCTR.
On first boot, the launcher verifies your ISO and extracts all 46 levels,
sound banks, music, voice/commentary audio, and all 18 PS2 cutscenes
(.m2v + .wav) directly on-console (B cancels and resumes cleanly on the next
boot). Once complete, Slygame.iso can be deleted to reclaim SD card space.

Option B - Prepare all assets on PC (fastest, includes 18 cutscenes):
  python3 platform/3ds/extract_all.py /path/to/Slygame.iso --sd-out /path/to/sd
Requires Python 3, g++, 7z, and ffmpeg (or pass --skip-cutscenes).
Writes all 46 levels, music/sound banks/dialog/commentary WAVs, and 18 CMV2
cutscenes directly into 3ds/ThieviusCTR/.

Optional PS3 HD cutscenes (HD videos):
1. Zero PC conversion (recommended on New 3DS): copy raw .bik files from your
   legally dumped PS3 Sly Collection PS3_GAME/USRDIR/Sly1/ folder directly
   into sdmc:/3ds/ThieviusCTR/Cutscenes/ (original names such as splash.bik,
   secret_origin.bik, ending_1.bik, etc., or Cutscene_00.bik..Cutscene_17.bik).
   The 3DS decodes Bink video + live stereo audio and converts YUV->RGB via 3DS
   Y2R hardware, displaying 16:9 movies in 800px wide mode on supported models.
2. Optional PC pre-conversion (CMV2 ETC1 + 16 kHz mono WAV):
   python3 platform/3ds/convert_ps3_cutscenes.py /path/to/PS3_GAME/USRDIR/Sly1 --output /path/to/sd/3ds/ThieviusCTR/Cutscenes
   Priority order: Cutscene_XX.bik -> PS3 .bik filename -> Cutscene_XX.cmov -> Cutscene_XX.m2v.

Do not publish personal extracted game data or converted cutscenes.

Audio defaults to OpenDSP with original effects, dialogue and music. Its
firmware is built into the executable; no Nintendo DSP firmware dump is needed.
Options > Sound also offers standard NDSP (requires your own dspfirm.cdc);
if NDSP cannot start, it falls back to OpenDSP. Backend, volume and stereo
choices are saved in audio-backend.cfg beside the app. Effect LFOs/reverb
and exact SPU fidelity remain work in progress.

Status & Rendering Notes:
Real-hardware tested on New 3DS through Paris, The Hideout, and the first
level of Episode 1 (Tide of Terror). Later episodes, gameplay polish, and
some UI quirks/panels remain work in progress.
All 18 cutscene slots (00-17) are supported. Movies play directly from PS3
.bik files or PS2 .m2v+.wav via stripped LGPL 2.1 FFmpeg + 3DS hardware Y2R,
or from pre-converted CMV2 .cmov+.wav. START or B (A if swapped) skips a movie.
HUD and dialogue text use original fonts and layout code. BLIP particles use
native billboards with soft alpha and additive/normal blending. Character
shadows use silhouette targets and stencil/additive projected volumes. Static
and dynamic lights drive ambient/midtone/highlight maps, and cel outlines
render authored silhouette edges. Gameplay targets 30 updates per second;
Old 3DS performance and full-game compatibility have not yet been verified.

Diagnostic logs are written beside the game data as runtime.log and
runtime-stderr.log. Saves are stored in Saves/. With Wi-Fi connected, the
Homebrew launcher checks GitHub Releases for newer ThieviusCTR updates at
startup (B skips; create an empty no-update-check.flag beside the .3dsx to
disable automatic checks; only ThieviusCTR.3dsx and ThieviusCTR.smdh are ever
replaced). CREDITS.txt lists all attributions; OpenDSP's BSD 2-Clause license
is included as LICENSE-OpenDSP.txt, FFmpeg's LGPL v2.1 license is included as
LICENSE-FFmpeg-LGPL2.1.txt, and the Mozilla MPL 2.0 CA bundle license is
included as LICENSE-Mozilla-MPL-2.0.txt. Zero game assets or Nintendo DSP
firmware are compiled into the app.
