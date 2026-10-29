# Hbox feedback: visual artifacts and input timing

Source: Crunch's complete message supplied by the owner on October 28, 2026. Hbox said the
game felt good overall but had off-putting horizontal visual artifacts. Crunch requested
consistent local timing comparable to CRT Melee. No game version, monitor model, refresh rate,
sync settings, graphics API, or recording accompanied the report.

## Priority 1: horizontal artifacts

Screen tearing is the first hypothesis to test. A horizontal break during motion fits tearing;
the description alone does not distinguish it from a rendering defect. Its absence on another
PC does not disprove the report.

There is relevant history: commit `9a299dc6` (October 17) addresses a report from a 0.8.99 user
with a 240 Hz G-Sync display, Reflex enabled, and VSync disabled. It adds display-count-based
VRR detection, a below-refresh cap, and synchronized presents after VRR is detected. That code
is present in the 0.9.64 tree. An older build remains plausible, but Hbox's build is unknown.

Confirmed defects in that detection path:

- A statistics interruption or counter reset retained the old refresh verdict and pending vote.
- A display refresh-rate change could reuse the verdict for the preceding mode.
- Undecidable measurement windows did not break a pending consecutive-window vote.
- The presentation loop did not restart its one-time below-refresh probe when statistics
  recovered or a verdict was invalidated. Full-refresh presentation can then remain
  undecidable, leaving automatic tear prevention inactive with VSync off.

The changes clear obsolete detection state, require fresh consecutive evidence, and re-arm
probing after recovery/mode changes. Regression cases fail on the preceding detector and pass
with the fix. This establishes defects in the code; it does not establish Hbox's root cause.

VSync off still permits tearing on fixed-refresh or unclassified configurations. Fresh standalone
installs now default to VSync on, with the label ?VSync (prevents tearing)?. Existing saved explicit
choices are preserved. Match monitor remains the normal launch default. The Video setting
explains that synchronized presentation may add display latency. DLSS frame generation has a separate presentation path
and must be recorded as part of reproduction settings.

Microsoft documents that frame statistics can be unreliable with multiple monitors or other
fullscreen applications: https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getframestatistics

Searches of public Twitch/YouTube information and public bot commands did not establish
Hbox's current monitor model, refresh rate, driver VSync, VRR, or settings for this session.
Historical event monitors and sponsorships are not evidence of the test setup.

Next reproduction evidence: exact build, graphics API, selected frame rate, VSync, Windows
refresh rate, monitor model, driver/monitor VRR, fullscreen mode, frame generation, and a clip.
Compare the same motion with VSync off/on and frame generation off. A physical-screen
recording can show a scanout tear that a game capture may omit. If horizontal lines persist
with synchronized presentation, investigate the renderer/interpolation with the same scene.

## Priority 2: consistent offline timing and clear controls

The former Frame delay slider applied offline only with Also use offline enabled. Offline added
delay defaulted off. Online defaulted to two frames, with optional Auto choosing two to six
from measured ping at match setup and holding the choice for that match. Offline play never
used Auto's ping-based selection.

The controls are now independent: Offline input delay offers Off (0 frames) or 1-9 frames;
Online input delay offers Auto or 1-9 frames. Existing saved shared-delay preferences migrate
without changing the previous offline queue length. Changing online delay no longer changes
offline timing. The offline path serves both engines and excludes menus/replay playback.

The number is added input queue delay, not total controller-to-screen latency. Three added
frames cannot be presented as a measured three-frame CRT equivalent. The simulation remains
at 60 ticks per second; higher rendered FPS does not speed up game logic. Keep defaults until
a same-hardware latency comparison establishes an appropriate console-matching preset.

Slippi's default enabled codes include Apply Delay to all In-Game Scenes, which applies online
delay to local gameplay:
https://github.com/project-slippi/Ishiiruka/blob/slippi/Data/Sys/GameSettings/GALE01r2.ini

The historical claim that offline delay was recently removed is not established by this review.
The optional offline queue is present from the 0.8.76 change (`6b340e7e`).

## Additional confirmed visual bug: temporal motion history

The D3D12 renderer recorded object poses and Streamline projection constants while draining
frames it would not present. DLAA/DLSS/XeSS evaluate presented images, so the next image could
receive motion relative to an unseen intermediate pose rather than the preceding displayed one.
A synthetic GPU scene with horizontal bands reproduced edge corruption with the actual
production renderer and NVIDIA DLAA. The control displayed one stationary image; the test
processed ten unseen moving frames and returned to the identical stationary displayed image.

Before the fix, the first affected image differed from its control at about 1.88% of pixels
using an RGB-channel difference greater than 8/255; its mean absolute RGB difference was
0.781/255. Across the next 17 displayed images the average was 0.185/255. After the fix,
**all 17 images matched the control exactly**. This demonstrates a real horizontal-edge defect,
not proof of Hbox's particular root cause. The YouGame executable has Streamline compiled out,
so NVIDIA DLAA cannot explain his YouGame session; XeSS uses the shared motion-history path.

