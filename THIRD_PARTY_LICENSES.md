# Third-Party Licenses & Attributions

ThieviusCTR builds on upstream [ProjectCane](https://github.com/theclub654/ProjectCane) and incorporates open-source libraries for the Nintendo 3DS target and desktop builds. **Zero copyrighted Sony game assets and zero proprietary Nintendo DSP firmware are distributed in this repository or its releases.**

---

## Upstream Base Project

| Component | Repository / Version | License / Notes |
| --- | --- | --- |
| **ProjectCane** (`theclub654` & contributors) | [github.com/theclub654/ProjectCane](https://github.com/theclub654/ProjectCane)<br>Base revision: `f77825d6bc9be3f795ead9d140b410fb9c9c22c1` | Upstream contains no root OSI license file in the pinned base revision; all upstream rights remain with their respective authors. Reuses the world loader, gameplay, collision, animation, render submissions, fonts, audio/sequencing, and BRX/dialog extraction logic. |

---

## Nintendo 3DS Runtime Dependencies

| Component | Source / Version | License | Local Notice & 3DS Notes |
| --- | --- | --- | --- |
| **FFmpeg** (`libavformat`, `libavcodec`, `libavutil`) | [ffmpeg.org](https://ffmpeg.org/)<br>Version `7.1.1` | **GNU LGPL v2.1 or later** | [`platform/3ds/LICENSE-FFmpeg-LGPL2.1.txt`](platform/3ds/LICENSE-FFmpeg-LGPL2.1.txt)<br>Statically linked into `ThieviusCTR.3dsx` via `--disable-everything` (enabling only `bink`, `binkaudio_dct`, `binkaudio_rdft`, and `mpeg2video` decoders; `bink` and `mpegvideo` demuxers; and `mpegvideo` parser). Includes a small patch to `libavformat/bink.c` (`avio_skip` for `AVDISCARD_ALL` audio tracks) applied automatically by [`platform/3ds/build_ffmpeg_3ds.py`](platform/3ds/build_ffmpeg_3ds.py). |
| **OpenDSP** (`Cellenseres`) | [github.com/Cellenseres/OpenDSP](https://github.com/Cellenseres/OpenDSP)<br>Rev `a15c8615c1659f34bfb2b38df69a9860f4cc7080` | **BSD 2-Clause** | [`platform/3ds/vendor/OpenDSP/LICENSE`](platform/3ds/vendor/OpenDSP/LICENSE)<br>Vendored from Eyes of Gold's pinned copy under `platform/3ds/vendor/OpenDSP/` (`source.json` records SHA-256 provenance). Provides open-source 3DS DSP firmware so no Nintendo `dspfirm.cdc` dump is required. |
| **minimp3** (via OpenDSP) | [github.com/lieff/minimp3](https://github.com/lieff/minimp3/tree/ea99364f61c14656440e8d77e9c233ccf3124633) | **CC0 1.0 Public Domain** | [`platform/3ds/vendor/OpenDSP/firmware/mp3/NOTICE`](platform/3ds/vendor/OpenDSP/firmware/mp3/NOTICE)<br>OpenDSP notes that its MP3 tables and transform dataflows derive from `minimp3`. |
| **devkitPro / libctru** | [github.com/devkitPro/libctru](https://github.com/devkitPro/libctru) | **zlib License** | System library linked from `$DEVKITPRO/libctru` (`-lctru`) alongside devkitARM, `picasso`, `3dsxtool`, and `smdhtool`. |
| **Citro3D** | [github.com/devkitPro/citro3d](https://github.com/devkitPro/citro3d) | **zlib License** | PICA200 3D graphics library linked from `$DEVKITPRO/libctru` (`-lcitro3d`). |
| **GLM** | [`Libraries/include/glm/`](Libraries/include/glm/) | **MIT / Happy Bunny License** | Includes a small `#ifdef __3DS__` newlib integer typedef compatibility fix in 4 headers (`ext/scalar_int_sized.hpp`, `ext/scalar_uint_sized.hpp`, `fwd.hpp`, `gtc/type_precision.hpp`). |
| **Eyes of Gold Updater** | [github.com/Raihwy3/eyes-of-gold](https://github.com/Raihwy3/eyes-of-gold) | **MIT License** | Startup GitHub Release updater design & implementation reused and adapted in `platform/3ds/update_{core,http,3ds}.*` and `platform/3ds/check_updater.py`. |
| **jsmn** (`zserge`) | [github.com/zserge/jsmn](https://github.com/zserge/jsmn) | **MIT License** | [`platform/3ds/vendor/updater/jsmn-LICENSE.txt`](platform/3ds/vendor/updater/jsmn-LICENSE.txt)<br>Vendored at [`platform/3ds/vendor/updater/jsmn.h`](platform/3ds/vendor/updater/jsmn.h) for strict zero-allocation JSON parsing in `update_core.c`. |
| **Mozilla CA Bundle** (`cacert.pem`) | [curl.se/ca/cacert.pem](https://curl.se/ca/cacert.pem) | **Mozilla Public License 2.0** | [`platform/3ds/LICENSE-Mozilla-MPL-2.0.txt`](platform/3ds/LICENSE-Mozilla-MPL-2.0.txt)<br>Vendored at [`platform/3ds/vendor/updater/cacert.pem`](platform/3ds/vendor/updater/cacert.pem) for HTTPS certificate verification (`CURLOPT_CAINFO_BLOB`). |
| **3ds-curl** (`8.4.0-1`) | devkitPro portlibs / [curl.se](https://curl.se/) | **MIT** (curl License) | Linked statically (`-lcurl`) for HTTPS-only release checks and ZIP downloads. |
| **3ds-mbedtls** (`2.28.8-1`) | devkitPro portlibs | **Apache-2.0** | Linked statically (`-lmbedtls -lmbedx509 -lmbedcrypto`) for TLS and streaming SHA-256 verification. |
| **3ds-zlib / minizip** (`1.3.1-1`) | devkitPro portlibs | **zlib License** | Linked statically (`-lminizip -lz`) for CRC-checked ZIP validation and extraction of `ThieviusCTR.3dsx` / `ThieviusCTR.smdh`. |

### FFmpeg LGPL v2.1 Source & Relinking Compliance

`ThieviusCTR.3dsx` statically links minimal LGPL v2.1+ FFmpeg libraries (`libavformat.a`, `libavcodec.a`, `libavutil.a`) built from upstream FFmpeg `7.1.1`. To satisfy the LGPL v2.1 requirement allowing users to modify and relink against a modified version of FFmpeg:

1. The exact FFmpeg version (`7.1.1`), configuration flags, and `libavformat/bink.c` patch are contained in [`platform/3ds/build_ffmpeg_3ds.py`](platform/3ds/build_ffmpeg_3ds.py).
2. Full build and link rules (object files, linker flags, and `3dsxtool` / `smdhtool` invocation) are provided in [`platform/3ds/build_runtime.py`](platform/3ds/build_runtime.py).
3. Verbatim LGPL v2.1 license text is included in [`platform/3ds/LICENSE-FFmpeg-LGPL2.1.txt`](platform/3ds/LICENSE-FFmpeg-LGPL2.1.txt) and bundled inside release packages alongside `ThieviusCTR.3dsx`.

---

## Upstream Desktop Dependencies (`Libraries/`)

These dependencies live under `Libraries/` from upstream ProjectCane (not linked into the 3DS binary except header-only GLM above):

| Component | Location | License / Notice |
| --- | --- | --- |
| **SDL2** | `Libraries/include/SDL2/`, `Libraries/lib/` | zlib License — [`Libraries/licenses/SDL2.txt`](Libraries/licenses/SDL2.txt) |
| **GLFW 3.3** | [`Libraries/include/GLFW/glfw3.h`](Libraries/include/GLFW/glfw3.h) | zlib/libpng License |
| **Dear ImGui** | `Libraries/include/imgui/` | MIT License (plus `dirent` MIT — [`Libraries/include/imgui/dirent/LICENSE`](Libraries/include/imgui/dirent/LICENSE)) |
| **nlohmann/json** (`3.12.0`) | [`Libraries/include/nlohmann/json.hpp`](Libraries/include/nlohmann/json.hpp) | MIT License |
| **stb** | `Libraries/include/stb/` | MIT or Public Domain — [`Libraries/include/stb/LICENSE`](Libraries/include/stb/LICENSE) |
| **GLAD / KHR** | `Libraries/include/glad/`, `Libraries/include/KHR/` | Public Domain / MIT / Apache 2.0 (generated by glad 0.1.36) |
| **FFmpeg (Windows x64 shared)** | `Libraries/ffmpeg/ffmpeg-master-latest-win64-lgpl-shared/` | GNU LGPL v3 — [`LICENSE.txt`](Libraries/ffmpeg/ffmpeg-master-latest-win64-lgpl-shared/LICENSE.txt) (desktop Windows builds only) |

---

## Acknowledgements & Reference Projects

- **[Eyes of Gold Launcher](https://github.com/Raihwy3/eyes-of-gold)** (`Raihwy3` and contributors) — referenced for 3DS PICA200 texture-coordinate conventions, the pinned OpenDSP vendor source, and inspiration for the dual-screen on-console extractor/launcher.
- **`Core-2-Extreme / Video_player_for_3DS`** and **`mockmodular / topos_3ds-video-player`** — consulted as behavioral references while debugging real-hardware 3DS Y2R CDMA transfers and event synchronization (no code copied).

---

## Legal Disclaimer

ThieviusCTR is an unofficial fan project and is **not** affiliated with, endorsed by, or connected to **Sony Interactive Entertainment**, **Sucker Punch Productions**, or **Nintendo**. *Sly Cooper* and all related characters, trademarks, models, levels, audio, and video assets belong to their respective owners.
