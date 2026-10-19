Melee Unlocked 0.9.0 - native Linux offline preview

This preview runs the 0.9 source game as a native Linux executable and ELF
library. It is an early Linux build with an offline host. Slippi matchmaking,
the replay frontend, the Windows launcher/settings interface, and the mod
import/profile workflow are not available in this preview. Graphics and
physical Linux controller/audio compatibility are still being validated.

Requirements

  - An x86_64 Linux desktop with glibc and a CPU with AVX2 (Intel Haswell 2013
    or newer, AMD Ryzen or newer), the same requirement as the Windows build.
    The binary is built on Ubuntu 24.04;
    older distributions may need a source build.
  - An OpenGL 4.5 driver, SDL2, GLEW 2.2, and libusb 1.0.
  - glslangValidator and spirv-cross on PATH. The current renderer uses these
    programs when compiling game shaders, including after installation.
  - Your own clean, uncompressed Super Smash Bros. Melee NTSC 1.02 ISO.
    PAL, other revisions, compressed images, and patched DOLs are rejected.
    No game ISO is included.

On Ubuntu 24.04, install the runtime packages with:

  sudo apt install libsdl2-2.0-0 libglew2.2 libusb-1.0-0 glslang-tools spirv-cross

Use your distribution's graphics driver. Shader tools are separate runtime
requirements; installing only the shared libraries is insufficient.

Launch

Extract the entire archive, open a terminal there, and supply the path to
your ISO:

  ./MeleeUnlocked.sh "/home/you/Games/Melee 1.02.iso"

Alternatively, place your ISO beside MeleeUnlocked.sh as melee.iso and run:

  ./MeleeUnlocked.sh

Keep melee_linux, melee_game.so, melee_game.snapexcl, and Sys/GC together.
Relative paths are resolved from your terminal's working directory. Shader
caches use $XDG_CACHE_HOME/melee-unlocked/shaders, or
~/.cache/melee-unlocked/shaders when XDG_CACHE_HOME is unset. Close the game
window to exit.

Display rate

The default presentation target is 120 FPS; game simulation remains 60 Hz.
This is a presentation target, not a guarantee of measured frame rate.
Choose a different target with --fps (60 through 1000):

  ./MeleeUnlocked.sh "/home/you/Games/Melee 1.02.iso" --fps 60

The Linux renderer and authored-pose presenter are still under validation.
Treat visual glitches or differences from the Windows build as preview bugs.

Controllers

SDL gamepads and official/compatible Wii U-mode GameCube USB adapters have
native input paths. Physical devices and rumble still need testing on Linux
hardware. An adapter must be in Wii U mode. To grant the active desktop user
access to the supported adapter, install the supplied udev rule:

  sudo install -m 644 51-melee-unlocked-gc-adapter.rules /etc/udev/rules.d/
  sudo udevadm control --reload-rules

Unplug and reconnect the adapter afterward. Use --no-gc-adapter to disable
direct adapter polling while testing another controller.

Without a gamepad on port 1, keyboard controls are:

  Arrow keys: main stick       I/J/K/L: C-stick
  Z: A       X: B              C: X       V: Y
  Q: L       E: R              S: Z       Enter: Start

Saves and logs

Slot A saves are .gci files in:

  $XDG_DATA_HOME/melee-unlocked/Card A

If XDG_DATA_HOME is unset, the default is:

  ~/.local/share/melee-unlocked/Card A

Choose a separate save folder and a log file with:

  ./MeleeUnlocked.sh "/home/you/Games/Melee 1.02.iso" \
    --card-dir "/home/you/Games/Melee Linux saves" \
    --log-file "/home/you/Games/melee-linux.log"

Useful diagnostics

  ./MeleeUnlocked.sh --version
  ./MeleeUnlocked.sh --memory-self-test
  ./MeleeUnlocked.sh "/home/you/Games/Melee 1.02.iso" --headless --frames 600 --fast

Headless mode is a developer check without a game window. It does not verify
graphics or physical audio/controller behavior.

Scope of this preview

The game library includes the 0.9 native gameplay sources and compiled
fighter/stage integrations. The Linux host currently accepts the clean retail
base ISO and has no mod importer or profile manager. Compilation of those
integrations does not establish mod-pack compatibility in this preview.

There is no Slippi matchmaking, replay browser/playback/recording frontend,
Windows launcher/settings parity, automatic updater, or Flatpak package yet.
This archive is for testing the native offline port, not a completed Linux
release with Windows feature parity. Include the log, Linux distribution,
graphics driver, and controller type when reporting a problem.

Licenses and third-party notices are in licenses/.
