# Melee Unlocked / YouGame handoff for Crunch

Audited October 28, 2026: Windows staging app 0.13.1086 (39e255b7), native build
880b2eeb-0d00-4a00-8000-0000000b001e, reporting MU 0.9.63. Its supplied patch is
upstream v0.9.63 plus YouGame integration. The [full audit](yougame-audit-2026-10-28.md)
indexes every change: 308 paths against 0.9.63 and 328 against current MU 0.9.64.

## What is already working

The Source gameplay rollback implementation (`mu_online.c`), early-input rules, authored-pose
code and shader generator are retained. The subframe solver has a profiling-only change.
No replacement with a Brawlback gameplay rollback engine was found in the supplied patch.

Two copies of the actual downloaded executable/DLL ran through the production shared-memory
ENet path against a local desktop/room fixture. Across 24 accepted client runs: 39,520 confirmed
state checks and 25,357 shared replay post frames, zero differences. Early-input capability bit 0,
MUE/0xE1 packets, unsequenced channel 1, native prediction and bad-early-pad trust fallback work.
Timed samples had 13.7% fewer rollback events at fixed delay and 37.5% fewer with jitter when
early inputs were enabled. This is local synthetic evidence, not a public Internet/two-PC or
Slippi Dolphin performance certification. No early-input implementation needs adding from scratch.

## Proposed controller parity fixes ? prioritize before claiming all MU settings work

1. **Preserve device family through the relay and route it through the corresponding MU path.**
   `desktop/dist/native-pad-relay.js` sends `kind: ps|xbox|switch|other`, but
   `yougame_relay_pads.h::State::read_locked` returns only XINPUT_STATE. `window.cpp::read_devices`
   consequently applies `g_pad_bindings` and Xbox family options to every relayed standard pad.
   The dedicated observation fixture proves a custom PS south-button-to-B mapping instead produces
   default Xbox A through this route. A saved PS/Switch device assignment can also point at a
   family whose native devices are no longer visible inside the sandbox.

   Expose the retained relay family/id alongside the state. Route PS and Switch into their
   respective connection facts, binding tables, trigger conventions, deadzones/calibration and
   port selection. Convert normalized buttons into the corresponding native family masks before
   applying bindings; using the same 16-bit value directly is wrong (PS Cross is 0x20 while the
   normalized south button is XInput A/0x1000). Keep the existing Xbox path for Xbox. Make live
   diagnostics and binding capture follow the same route as gameplay and the early-input sampler.
   Retain explicit per-port assignments; do not silently clear a player's profile to make fallback work.

2. **Provide a verified path for nonstandard HID/DInput and box raw-button modes.**
   `NativePadState::payload` currently filters on `mapping === standard`. That drops nonstandard
   DInput devices from this relay, while native generic-HID/HayBox handling remains in MU source.
   The game sandbox cannot be assumed to open the same HID handle itself. Add a bounded,
   versioned raw-device channel in the app helper, or a read-only shared snapshot with original
   button/axis information. Preserve raw button masks, model identity, trigger axis layout and
   independent buttons for box modifiers/SOCD. A normalized standard-gamepad payload cannot
   represent every raw button/profile of every supported box. Do not bypass the sandbox or give
   both processes ownership of the same USB device.

3. **Keep the raw GC adapter mirror, verify its edge cases.**
   The shared report layout preserves original 37-byte reports, existing neutral/trigger decoding
   and GC bindings. Check official/Mayflash/HHL paths, B0XX/Frame1 in GC mode, helper restarts,
   device unplug/replug, stale reports and all four ports. Confirm origin reset on mirror changes,
   pressure/light-shield behavior, no extra deadzone, and that rumble uses its return channel.
   Reset/reconnect should ask the helper rather than opening the adapter inside the game.

4. **Add standard-pad rumble forwarding.**
   `relay_pads::State::rumble` currently suppresses native rumble and logs that relayed pads have
   no rumble yet. Add an app/helper-owned output request by stable runtime device id, off the game
   thread; clear motors on disconnect, focus/policy transitions and game exit. Keep GC mirror output
   separate. This is an explicit missing feature, not merely an untested setting.

5. **Preserve profile/action and sampling parity.**
   Keep the fork's 27-action profile fix, older missing-modifier default handling, keyboard/stick
   modifiers and trigger thresholds. Test hotplug slot changes without moving profiles to another
   device. Relay ids are per-run opaque ids, so they are not persistent profile keys. Define a
   privacy-preserving stable model/device identity for saved selection where needed. Measure
   helper-to-game update intervals; a 1 ms sampler reading a relay updated at 60 Hz does not create
   1 kHz physical samples. Raw GC mirror and standard-pad IPC need independent measurements.

Acceptance scenarios: for every supported connection mode, compare actual in-game pad bytes,
trigger travel and stick/modifier coordinates with standalone MU under the same saved profile.
Include all four ports/mixed families, custom PS/Switch remaps, HayBox DInput, B0XX/Frame1 modes,
XInput box modifier coordinates, saved profiles, rebind capture, disconnect/replug and rumble.
Then run the same sources through an online early-input pair to check early and confirmed pads
use identical mapping. Existing box-coordinate/profile tests are useful but do not certify hardware.
Only accept physical models tested; report unsupported modes explicitly.

## Proposed NVIDIA activation fix

