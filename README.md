# ThieviusCTR

**ThieviusCTR** is an unofficial native Nintendo 3DS port of *Sly Cooper and the Thievius Raccoonus*, built on [ProjectCane](https://github.com/theclub654/ProjectCane) (`theclub654/ProjectCane`, base revision `f77825d6bc9be3f795ead9d140b410fb9c9c22c1`).

> [!IMPORTANT]
> **Zero copyrighted game assets and zero proprietary Nintendo DSP firmware are distributed with this repository or its releases.** You must supply your own legally obtained US retail PlayStation 2 disc image (`SCUS_971.98`).

---

## Status & Features

- **Playability**: Real-hardware tested on **New 3DS** through Paris, The Hideout, and the first level of Episode 1 (*Tide of Terror*). Later episodes, gameplay polish, and some UI panels/effects are still work in progress.
- **Hardware Recommendation**: Developed and tested primarily on **New 3DS / New 2DS XL**. Old 3DS compatibility and performance have not yet been verified.
- **Zero-PC On-Console Extractor**: Built-in dual-screen launcher verifies your `SCUS_971.98` ISO on the SD card and extracts all 46 levels, sound banks, music, dialog/commentary audio, and all 18 cutscenes directly on the console — no PC conversion tools required.
- **Built-in OpenDSP Audio**: Bundles [OpenDSP](https://github.com/Cellenseres/OpenDSP) open-source DSP firmware, so no Nintendo `dspfirm.cdc` dump is required (standard `NDSP` is also selectable in **Options > Sound** if you have your own DSP dump).
- **Native Citro3D Renderer**: Renders original level/skeleton meshes, vertex colors, diffuse/lighting maps, silhouette shadows, stencil/additive light volumes, BLIP particle billboards, and cel outlines, using original HUD/dialogue fonts, a selectable **New 3DS** or **PS2** button-icon set, and a live bottom-screen FPS / CPU / GPU / RAM system monitor.
- **Hardware-Accelerated Cutscenes (PS2 & PS3 HD)**: Plays all 18 cutscene slots using 3DS hardware `Y2R` YUV-to-RGB color conversion, supporting both PS2 `.m2v` streams and progressive **PS3 HD Collection** `.bik` videos directly on-console without PC transcoding.
- **Optional Startup Self-Updater**: Checks public GitHub Releases over Wi-Fi on Homebrew `.3dsx` launch, verifies TLS + SHA-256 + `3DSX`/`SMDH` headers, and atomically updates only `ThieviusCTR.3dsx` and `ThieviusCTR.smdh` (never touching game data or saves; see [`docs/updater.md`](docs/updater.md)).

---

## Installation (Nintendo 3DS)

### Requirements

- A Nintendo 3DS with custom firmware (CFW) and the **Homebrew Launcher** (New 3DS / New 2DS XL recommended)
- Your own legally backed-up **US retail PlayStation 2 ISO** of *Sly Cooper and the Thievius Raccoonus* (`SCUS_971.98` — PAL/JP discs are not supported)
- Free SD card space: ~3 GiB during first-time on-console extraction (~1.5–2 GiB once the ISO is deleted afterward)

### Option A — On-Console Extraction (No PC Conversion Required)

1. Download the asset-free release ZIP from [GitHub Releases](https://github.com/Raihwy3/ThieviusCTR/releases) (or build from source), and copy its `3ds/` folder to the root of your SD card so `ThieviusCTR.3dsx` is at:
   `sd:/3ds/ThieviusCTR/ThieviusCTR.3dsx`
2. Copy your US retail PS2 ISO (`SCUS_971.98`) to:
   `sd:/3ds/ThieviusCTR/Slygame.iso`
   *(The launcher also checks `Sly.iso`, `sly1.iso`, `SCUS_971.98.iso`, or any other `*.iso` in `sd:/3ds/ThieviusCTR/`.)*
3. *(Optional)* If you want to use **PS3 HD Collection `.bik` videos** instead of demuxing the PS2 cutscenes, copy them into `sd:/3ds/ThieviusCTR/Cutscenes/` **before** launching (see [Using PS3 HD Collection Videos](#using-ps3-hd-collection-videos-hd-cutscenes)).
4. Launch **ThieviusCTR** from the Homebrew Launcher. On first boot, the launcher validates `SYSTEM.CNF` / `SCUS_971.98` and extracts:
   - **46** game levels (`Worlds/*.brx`)
   - **7** sound banks (`Worlds/Sound Banks/*.bnk`)
   - **38** music tracks (`Worlds/Music/MUSID_*.mus` + `MusicMap.txt`)
   - **1,368** dialog voice clips, **33** developer commentary clips, **6** JLO ambient clips, and **1** prompt clip
   - **18** PS2 cutscenes (`Cutscenes/Cutscene_00.m2v`..`17.m2v` + `.wav`) for any slots not already covered by `.bik` or `.cmov`
   Press `B` at any point during extraction to cancel cleanly; extraction resumes from where it left off on the next launch.
5. Once extraction finishes, the game starts automatically. You can delete `sd:/3ds/ThieviusCTR/Slygame.iso` afterward to free up ~2 GiB on your SD card.

### Option B — Pre-Extract Assets on PC (Faster)

Preparing assets on a PC is faster and lets you stage an SD card layout in one command. Requires **Python 3**, `g++`, `7z`, and `ffmpeg` (or pass `--skip-cutscenes` to omit `ffmpeg`):

```bash
python3 platform/3ds/extract_all.py /path/to/Slygame.iso --sd-out /path/to/sd-card-root
```

- Pass `--skip-cutscenes` if you want to pair the PC-extracted levels/audio with raw PS3 `.bik` cutscenes instead of converting PS2 cutscenes.
- The script verifies all extracted counts (`46` levels, `7` banks, `38` music tracks, `1,368` dialog WAVs, `33` commentary WAVs, `6` ambient WAVs, `1` prompt WAV, and `18` cutscenes unless skipped) and stages `sd:/3ds/ThieviusCTR/`.

### SD Card Layout

After installation, your SD card directory should look like this:

```text
sd:/3ds/ThieviusCTR/
├── ThieviusCTR.3dsx
├── ThieviusCTR.smdh
├── README-runtime.txt
├── CREDITS.txt
├── LICENSE-OpenDSP.txt
├── LICENSE-FFmpeg-LGPL2.1.txt
├── LICENSE-Mozilla-MPL-2.0.txt
├── Worlds/                  # Extracted levels, banks, music, dialog & commentary
├── Cutscenes/               # PS3 .bik, PC-converted .cmov/.wav, or PS2 .m2v/.wav
├── Saves/                   # Created automatically on first save
├── extraction_manifest.json
└── assets-ready.stamp
```

---

## Using PS3 HD Collection Videos (HD Cutscenes)

The PS2 disc uses interlaced MPEG-2 video, whereas the PS3 *The Sly Collection* / *The Sly Trilogy* stores cleaner progressive Bink (`.bik`) videos. ThieviusCTR supports both **zero-conversion direct `.bik` playback on the 3DS** and **optional PC pre-conversion to `CMV2`**.

For each cutscene index (`00`–`17`), ThieviusCTR checks `sd:/3ds/ThieviusCTR/Cutscenes/` in this order and plays the first valid match:

1. `Cutscene_XX.bik`
2. Original PS3 `.bik` filename (e.g., `splash.bik`, `secret_origin.bik`, `ending_1.bik`, ...)
3. `Cutscene_XX.cmov` + `Cutscene_XX.wav` *(pre-converted `CMV2` ETC1)*
4. `Cutscene_XX.m2v` + `Cutscene_XX.wav` *(on-console demuxed PS2 MPEG-2)*

> [!TIP]
> **Save time & SD space:** If you place the PS3 `.bik` files in `sd:/3ds/ThieviusCTR/Cutscenes/` **before** your first launch, the on-console ISO extractor sees that all 18 cutscene slots are filled and skips demuxing the PS2 `.m2v` files. If you already extracted the PS2 cutscenes earlier, dropping `.bik` files into `Cutscenes/` automatically takes priority over them, and you can safely delete the older `Cutscene_*.m2v` and `Cutscene_*.wav` files.

### Method 1 — Direct `.bik` Playback on 3DS (No PC Conversion Required)

Copy the **17** `.bik` files directly from your legally dumped PS3 copy (`PS3_GAME/USRDIR/Sly1/`) into:

```text
sd:/3ds/ThieviusCTR/Cutscenes/
```

No renaming or re-encoding is needed. ThieviusCTR decodes Bink video and live stereo Bink audio on the 3DS using stripped LGPL 2.1 FFmpeg and converts YUV-to-RGB565 in real time via 3DS `Y2R` hardware. On consoles that support 800px wide mode (New 3DS, Old 3DS, New 2DS XL), 16:9 letterboxed `.bik` cutscenes crop the black bars (`640×360`) and display across the full width; 4:3 extras keep a centered 4:3 frame.

| Slot | Filename in `PS3_GAME/USRDIR/Sly1/` | Aspect | Description |
| --- | --- | --- | --- |
| `00` | `secret_origin.bik` | 16:9 | Opening origin cutscene |
| `01` | `secret_origin_alt.bik` | 4:3 | Alternate origin cutscene |
| `02` | `making_of.bik` | 4:3 | Making-of extra |
| `03` | `outtakes.bik` | 4:3 | Outtakes extra |
| `04` | `underwater_intro.bik` | 16:9 | Episode 1 (*Tide of Terror*) intro |
| `05` | `underwater_outro.bik` | 16:9 | Episode 1 outro |
| `06` | `muggshot_intro.bik` | 16:9 | Episode 2 (*Sunset Snake Eyes*) intro |
| `07` | `muggshot_outro.bik` | 16:9 | Episode 2 outro |
| `08` | `voodoo_intro.bik` | 16:9 | Episode 3 (*Vicious Voodoo*) intro |
| `09` | `voodoo_outro.bik` | 16:9 | Episode 3 outro |
| `10` | `snow_intro.bik` | 16:9 | Episode 4 (*Fire in the Sky*) intro |
| `11` | `snow_outro.bik` | 16:9 | Episode 4 outro |
| `12` | `clockwerk_intro.bik` | 16:9 | Episode 5 (*Cold Heart of Hate*) intro |
| `13` | `ending_1.bik` | 16:9 | Ending part 1 |
| `14` | `ending_2.bik` | 16:9 | Ending part 2 |
| `15` | `credits.bik` | 4:3 | Credits |
| `16`, `17` | `splash.bik` | 16:9 | Startup logo & splash attract movie (shared) |

Press `START` or `B` (`A` when **Swap B/A** is enabled) during any cutscene to skip it.

### Method 2 — Optional PC Pre-Conversion (`CMV2` ETC1)

If you prefer lower CPU decode overhead (for example when testing on Old 3DS, selecting a non-English audio track inside multi-language `.bik` files, or forcing a lower FPS), you can pre-convert the PS3 `.bik` folder into `CMV2` ETC1 + 16 kHz mono WAV on a PC.

Requires **Python 3**, `g++`, `ffmpeg`, and `ffprobe`:

```bash
python3 platform/3ds/convert_ps3_cutscenes.py /path/to/PS3_GAME/USRDIR/Sly1 --output /path/to/sd-card-root/3ds/ThieviusCTR/Cutscenes
```

Optional flags:

- `--audio-track <n>` — select which embedded audio stream to extract (`0` = English default)
- `--fps <fps>` — override frame rate (`1`..`60`, or milli-fps `1000`..`60000`; default preserves source FPS)
- `--index <0..17>` — convert specific cutscene slot(s) only (repeatable)
- `--force` — overwrite existing `Cutscene_XX.cmov` / `.wav` files

This produces `Cutscene_00.cmov`..`Cutscene_17.cmov` + `.wav` (`640×224` for widescreen slots, `640×240` for 4:3 slots). Remove any `.bik` files from `Cutscenes/` if you want ThieviusCTR to load the `.cmov` files instead of `.bik`.

---

## Controls & In-Game Options

| Input | Action |
| --- | --- |
| **Circle Pad** | Move |
| **C-Stick** | Camera control (where supported) |
| **A** | Jump / Accept in menus *(with Swap B/A On by default)* |
| **Y** | Cane attack |
| **B** | Original Circle action *(with Swap B/A On by default)* |
| **X** | Original Triangle special move / Cancel in menus |
| **L / R** | Binocucom controls |
| **START** | Start from title / Pause menu / Skip cutscene |
| **SELECT** | Title Options menu |
| **SELECT + START** | Exit application cleanly |

All 3DS-specific preferences integrate into the game's built-in **Options** menu:

- **Options > Sound**: Switch between **OpenDSP** (default, no dump needed) and **NDSP**, and adjust volumes / stereo output (saved in `audio-backend.cfg`).
- **Options > Graphics**: Choose **New 3DS** (default) or **PS2** button icons. Display modes include **400×240**, **800×240 2D**, per-eye stereoscopic 3D and **800×480 internal supersampling**. These modes are mutually exclusive. Adjust 3D strength, Original/Low shadow maps, and a **30/60 FPS render cap**. The game simulation remains 30 Hz; the 60 FPS option lets the renderer submit frames at 60 FPS using the latest simulation state. PICA-SR filter implementations are retained but hidden from the selector for now.
  - The top screen shares its extra horizontal pixels between wide 2D and stereo: 800×240 **per eye** is not supported. The physical 3D slider controls depth in stereo mode; 2DS models cannot show stereo. Wide output falls back to 400×240 in the emulator, whose 800-column output is cropped. SSAA and PICA-SR modes need additional render-target memory; if allocation fails, the renderer falls back to native 400×240.
  - **3D (400×480/eye)** renders twice the vertical resolution and downsamples to the panel. It uses more GPU time and VRAM than normal 3D. Both stereo modes use the same 3D settings submenu.
  - Cutscenes keep their existing playback path. These display settings apply to the rendered game and menus.
- **Options > Controls**: Configure **Swap B/A** (default **On**; swaps physical `B`/`A` and displayed icons), **Dialogue Skip Controls**, **Camera Invert X/Y** (X defaults **On**), and **Binoc Invert X/Y** (Y defaults **On**) (saved in `native-options.cfg`).

The new display modes and gyro controls have passed emulator/host checks; console testing of depth and physical motion is still needed.

**Options > Controls > Gyro aiming** enables motion aiming for the binoculars and sniper/aiming sections. It adds direct angular movement to the existing stick controls, with 25–400% sensitivity (100% is a 1× angular multiplier), a small jitter filter, and a nonblocking calibration action. Keep the console still for about 1.5 seconds when calibrating. Gyro is On by default and does not steer regular third-person movement or menus.

Existing ETC1 world-texture caches under `Cache/Textures/` can still be loaded. Missing caches immediately use the original texture format; synchronous startup encoding is disabled to avoid stalls before cutscenes on fresh ISO installations. Transparent textures, font atlases and UI retain their existing formats. Cache files derive from your own game data and are not included in release ZIPs. World textures have a 3 MiB VRAM budget, with overflow in linear RAM, reserving space for display targets and shadows.

The bottom screen shows the original coin counter at the upper left and health/lives indicator at the upper right. Enabled counters stay visible during gameplay alongside the original top-screen pop-ups. Portrait conversations fill the bottom screen, with text above the animated character windows. Drag to control the gameplay camera or tap to advance dialogue. The title has Play/Options buttons; pause menus stay on the top screen. **Graphics > Bottom screen** provides separate counter toggles and a choice of game HUD or the **FPS / CPU / GPU / RAM** system monitor. Portrait conversations temporarily replace the monitor.

---

## Saves, Settings & Troubleshooting

START at the title resumes your last save. If none exists, choose a slot to
start a new game. SELECT opens the New Game and Load Game options.

Inside `sd:/3ds/ThieviusCTR/`:

- `Saves/` — in-game save files
- `native-options.cfg` & `audio-backend.cfg` — persisted 3DS interface, controls, and audio settings
- `runtime.log` & `runtime-stderr.log` — startup, asset check, FMV timing, and error diagnostics

When reporting an issue on [GitHub Issues](https://github.com/Raihwy3/ThieviusCTR/issues), please include `runtime.log`, `runtime-stderr.log`, your 3DS model (Old/New 3DS/2DS), which cutscene source you are using (`.m2v`, `.bik`, or `.cmov`), and steps to reproduce.

---

## Building from Source

### Nintendo 3DS Runtime (`ThieviusCTR.3dsx`)

Requirements:

- [devkitPro](https://devkitpro.org/) with `devkitARM`, `libctru`, `citro3d`, `picasso`, `3dsxtool`, and `smdhtool` (`DEVKITPRO` environment variable pointing to your install, e.g., `/opt/devkitpro`)
- devkitPro 3DS portlibs `3ds-curl`, `3ds-mbedtls`, and `3ds-zlib` (checked under `$THIEVIUS_UPDATE_PORTLIBS`, `$DEVKITPRO/portlibs/3ds`, or `local/update-deps/opt/devkitpro/portlibs/3ds`)
- Python 3, `make`, and `curl` / `tar` (used on the first build to fetch and cross-compile FFmpeg `7.1.1`)

Build and stage the asset-free package from the repository root:

```bash
python3 platform/3ds/build_runtime.py
python3 platform/3ds/package_runtime.py
```

What these scripts do:

- `build_runtime.py` compiles vendored **OpenDSP**, builds the ARM gameplay core (`libcane-gameplay.a`), BRX rebuilder, on-console launcher, startup updater ([`platform/3ds/updater_deps.py`](platform/3ds/updater_deps.py)), shader (`runtime.v.pica`), invokes `platform/3ds/build_ffmpeg_3ds.py` (which downloads FFmpeg `7.1.1`, applies the `libavformat/bink.c` patch, and cross-compiles minimal LGPL static libs), and links `build/3ds/ThieviusCTR.3dsx` + `ThieviusCTR.smdh`.
- `package_runtime.py` creates the clean, redistributable, asset-free archive at `dist/ThieviusCTR-test.zip` containing `ThieviusCTR.3dsx`, `ThieviusCTR.smdh`, `README-runtime.txt`, `CREDITS.txt`, `LICENSE-OpenDSP.txt`, `LICENSE-FFmpeg-LGPL2.1.txt`, and `LICENSE-Mozilla-MPL-2.0.txt`. *(Do not distribute `local/` or archives built with `--zip-assets` — those contain your personal extracted game data.)*

### Upstream Desktop Builds (Windows / Linux)

Desktop Windows and Linux CMake targets from upstream ProjectCane remain in the repository (`cmake --preset windows-release`, `cmake --preset linux-release`, and [`packaging/linux/README.md`](packaging/linux/README.md)). See [upstream ProjectCane](https://github.com/theclub654/ProjectCane) for desktop releases and guides.

---

## Credits & Licenses

Full third-party license notices, upstream revisions, and LGPL v2.1 rebuild/relinking details are documented in [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md) and [`platform/3ds/CREDITS.txt`](platform/3ds/CREDITS.txt).

| Project / Library | Role in ThieviusCTR | License |
| --- | --- | --- |
| [**ProjectCane**](https://github.com/theclub654/ProjectCane) (`theclub654`) | Upstream engine reconstruction & PC port (rev `f77825d`) | No root OSI license file in upstream base; upstream rights retained by respective authors |
| [**FFmpeg**](https://ffmpeg.org/) `7.1.1` | Stripped static `libavformat`/`libavcodec`/`libavutil` (`bink`, `binkaudio_dct`/`rdft`, `mpeg2video`) with `libavformat/bink.c` patch via `build_ffmpeg_3ds.py` | **GNU LGPL v2.1 or later** ([`LICENSE-FFmpeg-LGPL2.1.txt`](platform/3ds/LICENSE-FFmpeg-LGPL2.1.txt)) |
| [**OpenDSP**](https://github.com/Cellenseres/OpenDSP) (`Cellenseres`) | Built-in open-source 3DS DSP firmware & library (rev `a15c8615`); MP3 tables/dataflows derive from [`minimp3`](https://github.com/lieff/minimp3/tree/ea99364f61c14656440e8d77e9c233ccf3124633) (CC0) | **BSD 2-Clause** ([`LICENSE`](platform/3ds/vendor/OpenDSP/LICENSE)) + **CC0** (`minimp3`) |
| [**Eyes of Gold Launcher**](https://github.com/Raihwy3/eyes-of-gold) (`Raihwy3`) | Startup GitHub Release updater, PICA200 texture-coordinate reference, pinned OpenDSP vendor copy, dual-screen launcher inspiration | **MIT License** |
| [**PICA-SR**](https://github.com/ponuno/PICASR-FSR-for-3DS) (`ponuno`) | Optional PICA200 edge-AA and sharpening TEV passes; used with permission from the author | Upstream repository has no license file; permission granted directly by the author |
| [**Conker3DSRecomp**](https://github.com/Raihwy3/Conker3DSRecomp) | Design reference for tagged HUD routing, console text isolation and vertically supersampled stereo | Referenced (no code copied) |
| [**jsmn**](https://github.com/zserge/jsmn) + **Mozilla CA Bundle** | Strict zero-alloc JSON parser & TLS root store for the startup updater ([`vendor/updater/`](platform/3ds/vendor/updater)) | **MIT** ([`jsmn-LICENSE.txt`](platform/3ds/vendor/updater/jsmn-LICENSE.txt)) + **MPL 2.0** ([`LICENSE-Mozilla-MPL-2.0.txt`](platform/3ds/LICENSE-Mozilla-MPL-2.0.txt)) |
| [**3ds-curl**](https://curl.se/), **3ds-mbedtls**, **3ds-zlib / minizip** | devkitPro 3DS HTTPS, SHA-256, and ZIP extraction libraries for the startup updater | **MIT** (`curl`) / **Apache-2.0** (`Mbed TLS`) / **zlib** (`zlib`/`minizip`) |
| [**devkitPro**](https://devkitpro.org/) / [**libctru**](https://github.com/devkitPro/libctru) / [**Citro3D**](https://github.com/devkitPro/citro3d) | 3DS toolchain, OS/hardware bindings, and PICA200 GPU library | **zlib License** |
| **`Video_player_for_3DS`** (`Core-2-Extreme`) & **`topos_3ds-video-player`** (`mockmodular`) | Consulted as references while debugging real-hardware Y2R synchronization | Referenced (no code copied) |
| **Upstream Desktop Libraries** (`Libraries/`) | GLM (MIT/Happy Bunny, with small `#ifdef __3DS__` newlib typedef fix), SDL2, GLFW, ImGui, nlohmann/json, stb, GLAD, Windows FFmpeg LGPL v3 | See [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md) |

**FFmpeg LGPL v2.1 Compliance:** `ThieviusCTR.3dsx` statically links minimal LGPL v2.1+ FFmpeg libraries. All FFmpeg build configuration flags, the small `libavformat/bink.c` patch ([`platform/3ds/build_ffmpeg_3ds.py`](platform/3ds/build_ffmpeg_3ds.py)), the complete application link rules ([`platform/3ds/build_runtime.py`](platform/3ds/build_runtime.py)), and the verbatim LGPL v2.1 text ([`platform/3ds/LICENSE-FFmpeg-LGPL2.1.txt`](platform/3ds/LICENSE-FFmpeg-LGPL2.1.txt)) are included so anyone can rebuild and relink against a modified FFmpeg.

---

## AI Usage Disclaimer

AI coding tools were used during the development, porting, debugging, testing, and documentation of the Nintendo 3DS platform layer (`platform/3ds/`) and project organization. All changes are compiled with standard toolchains and verified via automated checks, emulator runs, and real Nintendo 3DS hardware testing.

---

## Legal

ThieviusCTR is an unofficial fan project and is not affiliated with, endorsed by, or connected to **Sony Interactive Entertainment**, **Sucker Punch Productions**, or **Nintendo**. *Sly Cooper* and all related characters, trademarks, assets, and intellectual property belong to their respective owners. No original game data or Nintendo DSP firmware is distributed in this repository or its releases.
