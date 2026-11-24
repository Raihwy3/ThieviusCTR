ThieviusCTR - installed HOME Menu application

Install the CIA using your preferred homebrew title installer. Keep your ISO,
prepared game files, settings and Saves in /3ds/ThieviusCTR/ on the SD card.
This is the same folder used by the Homebrew Launcher version.

No ISO or extracted game files are included. Fresh installations require your
own supported US retail Sly Cooper ISO, named Slygame.iso, in that folder.
The first launch prepares game files on the console.

With Wi-Fi connected, startup can check for newer releases and download a
verified raw CIA to /3ds/ThieviusCTR/launcher-update.cia. Close the game and
install that file with FBI. This does not install over a running title.
Both builds share saves and settings, so either launcher uses the same progress.
An empty no-update-check.flag in the game folder disables update checks.

The CIA has been installed and launched on a console. The animated HOME Menu
banner still has known playback issues. Build/package validation does not
replace real-console testing.

HOME Menu artwork supplied by Raihwy3. The short original jingle is synthesized
by make_jingle.py; it contains no sampled game music. The boot logo is makerom's
MIT-licensed Homebrew logo. Credits and license notices are included.

The banner is original extruded 3D text: gold Thievius script and white CTR,
built using pycgfx (Floogle, MIT). Its intended animation is a 10-second
Thievius rotation with stationary CTR, but this does not play correctly on
real HOME Menu yet. The pycgfx license is included. No Nintendo banner model or animation is used.

Build requirements beyond the normal 3DS toolchain: makerom, bannertool, Pillow,
pyelftools, and (to regenerate the text mesh) matplotlib, numpy, shapely,
mapbox-earcut, gltflib, plus the system Z003 and DejaVu Sans Bold fonts.
make_banner.py uses pycgfx commit 6c2e0cd53a5f4c3265b9aa9a1f30806f8291116e;
clone https://github.com/skyfloogle/pycgfx into local/cia-tools/pycgfx and check
out that commit, or pass --pycgfx /path/to/pycgfx. The existing banner.cgfx and
jingle.wav can be packaged without regenerating artwork or audio.

python3 platform/3ds/build_cia.py --makerom /path/to/makerom --bannertool /path/to/bannertool

The CIA also passed installation, gameplay, effects and clean-exit checks in
an isolated Azahar profile. The animation checks do not confirm correct HOME Menu playback.

Some makerom 0.19.0 builds misencode the application version due to a macro
name collision. This package uses Project_CTR commit
d52c2b0e788c2d085763d7c5dcc4b2edd9ffb7d8 with makerom-version.patch applied.
To reproduce it, check out that commit, run git apply with the supplied patch
from the Project_CTR root, then run make deps -j4 and make -j4 in makerom/.
Pass the resulting makerom/bin/makerom to build_cia.py. The package validator
checks the installed version and rejects incorrectly encoded versions.