Observed: `dlss: built without the Streamline SDK` on RTX 5070. The package contains the
Streamline/DLSS runtime DLLs, but `GX_STREAMLINE` was not compiled into this executable.
The supplied source patch preserves Streamline; no intentional deletion of DLSS/Reflex was found.
The precise builder mistake is unproven. A clean build without the required SDK files at CMake
configure time naturally takes upstream's optional compiled-out fallback.

1. Check MELEE_STREAMLINE_ROOT in the **actual build directory used for this executable** before
   configuration. Upstream requires include/sl.h, lib/x64/sl.interposer.lib,
   bin/x64/sl.interposer.dll and bin/x64/sl.common.dll. Runtime copying after build is insufficient.
2. Reconfigure/rebuild the intended runtime target (`runtime` versus `runtime_ns`, depending on
   the configuration) with GX_STREAMLINE, SDK include path/import library and delayed interposer
   loading. Rebuild the host that links that runtime. Keep it consistent with the game DLL/ABI.
3. Make packaging emit a feature manifest and fail if a build advertised as NVIDIA-enabled links
   the compiled-out fallback. Include actual SDK/runtime versions, target/configuration and hashes.
4. On RTX, verify the log shows feature loading and successful activation for DLAA/DLSS, Reflex
   On/Boost, and frame generation separately. Test sandbox startup, adapter selection and fallback
   after initialization failure. Bundled DLL presence or an exposed setting is not activation proof.
5. Keep Native and XeSS paths functional on non-RTX hardware. Experimental DLSS5 needs its own
   SDK/model/runtime validation; ordinary Streamline support alone is not proof it works.

## Intentional feature restrictions to review with Hero

- Diet Classic, Diet 64 and Crystal: upstream detects locally built artwork at startup and
  activates real pack assets. YouGame skips this automatic activation and uses renderer styles.
  Online reads additionally reject exact fighter tables/full look files. Comments cite unverified
  pack identity/online transition safety. The setup UI still exists, so it can misleadingly suggest
  prepared artwork will apply after restart. Restore only after validating identity and transition
  safety, or label the unavailable exact-artwork feature accurately.
- Slippi spectator listener is off in YouGame.
- YouGame identity/matchmaking/results replace Slippi's corresponding paths within app sessions;
  standalone fallback exists outside YouGame mode. If the intended product is both services in
  one app session, add an explicit backend choice and preserve backend-specific lifecycle/gates.
- Saved gameplay mod profiles, implicit Mods/Files loaders and Gecko source editing are suppressed;
  listing-supplied layers are authoritative. Vanilla listing passes --vanilla-only. Some old mod
  saves require explicit GCI layers. These restrictions have reasons but are visible parity changes.
- YouGame Ranked is its own best-of-three/rules/rating flow. Source currently uses Unranked scene
  states/random legal stage rather than the full Slippi Ranked stage-pick flow. Teams code-search
  is unavailable; room-owned teams and empty-code queue are supported.
- Two full Slippi Sys assets are reconstructed from retail disc deltas instead of shipped whole.
  Reconstructed target hashes match upstream originals; this is packaging, not removed menus.
- Mac work is a substantial port, including embedding/GL/Rosetta paths, not the Windows binary
  unchanged. Crunch's reported slowness needs Mac measurements; these Windows tests do not clear it.

## Visual issue and local upstream fixes

Crunch reports VSync on removes Hbox's artifacts, with Yoshi's Story recalled. The fork retains
upstream Windows Present logic and VSync-off default. Five actual YouGame Yoshi gameplay cases
(Unlocked, monitor, synchronized Unlocked, SSAA2/AF16, active XeSS AA) captured successfully.
Game captures do not rule out physical scanout tearing. Upstream local changes now default new
installs to VSync on while preserving saved explicit choices, and fix VRR re-probing after mode/
statistics changes. Match monitor remains the cap default.

A separate actual GPU reproduction found temporal AA motion history being changed by unseen
catch-up frames. Local upstream fix produced 17/17 images identical to a no-hidden-motion control,
versus horizontal-edge differences before. It is not established as Hbox's cause. The shared
history fix can matter to XeSS too, although NVIDIA is compiled out of this staging game.

Offline/online added-delay controls are now independent locally. Three added frames must not be
advertised as measured three-frame total CRT latency. Auto online delay is chosen from ping at
match start, not continuously and not for offline play.

The old Slippi gate used cached/startup latestVersion and did not guarantee a fresh refusal.
Local upstream changes verify before searches/connections/rematches and fail closed. They keep
an active match running and refuse its next game if newer/unknown. Do not apply this gate to
YouGame-only rooms as though they connect to Slippi. This patch is not in the downloaded build.

## Evidence and handoff state

[Full source/build differences](yougame-audit-2026-10-28.md) ?
[Prioritized findings](yougame-launch-findings-2026-10-28.md) ?
[Visual/input/gate triage](hbox-triage-2026-10-28.md).

Thirteen focused upstream and 21 fork tests passed; both upstream Release AVX2 engines built.
The dedicated controller fixture intentionally observes the routing mismatch. No public players
were contacted, no release was deployed, and no app/GitHub message was sent. This handoff proposes
controller/NVIDIA integration changes; it does not claim those changes have been applied to the
YouGame application or certified on every physical controller.
