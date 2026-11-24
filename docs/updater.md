# Startup Updates

With Wi-Fi connected, the Homebrew `.3dsx` launcher checks the latest public GitHub Release at `https://api.github.com/repos/Raihwy3/ThieviusCTR/releases/latest` before extracting or starting the game. `B` skips the check. Offline consoles, unavailable endpoints, rate-limited requests, or malformed responses leave normal startup available. The network check runs with a short timeout on a dedicated joinable worker thread (`socInit` / `256 KiB` socket buffer cleanly released on exit).

When a newer non-draft, non-prerelease version is found, the bottom screen offers **A: Install** or **B: Not now** (also selectable by touch). A SHA-256-verified ZIP download is staged on the SD card: only `ThieviusCTR.3dsx` and `ThieviusCTR.smdh` inside `sd:/3ds/ThieviusCTR/` are replaced. Your ISO, `Worlds/`, `Cutscenes/`, `Saves/`, and `.cfg` files are left untouched. The downloaded archive remains as `launcher-update.zip`.

The update takes effect on the next launch: **A: Exit** quits cleanly so Homebrew Launcher reloads the new binary, while **B: Play** continues running the already-loaded build. A `"TCTU"` rollback journal (`launcher-update.pending`) and `.update-old` backups automatically restore the previous binary on the next boot if replacement is interrupted.

Creating an empty `no-update-check.flag` file inside `sd:/3ds/ThieviusCTR/` disables automatic startup checks (interrupted-update recovery still runs first even when checks are disabled).

## Publishing a Release

1. Set `CANE_RELEASE_TAG` in [`platform/3ds/version_3ds.h`](../platform/3ds/version_3ds.h) to the release tag before building; keep `CANE_ENGINE_VERSION` at the base numeric version for revision releases (supported tags: `vMAJOR.MINOR.PATCH` and `vMAJOR.MINOR.PATCH-rREVISION`). The installed build compares this tag against the latest release so it will not offer to replace itself with the same or older version.
2. Mark the GitHub Release as **Latest** (with **Draft** and **Set as a pre-release** unchecked).
3. Build via `python3 platform/3ds/build_runtime.py` and `python3 platform/3ds/package_runtime.py`, and attach an asset named `ThieviusCTR*.zip` (without `source` in the filename) — for example `ThieviusCTR-v0.2.0.zip`. The ZIP must contain the standard `3ds/ThieviusCTR/ThieviusCTR.3dsx` and `3ds/ThieviusCTR/ThieviusCTR.smdh` pair, and stay under the updater size caps (`24 MiB` ZIP / `32 MiB` `.3dsx`). GitHub supplies the `sha256:` asset digest automatically; downloads verify that digest, content length, ZIP CRCs, `3DSX`/`SMDH` headers, and HTTPS TLS certificates/hostnames (including redirects).

## Building & Host Verification

The 3DS build links devkitPro portlibs `3ds-curl`, `3ds-mbedtls`, and `3ds-zlib` (`minizip` + `zlib`). [`platform/3ds/updater_deps.py`](../platform/3ds/updater_deps.py) looks under `$THIEVIUS_UPDATE_PORTLIBS`, `$DEVKITPRO/portlibs/3ds`, or the ignored fallback directory `local/update-deps/opt/devkitpro/portlibs/3ds`.

[`platform/3ds/check_updater.py`](../platform/3ds/check_updater.py) compiles and runs the updater parser, `libcurl` TLS transport, SHA-256 verification, transactional replacement, and `"TCTU"` recovery suite on the host with `-fsanitize=address,undefined` against a local ephemeral HTTPS server (requires host `gcc`, `libcurl`, `minizip`, `zlib`, OpenSSL dev headers, and the `openssl` CLI).

The separate `cia-ThieviusCTR-*.zip` asset is for manual installation. Its prefix deliberately excludes it from the 3DSX updater.
