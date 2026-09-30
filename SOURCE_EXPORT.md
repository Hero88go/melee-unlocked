# Source export for Melee Unlocked 0.8.5

Native decomp base: `039c4bf4ca33338c35d21901ad19b7ede19d19ad` (doldecomp/melee). The archive already carries
the actual adapted native `src`, `libs` and `config` inputs. No original disc data or
Git databases are included. The repository form uses the pinned submodule plus
`sourceport/patches/melee-native.patch` and `tools/prepare_native_sources.py`.

Build the native DLL with GCC 14+ using `sourceport/game` and the supplied MinGW
toolchain, then build the MSVC host/launcher as described in docs/build-source-port.md.
The three `melee/src/sysdolphin/baselib` files are the exact inputs used by the
host-only animation adapter generator. Static translation is regenerated from
your own NTSC 1.02 ISO; generated translations and extracted Nintendo data are absent.

Optional NGX headers and the static SDK are not exported: acquire them from NVIDIA
under its own terms for a DLSS5 build. Streamline/Intel runtime binaries are also
omitted. Standard source builds run without these optional proprietary components.
Keep third-party notices; project terms are GPL-3.0-or-later, see LICENSE and NOTICE.
