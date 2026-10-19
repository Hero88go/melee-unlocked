Melee Unlocked for Linux

The README shipped in the Linux release is written by tools/package_linux_release.py (README).

Build and package from a Linux or WSL (Ubuntu 24.04) shell at the tree root:

  bash tools/build_linux_release.sh

That runs tools/build_linux.py (the ELF game library melee_game.so, the Source Port melee_source
and the launcher MeleeUnlocked, AVX2 like the Windows build so matches stay bit-exact with it) and
tools/package_linux_release.py, which writes release/MeleeUnlocked-<version>-linux-x86_64.tar.gz
with the libraries the programs need in lib/. bugwatch/publish_release.ps1 runs it on every
publish and attaches the archive beside the Windows zip.

Layout: compat/ holds the Win32 headers and implementations the shared host compiles against on
Linux (force-included as compat/windows.h), full/ the Source Port target and its Linux pieces
(OpenGL renderer hooks, SDL audio, FFmpeg media, crash handler), launcher/ the ImGui launcher.