The fix keys pose history to presentations, ignores drained-frame writes, preserves preceding
poses across multipass draws, and clears temporal state on discontinuities. Streamline constants
and reset consumption also wait for displayed frames. Regression tests cover unseen frames,
multipass draws, missing/reappearing draws, resets and retirement.

[Before/after image](../run-current-reports/hbox-visuals/temporal-probe/stationary-before-after.png) ?
[GPU fixture](../run-current-reports/hbox-visuals/temporal-probe/probe.cpp) ?
[GPU fixture build](../run-current-reports/hbox-visuals/temporal-probe/CMakeLists.txt)

## Later report and YouGame findings

Crunch subsequently says the artifacts vanish when VSync is enabled and recalls Yoshi's Story.
This makes tearing the leading explanation. It does not establish that the old Yoshi renderer
bug returned or that his app integration introduced a shader defect. Backbuffer captures can
miss scanout tears. The Windows fork retains the upstream Present synchronization expression.

The actual installed/latest staging path was exercised from the YouGame listing's Play button,
including ISO selection and offline Training. The downloaded native build is 0.9.63 plus a
308-file integration patch. It retains gameplay rollback and early inputs, but has controller
routing/feature differences and compiled-out Streamline support. The exhaustive
[YouGame audit](yougame-audit-2026-10-28.md) and [launch findings](yougame-launch-findings-2026-10-28.md)
record these separately from upstream fixes. No external messages or public matches were sent.

## Slippi update gate

The old standalone gate blocked a newer **known** `latestVersion`, but used cached user.json
and a startup background refresh. Failed refreshes and a release published during a long-lived
session could leave it stale. Therefore ?any update immediately blocks until testing? was too
strong a description of that implementation.

The local fix verifies Slippi's server version before each new server search, again before
connecting, and asynchronously before starting/rematching on an existing peer connection.
Missing, malformed and failed responses stay offline; the character screen remains responsive
while a check is pending. A cancelled check cannot authorize a subsequent game. Local test peers
stay independent. The approved protocol remains a build-time constant, currently 3.6.4; it is
not automatically advanced when Slippi publishes an update. The official latest release checked
in this session was [v3.6.4](https://github.com/project-slippi/Ishiiruka/releases/tag/v3.6.4).

This detects the version Slippi publishes through its user API. It cannot instantaneously know
about an unpublished change, block a release only in the launcher which does not change netplay,
or prove that manual compatibility testing happened. An already-running match is allowed to
finish; its next game requires a fresh check. YouGame-only rooms use a separate backend and
version/content key, so a Slippi server release does not inherently require disabling those rooms.
The YouGame fork has not received these local uncommitted gate changes.

## Validation

Release AVX2 builds passed for both `melee_port` and `melee_source`. Thirteen focused upstream
checks passed, including settings/default/migration, offline delay, VRR, temporal history, GPU
resources, subframes, Slippi login/version refusal and ticket handling, textures and stage safety.
Twenty-one fork checks passed; the audit explains that one is a controller mismatch observation.
The VRR regressions produced five failing assertions before their fix and passed afterward.
The temporal GPU before/after comparison reproduced the defect and eliminated its image error.

Standalone matrix coverage included Unlocked first, monitor cap, both with VSync on/off, SSAA2/AF16,
DLAA, DLSS Quality/Performance, Reflex/Boost combinations, frame generation, interpolation,
D3D11, Yoshi/Fountain scenes and 1080p/1440p. DLSS5 initialization was unavailable and is recorded
as fallback, not successful DLSS5 coverage. Selected native-AA/default cases were rerun after
the temporal fix; normal-speed synchronized default and Unlocked functional tests also ran.
Both engines booted with isolated profiles and captured frames after the Static harness was
corrected to supply its Sys directory and omit Source-only flags. Early harness failures remain
in artifacts but are excluded from successful coverage.

Separate actual-YouGame Yoshi gameplay cases covered shipped defaults, Unlocked, VSync,
SSAA2/AF16 and active XeSS AA with 24 consecutive captures per case. These fast captures are
functional checks, not clean pacing benchmarks. The 12 paired broker tests produced 39,520
matching confirmed-state checks and 25,357 matching shared replay frames; their test conditions
and limitations are in the audit. Several runs shared this GPU, and capture stalls affect pacing,
so no absolute latency/performance conclusion is derived from those frame-time logs.

No physical scanout test on Hbox's display, controller-to-photon/CRT measurement, macOS runtime
test, every-model controller hardware certification, or release deployment was performed.
All fixes are local in the 0.9.64 hotfix tree; the downloaded YouGame build is unmodified.
