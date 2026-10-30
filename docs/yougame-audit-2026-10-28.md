# YouGame fork audit : 2026-10-28

## Scope and provenance

Audited the Windows staging app 0.13.1086 (build 39e255b7), its downloaded native build
880b2eeb-0d00-4a00-8000-0000000b001e, and the bundled source patch. The native program reports
Melee Unlocked 0.9.63. Its README explicitly identifies upstream tag v0.9.63 plus
`yougame-online.patch`; it is a rebuild with integration changes, not our unchanged release binary.
The installed older staging app was 0.12.48; the current staging installer was extracted into
an isolated test copy. No replacement of the user's installed app or account profile was needed.

The supplied patch applies to a clean v0.9.63 worktree. It affects **308 files: 182 added,
120 modified, six deleted**. Binary changes are included in those counts. This report describes
what the supplied source says; source provenance alone cannot prove every compiler option used
for the executable. Runtime tests establish individual enabled/disabled features.

Patch SHA-256: `f3b43007e2c3937854899d23ec58596a3d62c14fba9b9df47757c941012abf10`.

## Rollback, subframes, and early inputs

- `sourceport/game/shim/mu_online.c` is identical to upstream v0.9.63 **and current 0.9.64**.
  Its early predictions, confirmed-pad reconciliation, trust handling, snapshots and rollback
  scheduling were not replaced by this patch.
- `early_input_rules.h`, `early_input.h`, `authored_pose.cpp`, and `gx_shader.cpp` also match
  the current upstream code. The sampler's two source changes wrap diagnostic-only lead/lying
  overrides in `test_env`; the ordinary sampler and enabled-by-default preference remain.
- The early-input capability stays bit 0. Packet 0xE1 (`MUE`, version 1), peer gating, unsequenced
  ENet channel 1, receiver buffer and native reply entries remain. Ping capability stays bit 1.
  YouGame adds engine/ranked-rule capability bits without removing those original bits.
- The subframe solver adds a timing scope around one wait; no solver algorithm change.
  The presentation thread adds measurements/trace columns, retaining rollback discontinuity,
  authored fighter continuity and hard-cut behavior.
- The `mu_snapshot.c` difference between 0.9.63 and current 0.9.64 adds read-only low-detail
  item/projectile visuals. It is not a different gameplay rollback snapshot implementation.
- `rollback_perf.h` and `sim_work.h` measure timing. They do not implement a replacement
  gameplay rollback engine. No Brawlback rollback replacement was found in this source patch.
  That does not establish the provenance of code outside the supplied game patch.
- One substantive network fix preserves already-ACKed pre-start inputs when a late selection
  message arrives; it clears old confirmed/early pads only when leaving a started game.
  This differs from upstream but is a receive-ordering correction, not a rollback replacement.

The game has been exercised with two actual downloaded executables in YouGame stdio mode,
relayed synthetic controllers and injected delay. A second fixture exercises its real brokered
shared-memory ENet path, with a local simulation of the app/room service. No public players or
matchmaking services participate. See the machine-readable test results below; this is not a
claim of measured superiority over Slippi Dolphin or a live two-PC Internet certification.

## Behavior changes and their reasons

Reasons marked **stated** come from patch comments/docs; **inferred** are interpretations of code.

| Area | Exact resulting difference in a YouGame session | Reason/evidence |
|---|---|---|
| Account | Uses YouGame identity and code; skips Slippi user/account discovery and profile refresh. | Stated: account belongs to the app; no Slippi hosts contacted in YouGame mode. |
| Matchmaking | Existing online modes route to YouGame rooms/tickets instead of Slippi matchmaking. Standalone fallback remains when YouGame mode is absent. | Stated integration design. These are replacements within that session, not separate additional Slippi/YouGame choices. |
| Results/ranks | Slippi game reporting is skipped; YouGame rooms receive results and supply ladder ranks. | Stated: keep reporting in the backend that owns the match. |
| Ranked | Adds YouGame ranked best-of-three sets, rules digests, receipts/concessions and rank UI. Source uses Unranked scene flow with random legal stages rather than a full Slippi ranked stage-pick flow. | Stated in the game-side menu and ranked implementation. This is YouGame rank, not Slippi rank. |
| Friends/Teams | Adds invite-room handling and room-authoritative team assignment; Teams code searches are explicitly unavailable and require an empty code. | Stated routing/seat consistency. |
| Transport | Adds brokered room streams and 1200-byte datagram shared-memory rings/virtual ENet addresses. Non-broker internet mode retains STUN/punching. | Stated: app owns networking under sandbox/broker mode. Core gameplay packets continue through ENet. |
| Compatibility | Room key includes MU version, engine, mod fingerprint and optional ranked rules; full peer digests are checked. | Stated: isolate incompatible engines/content/rules. |
| NVIDIA | Actual executable reports built without Streamline SDK. DLAA/DLSS/Reflex/frame generation cannot activate, despite bundled NVIDIA DLLs. The patch preserves the implementation and CMake optional SDK detection. | Confirmed runtime/build mismatch. Missing or mislocated build-time SDK components is an inference; deliberate removal/motive is not established. DLL copying alone cannot restore compiled-out code. |
| XeSS | Actual Windows game reports XeSS 2.0.2 available. | Confirmed runtime. Availability is distinct from testing every XeSS mode. |
| VSync/FPS | Fork does not change the Windows Present synchronization/tearing expression or default VSync-off preference. Match monitor remains the normal cap default. | Source diff. Crunch reports the visual artifacts disappear with VSync on; matching FPS alone does not guarantee scanout synchronization. |
| Exact looks | Automatic startup activation of locally built Diet Classic, Diet 64 and Crystal artwork is skipped entirely in YouGame; renderer versions stand in. The setup UI remains, despite this startup restriction. Imported exact fighter tables/full look files are additionally forbidden for online reads. | Stated: packs recognized by name/size and no validated online-safe transition for their fighter data. Ordinary cosmetics are not all removed. |
| Costumes/stages | Adds draw-safety to costume online validation and repins a stage's content when online/offline state changes; fixes announcer SSM header handling. | Stated correctness and content consistency fixes. |
| Mods/Gecko | Disables saved gameplay profiles, implicit Mods/Files discovery and auxiliary folder loaders/Gecko-source editing in YouGame. Explicit listing-supplied layers remain available. | Stated: game/mod selection belongs to the listing, avoiding accidental local gameplay changes. Vanilla listing also passes --vanilla-only. |
| Mod saves | Normal progress saves remain; known mod saves require an explicit imported layer. | Stated: prevent an old mod save from silently entering a clean listing. |
| Settings/saves location | Launcher supplies per-listing data/settings/card/replay/log paths. Existing standalone settings are not automatically the same file. | Stated launch contract/isolation; settings migration must be verified separately. |
| Spectator | Slippi spectator listener and its toggle are disabled in YouGame. | Explicit mode policy; useful broadcasting integration is lost in that session. |
| Auto L-cancel | Remains available offline/Direct under ordinary policy, suppressed in other competitive modes; Ranked toggle is locked off. Vanilla-only also suppresses it. | Stated ranked rule enforcement. |
| GameCube adapter | Native direct USB/HID ownership and reset are disabled in stdio mode; app helper shares raw reports and accepts rumble through a mirror. | Stated: one adapter owner; the game sandbox cannot open USB directly. |
| Standard controllers | App relays normalized standard-layout pads; game reads them through XInput slots. Relay kind ps/switch does not select native PlayStation/Switch bindings or family options. | Confirmed code path; normalization/sandbox adaptation is the likely reason, but saved-family mapping parity is incomplete. |
| Generic HID/DInput | App relay filters out non-standard-mapping pads and recognized GC adapters. GC adapters have a separate mirror; an unrecognized non-standard HID needs a verified alternative path. | Confirmed filter; raw HID/HayBox-specific behavior cannot be certified from standard-pad tests. |
| Rumble | Relayed non-adapter pads explicitly have no rumble yet; native rumble callback is suppressed. GC mirror has a separate return path. | Explicit implementation/log message. |
| Bindings/profiles | Moves shared binding types/access helpers; expands saved profiles to all 27 actions and preserves missing modifier defaults for older profiles. | Refactor and compatibility fix. Saving a profile is separate from routing a relayed device into the correct family. |
| Overlay | F1 opens/backtracks the game/app menu; Escape relays to the page; embedding owns fullscreen/size. Mouse event trickling is enabled with duplicate platform gamepad polling disabled. | Stated: avoid swallowed press/release pairs and competing gamepad producers. |
| Crash/pause/volume | Replaces modal crash dialog with exit reason for app UI; page volume multiplies game volume; page pause applies offline only and never pauses an online match. | Stated embedded-player lifecycle controls. |
| Disc/Sys assets | Deletes six copies of two full Sys assets and adds two VCDIFF reconstructions from the player's retail disc. | Patch docs state a distribution restriction. Target SHA-1/length match upstream originals; this is not evidence the menus were stripped out. |
| Memory/card boot | Reserves native image address space earlier; changes memory-card confirmation to normal create/write with app-owned confirmation suppression. | Stated sandbox ASLR collision and boot-flow fixes. |
| Static mod dispatch | Corrects host-stub detection to exclude Gecko synthetic functions and inspect recompiled functions rather than installed wrappers. | Stated Beyond Melee crash fixes; not a new Source rollback engine. |
| Diagnostics/privacy | Adds timing/catch-up measurements, removes peer addresses from several logs and suppresses diagnostic environment hooks in app/vanilla sessions. | Explicit comments/code. |
| Staging automation | Adds compile-gated agent test hooks and private/agent-room input restrictions. The downloaded staging executable contains those hooks. | Confirmed strings plus exercised staging paths; presence in staging alone does not establish inclusion in production releases. |
| Linux/macOS | Adds X11/AppKit embedding, GL shader translation/work scheduling, compatibility/networking code, packaging and many tests. Mac full-host docs describe an x86-64 host/game ELF path through Rosetta on Apple silicon. | Substantial platform-port work. Translation/GL/compositing overhead are plausible performance contributors, not a measured diagnosis of Crunch's Mac. |

## Controller acceptance matrix

| Controller/mode | Retained path | Current acceptance evidence / limit |
|---|---|---|
| Official/Mayflash/HHL GC adapter | Raw-report app mirror, original neutral/trigger conversion and GC binding tables | Mirror ownership/read/staleness source inspected; physical USB/hotplug/rumble not certified on every model. |
| B0XX/Frame1 in GC mode | Same GC adapter path | No separate removal found; requires actual supported adapter/firmware validation. |
| Xbox/XInput, box in XInput mode | Normalized relay into existing XInput binding code | Synthetic relayed inputs exercised actual paired online games and early sampler. No native deadzone reintroduced in the inspected path. |
| PlayStation | Relay ps metadata, but read as XInput | Family-specific saved bindings/options are not automatically applied by the relayed path. Launch blocker for full settings parity. |
| Switch 1/2 Pro | Relay switch metadata, but read as XInput; original native parser still exists | Same family/profile issue. Neither model's physical hardware has been certified here. |
| Generic HID / DInput / HayBox DInput | Original raw parser exists; normalized relay accepts standard mapping only | Sandbox ownership and non-standard relay filtering require a tested raw-device path. No blanket support claim. |
| Keyboard / box keyboard mode | Embedded message keys plus optional app virtual-pad relay | Native mapping code remains; verify virtual-pad priority and custom bindings under each focus mode. |
| Profiles/modifiers | All 27 actions and six device families retained in helper tests | Persistence passes; persistence does not prove device-family routing parity. |
| Rumble | GC mirror return supported, relayed standard pad rumble suppressed | Standard-pad rumble is a confirmed missing feature. |

## Validation and remaining release evidence

21 focused C++ checks built against the applied fork passed, including the actual subframe solver's
rollback/hard-cut tests, early input rules, receive ordering, controller relay, box coordinates,
profile access, room/transport/ranked rules, and pause policies. One additional observation fixture
reproduces the PlayStation-to-XInput binding mismatch; its passing assertion confirms that
difference rather than certifying controller parity. Full-game paired results are
recorded separately with exact commands, logs and timing CSVs. Local fixtures cannot certify the
public service, Internet routes, every USB driver/firmware, or macOS performance.

The current code audit establishes that the recent early-input/subframe rollback work was not
replaced by a separate Brawlback engine. It does not establish that the whole YouGame integration
is identical or that every controller preference is honored. Those are separate acceptance checks.

## Runtime rollback acceptance results

The **actual downloaded staging executable and game DLL** ran 12 paired broker scenarios
(24 client runs), hidden and volume zero. The local fixture implements the app/room protocol,
relays ordinary controller state, and connects the production shared-memory ENet adapter.
It never queues with public players or contacts external matchmaking services.

- 39,520 confirmed-state checksum comparisons: **zero mismatches**.
- Paired replay post-frame comparison: **25,357 matching shared frames; zero differing frames**.
- Sustained run: over 3,500 game frames, with more than 3,100 checksum comparisons per peer.
- Delay coverage: 20, 40 and 80 ms added one-way; 25 ms plus 0-25 ms deterministic jitter;
  early inputs off/on; subframes off/on. These are injected network conditions on one PC,
  not claims about a particular Internet connection or physical input latency.
- The invalid early-input run changed 383 early wire packets. The receiver logged that it
  ignored early pads after 24 of 24 early guesses were wrong; the peers still agreed on
  their confirmed states and replay output. This tests the retained trust fallback.

Timed on/off comparisons used the same input generator, stage setup, added delay and game timer.
They are separate wall-clock-scheduled runs, so packet/input phase is not bit-identical between
runs; the percentages below demonstrate an improvement in these samples, not a benchmark claim
against Slippi Dolphin.

| Network fixture | Early inputs off: rollback events, peers combined | Early inputs on | Change |
|---|---:|---:|---:|
| 40 ms added each way, measured RTT about 87 ms | 410 | 354 | 13.7% fewer |
| 25 ms plus 0-25 ms added jitter each way, RTT about 82 ms | 296 | 185 | 37.5% fewer |

The on cases recorded 54 and 144 correct early predictions respectively, with **zero wrong**.
The off cases sent/received zero early packets. Both sides completed 1,333 game frames in each
timed case. The replay comparison uses final post-frame values after rollback overwrites.

An initial Python fixture published shared-ring counters with ordinary byte stores. Some runs
ended early. Those runs are **excluded** from these acceptance totals. After replacing those
stores/loads with Win32 interlocked operations, all accepted runs completed. This was a fixture
error, not evidence that the shipped broker has the same defect.

This establishes that your existing early inputs and subframe rollback paths are present and
functional through the shipped Windows YouGame game transport. Crunch does **not** need to
add them from scratch. It does not certify the actual desktop helper's every controller path,
a production room server, cross-platform play, long-distance routes, or superiority to Slippi.

Evidence: [rollback matrix](../run-current-reports/hbox-visuals/yougame/full-audit/rollback-matrix.json),
[paired replay comparisons](../run-current-reports/hbox-visuals/yougame/full-audit/paired-replay-results.json),
[background harness](../run-current-reports/hbox-visuals/yougame/early_pair.py),
[ring/room fixture](../run-current-reports/hbox-visuals/yougame/ring_fixture.py).

## Differences from current upstream 0.9.64

The original integration patch is relative to 0.9.63. Against current release commit
`1110a82194b745590ef62b81d42fb0e74bba2c55`, the fork differs at 328 paths. The exact
[current-release comparison](../run-current-reports/hbox-visuals/yougame/full-audit/current-release-to-yougame.patch)
includes every text and binary change, and its
[file totals](../run-current-reports/hbox-visuals/yougame/full-audit/current-release-to-yougame-numstat.tsv)
are separate from the 308-file integration inventory.

The [34 upstream paths changed since 0.9.63](../run-current-reports/hbox-visuals/yougame/full-audit/upstream-0963-to-0964-files.txt)
cover the 0.9.64 low-detail shield/projectile fixes, stage/custom-texture safety and graft handling,
CRT/GL/platform handling, observer/trace/crash-report details, associated tests and release metadata.
The fork lacks those later upstream changes unless it independently supplies an equivalent.
The native gameplay rollback file and early-input rules did not change between those releases;
being based on 0.9.63 does not make this fork's gameplay rollback older than current 0.9.64.
The local uncommitted Hbox fixes and stronger Slippi gate are additional to that release baseline.

## Yoshi and visual feedback

Crunch's later message says the artifacts disappear when he turns VSync back on. That is strong
support for unsynchronized scanout, not proof of an old Yoshi shader regression or a particular
monitor configuration. The Windows fork leaves the upstream Present synchronization/tearing
expression intact. No current Hbox monitor/driver configuration was established.

The actual downloaded YouGame executable reached Yoshi's Story gameplay in five hidden,
muted cases: Unlocked/VSync off, Match monitor/VSync off (shipped defaults), Unlocked/VSync on,
SSAA 2 plus AF16, and XeSS AA. Each produced 24 consecutive gameplay captures, and the XeSS
case logged actual activation. Earlier navigation-only attempts captured menus and are excluded.
These fast functional captures validate scene rendering, not physical screen tearing or pacing.
No obvious old Yoshi surface corruption appeared in the inspected captures. Physical scanout
tearing can be absent from backbuffer capture, so that absence does not contradict Crunch.

## Complete file inventory

Every supplied file change is indexed below, including binary deletions and additions. The linked
individual diffs preserve every hunk from the supplied patch; binary patches retain their exact
payload. Added/removed counts apply to text lines only. Explanations above group related behavior
without treating test files and platform ports as missing gameplay features.

| File | Change | Lines + / - | Exact diff |
|---|---|---:|---|
| `CMakeLists.txt` | modified | 326 / 70 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/001-CMakeLists.txt.diff>) |
| `port/CMakeLists.txt` | modified | 78 / 11 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/002-port_CMakeLists.txt.diff>) |
| `port/app/main.cpp` | modified | 211 / 24 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/003-port_app_main.cpp.diff>) |
| `port/app/settings_window.cpp` | modified | 3 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/004-port_app_settings_window.cpp.diff>) |
| `port/app/source_host.cpp` | modified | 509 / 73 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/005-port_app_source_host.cpp.diff>) |
| `port/app/source_host.h` | modified | 4 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/006-port_app_source_host.h.diff>) |
| `port/linux/compat/bcrypt.h` | modified | 4 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/007-port_linux_compat_bcrypt.h.diff>) |
| `port/linux/compat/compat.cpp` | modified | 183 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/008-port_linux_compat_compat.cpp.diff>) |
| `port/linux/compat/compat_more.inl` | modified | 22 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/009-port_linux_compat_compat_more.inl.diff>) |
| `port/linux/compat/tests/ws_frame_test.cpp` | added | 147 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/010-port_linux_compat_tests_ws_frame_test.cpp.diff>) |
| `port/linux/compat/windows.h` | modified | 3 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/011-port_linux_compat_windows.h.diff>) |
| `port/linux/compat/windows_more2.h` | modified | 16 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/012-port_linux_compat_windows_more2.h.diff>) |
| `port/linux/compat/winhttp.h` | modified | 16 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/013-port_linux_compat_winhttp.h.diff>) |
| `port/linux/compat/winhttp_ws.cpp` | added | 255 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/014-port_linux_compat_winhttp_ws.cpp.diff>) |
| `port/linux/compat/winhttp_ws_internal.h` | added | 10 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/015-port_linux_compat_winhttp_ws_internal.h.diff>) |
| `port/linux/compat/winhttp_ws_protocol.h` | added | 130 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/016-port_linux_compat_winhttp_ws_protocol.h.diff>) |
| `port/linux/compat/winsock2.h` | modified | 16 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/017-port_linux_compat_winsock2.h.diff>) |
| `port/linux/embed/README.md` | added | 176 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/018-port_linux_embed_README.md.diff>) |
| `port/linux/embed/yougame_embed_x11.c` | added | 353 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/019-port_linux_embed_yougame_embed_x11.c.diff>) |
| `port/linux/embed/yougame_embed_x11.h` | added | 33 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/020-port_linux_embed_yougame_embed_x11.h.diff>) |
| `port/linux/embed_visibility.h` | added | 18 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/021-port_linux_embed_visibility.h.diff>) |
| `port/linux/full/CMakeLists.txt` | modified | 40 / 5 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/022-port_linux_full_CMakeLists.txt.diff>) |
| `port/linux/full/audio_sdl.cpp` | modified | 7 / 3 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/023-port_linux_full_audio_sdl.cpp.diff>) |
| `port/linux/full/crash_linux.inl` | modified | 19 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/024-port_linux_full_crash_linux.inl.diff>) |
| `port/linux/full/gx_backend_linux.cpp` | modified | 21 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/025-port_linux_full_gx_backend_linux.cpp.diff>) |
| `port/linux/full/linux_media.cpp` | modified | 22 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/026-port_linux_full_linux_media.cpp.diff>) |
| `port/linux/full/pc_settings_gl.cpp` | modified | 13 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/027-port_linux_full_pc_settings_gl.cpp.diff>) |
| `port/linux/full/settings_window_linux.cpp` | modified | 8 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/028-port_linux_full_settings_window_linux.cpp.diff>) |
| `port/linux/gx_host_compat.cpp` | modified | 1 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/029-port_linux_gx_host_compat.cpp.diff>) |
| `port/linux/hid_linux.cpp` | modified | 15 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/030-port_linux_hid_linux.cpp.diff>) |
| `port/linux/host_services.cpp` | modified | 88 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/031-port_linux_host_services.cpp.diff>) |
| `port/linux/launcher/CMakeLists.txt` | modified | 2 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/032-port_linux_launcher_CMakeLists.txt.diff>) |
| `port/linux/renderer_gl.cpp` | modified | 717 / 35 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/033-port_linux_renderer_gl.cpp.diff>) |
| `port/linux/renderer_gl.h` | modified | 7 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/034-port_linux_renderer_gl.h.diff>) |
| `port/linux/shader_translation.h` | added | 220 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/035-port_linux_shader_translation.h.diff>) |
| `port/linux/window_sdl.inl` | modified | 393 / 7 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/036-port_linux_window_sdl.inl.diff>) |
| `port/linux/xinput_sdl.cpp` | modified | 9 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/037-port_linux_xinput_sdl.cpp.diff>) |
| `port/mac/D0016.md` | added | 58 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/038-port_mac_D0016.md.diff>) |
| `port/mac/M7-0956.md` | added | 104 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/039-port_mac_M7-0956.md.diff>) |
| `port/mac/README.md` | added | 573 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/040-port_mac_README.md.diff>) |
| `port/mac/bcrypt_apple.inl` | added | 64 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/041-port_mac_bcrypt_apple.inl.diff>) |
| `port/mac/bridge_test.cpp` | added | 11 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/042-port_mac_bridge_test.cpp.diff>) |
| `port/mac/build-app.sh` | added | 98 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/043-port_mac_build-app.sh.diff>) |
| `port/mac/build-full.sh` | added | 5 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/044-port_mac_build-full.sh.diff>) |
| `port/mac/build.sh` | added | 17 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/045-port_mac_build.sh.diff>) |
| `port/mac/build_full.py` | added | 144 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/046-port_mac_build_full.py.diff>) |
| `port/mac/check_embed.py` | added | 86 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/047-port_mac_check_embed.py.diff>) |
| `port/mac/check_full.py` | added | 337 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/048-port_mac_check_full.py.diff>) |
| `port/mac/compare_digest.py` | added | 64 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/049-port_mac_compare_digest.py.diff>) |
| `port/mac/compare_digest_test.py` | added | 62 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/050-port_mac_compare_digest_test.py.diff>) |
| `port/mac/compat_test.cpp` | added | 43 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/051-port_mac_compat_test.cpp.diff>) |
| `port/mac/context_registers.h` | added | 15 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/052-port_mac_context_registers.h.diff>) |
| `port/mac/convert_digest.py` | added | 24 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/053-port_mac_convert_digest.py.diff>) |
| `port/mac/cpu_check.h` | added | 23 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/054-port_mac_cpu_check.h.diff>) |
| `port/mac/elf_loader.cpp` | added | 301 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/055-port_mac_elf_loader.cpp.diff>) |
| `port/mac/elf_loader.h` | added | 29 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/056-port_mac_elf_loader.h.diff>) |
| `port/mac/elf_types.h` | added | 29 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/057-port_mac_elf_types.h.diff>) |
| `port/mac/embed_mac.h` | added | 13 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/058-port_mac_embed_mac.h.diff>) |
| `port/mac/embed_mac.mm` | added | 353 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/059-port_mac_embed_mac.mm.diff>) |
| `port/mac/embed_mac_state.h` | added | 180 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/060-port_mac_embed_mac_state.h.diff>) |
| `port/mac/embed_mac_test.cpp` | added | 155 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/061-port_mac_embed_mac_test.cpp.diff>) |
| `port/mac/embed_test_parent.mm` | added | 125 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/062-port_mac_embed_test_parent.mm.diff>) |
| `port/mac/full_launch.h` | added | 50 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/063-port_mac_full_launch.h.diff>) |
| `port/mac/mac_gl.mm` | added | 37 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/064-port_mac_mac_gl.mm.diff>) |
| `port/mac/main.cpp` | added | 281 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/065-port_mac_main.cpp.diff>) |
| `port/mac/memory_gate.py` | added | 7 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/066-port_mac_memory_gate.py.diff>) |
| `port/mac/package_full.py` | added | 118 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/067-port_mac_package_full.py.diff>) |
| `port/mac/package_full_test.py` | added | 52 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/068-port_mac_package_full_test.py.diff>) |
| `port/mac/platform_paths.h` | added | 20 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/069-port_mac_platform_paths.h.diff>) |
| `port/mac/run.sh` | added | 26 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/070-port_mac_run.sh.diff>) |
| `port/mac/seed-merge.py` | added | 79 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/071-port_mac_seed-merge.py.diff>) |
| `port/mac/seed_build.cpp` | added | 59 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/072-port_mac_seed_build.cpp.diff>) |
| `port/mac/translation_check.cpp` | added | 75 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/073-port_mac_translation_check.cpp.diff>) |
| `port/runtime/abi/mu_host.h` | modified | 9 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/074-port_runtime_abi_mu_host.h.diff>) |
| `port/runtime/gx/frame_queue.h` | modified | 5 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/075-port_runtime_gx_frame_queue.h.diff>) |
| `port/runtime/gx/gx_core.cpp` | modified | 12 / 4 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/076-port_runtime_gx_gx_core.cpp.diff>) |
| `port/runtime/gx/gx_core.h` | modified | 2 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/077-port_runtime_gx_gx_core.h.diff>) |
| `port/runtime/gx/gx_d3d11.cpp` | modified | 24 / 3 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/078-port_runtime_gx_gx_d3d11.cpp.diff>) |
| `port/runtime/gx/gx_d3d12.cpp` | modified | 32 / 9 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/079-port_runtime_gx_gx_d3d12.cpp.diff>) |
| `port/runtime/gx/gx_streamline.cpp` | modified | 6 / 3 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/080-port_runtime_gx_gx_streamline.cpp.diff>) |
| `port/runtime/gx/lab_view.cpp` | modified | 2 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/081-port_runtime_gx_lab_view.cpp.diff>) |
| `port/runtime/gx/net_overlay.cpp` | modified | 2 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/082-port_runtime_gx_net_overlay.cpp.diff>) |
| `port/runtime/gx/pc_settings.cpp` | modified | 209 / 103 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/083-port_runtime_gx_pc_settings.cpp.diff>) |
| `port/runtime/gx/pc_settings_shared.h` | modified | 1 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/084-port_runtime_gx_pc_settings_shared.h.diff>) |
| `port/runtime/gx/pc_settings_visuals.inl` | modified | 2 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/085-port_runtime_gx_pc_settings_visuals.inl.diff>) |
| `port/runtime/gx/settings_imgui_input.h` | added | 14 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/086-port_runtime_gx_settings_imgui_input.h.diff>) |
| `port/runtime/gx/settings_profile_bindings.h` | added | 54 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/087-port_runtime_gx_settings_profile_bindings.h.diff>) |
| `port/runtime/gx/subframe.cpp` | modified | 3 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/088-port_runtime_gx_subframe.cpp.diff>) |
| `port/runtime/gx/texture_pack.cpp` | modified | 6 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/089-port_runtime_gx_texture_pack.cpp.diff>) |
| `port/runtime/gx/threaded_backend.cpp` | modified | 16 / 3 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/090-port_runtime_gx_threaded_backend.cpp.diff>) |
| `port/runtime/gx/user_shader.cpp` | modified | 17 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/091-port_runtime_gx_user_shader.cpp.diff>) |
| `port/runtime/gx/video_background.cpp` | modified | 3 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/092-port_runtime_gx_video_background.cpp.diff>) |
| `port/runtime/hle/ax_ucode.cpp` | modified | 7 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/093-port_runtime_hle_ax_ucode.cpp.diff>) |
| `port/runtime/hle/exi_slippi.cpp` | modified | 49 / 3 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/094-port_runtime_hle_exi_slippi.cpp.diff>) |
| `port/runtime/hle/hle_card.cpp` | modified | 2 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/095-port_runtime_hle_hle_card.cpp.diff>) |
| `port/runtime/hle/hle_card_filter.h` | added | 10 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/096-port_runtime_hle_hle_card_filter.h.diff>) |
| `port/runtime/hle/hle_dvd.cpp` | modified | 5 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/097-port_runtime_hle_hle_dvd.cpp.diff>) |
| `port/runtime/hle/hle_os.cpp` | modified | 2 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/098-port_runtime_hle_hle_os.cpp.diff>) |
| `port/runtime/hle/hle_pad.cpp` | modified | 26 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/099-port_runtime_hle_hle_pad.cpp.diff>) |
| `port/runtime/hle/jukebox.cpp` | modified | 14 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/100-port_runtime_hle_jukebox.cpp.diff>) |
| `port/runtime/hle/native_online_policy.h` | modified | 53 / 5 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/101-port_runtime_hle_native_online_policy.h.diff>) |
| `port/runtime/hle/native_practice.cpp` | modified | 132 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/102-port_runtime_hle_native_practice.cpp.diff>) |
| `port/runtime/hle/native_practice.h` | modified | 1 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/103-port_runtime_hle_native_practice.h.diff>) |
| `port/runtime/hle/native_slippi_bridge.h` | modified | 4 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/104-port_runtime_hle_native_slippi_bridge.h.diff>) |
| `port/runtime/hle/online_test_quit.h` | added | 31 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/105-port_runtime_hle_online_test_quit.h.diff>) |
| `port/runtime/hle/slippi_game_prep.h` | added | 37 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/106-port_runtime_hle_slippi_game_prep.h.diff>) |
| `port/runtime/hle/slippi_net.cpp` | modified | 227 / 36 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/107-port_runtime_hle_slippi_net.cpp.diff>) |
| `port/runtime/hle/slippi_net.h` | modified | 32 / 8 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/108-port_runtime_hle_slippi_net.h.diff>) |
| `port/runtime/hle/slippi_netplay_receive.h` | added | 38 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/109-port_runtime_hle_slippi_netplay_receive.h.diff>) |
| `port/runtime/hle/slippi_online.cpp` | modified | 293 / 45 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/110-port_runtime_hle_slippi_online.cpp.diff>) |
| `port/runtime/hle/slippi_online.h` | modified | 19 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/111-port_runtime_hle_slippi_online.h.diff>) |
| `port/runtime/hle/slippi_playback.cpp` | modified | 9 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/112-port_runtime_hle_slippi_playback.cpp.diff>) |
| `port/runtime/hle/slippi_spectate.cpp` | modified | 6 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/113-port_runtime_hle_slippi_spectate.cpp.diff>) |
| `port/runtime/hle/yougame_agent_fight.h` | added | 142 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/114-port_runtime_hle_yougame_agent_fight.h.diff>) |
| `port/runtime/hle/yougame_agent_input.h` | added | 84 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/115-port_runtime_hle_yougame_agent_input.h.diff>) |
| `port/runtime/hle/yougame_agent_keys.h` | added | 129 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/116-port_runtime_hle_yougame_agent_keys.h.diff>) |
| `port/runtime/hle/yougame_agent_pad.h` | added | 108 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/117-port_runtime_hle_yougame_agent_pad.h.diff>) |
| `port/runtime/hle/yougame_agent_paths.h` | added | 425 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/118-port_runtime_hle_yougame_agent_paths.h.diff>) |
| `port/runtime/hle/yougame_agent_present.h` | added | 235 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/119-port_runtime_hle_yougame_agent_present.h.diff>) |
| `port/runtime/hle/yougame_agent_script.h` | added | 16 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/120-port_runtime_hle_yougame_agent_script.h.diff>) |
| `port/runtime/hle/yougame_agent_static.h` | added | 98 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/121-port_runtime_hle_yougame_agent_static.h.diff>) |
| `port/runtime/hle/yougame_agent_test.h` | added | 500 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/122-port_runtime_hle_yougame_agent_test.h.diff>) |
| `port/runtime/hle/yougame_compatibility.h` | added | 27 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/123-port_runtime_hle_yougame_compatibility.h.diff>) |
| `port/runtime/hle/yougame_embed_view.h` | added | 85 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/124-port_runtime_hle_yougame_embed_view.h.diff>) |
| `port/runtime/hle/yougame_embed_volume.h` | added | 30 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/125-port_runtime_hle_yougame_embed_volume.h.diff>) |
| `port/runtime/hle/yougame_exit_reason.h` | added | 47 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/126-port_runtime_hle_yougame_exit_reason.h.diff>) |
| `port/runtime/hle/yougame_friends.h` | added | 165 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/127-port_runtime_hle_yougame_friends.h.diff>) |
| `port/runtime/hle/yougame_link.cpp` | added | 1539 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/128-port_runtime_hle_yougame_link.cpp.diff>) |
| `port/runtime/hle/yougame_link.h` | added | 179 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/129-port_runtime_hle_yougame_link.h.diff>) |
| `port/runtime/hle/yougame_memcard.cpp` | added | 49 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/130-port_runtime_hle_yougame_memcard.cpp.diff>) |
| `port/runtime/hle/yougame_memcard.h` | added | 19 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/131-port_runtime_hle_yougame_memcard.h.diff>) |
| `port/runtime/hle/yougame_mode_policy.h` | added | 61 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/132-port_runtime_hle_yougame_mode_policy.h.diff>) |
| `port/runtime/hle/yougame_net_test.h` | added | 75 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/133-port_runtime_hle_yougame_net_test.h.diff>) |
| `port/runtime/hle/yougame_pad_mirror.h` | added | 32 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/134-port_runtime_hle_yougame_pad_mirror.h.diff>) |
| `port/runtime/hle/yougame_parity.h` | added | 162 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/135-port_runtime_hle_yougame_parity.h.diff>) |
| `port/runtime/hle/yougame_ranked.h` | added | 542 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/136-port_runtime_hle_yougame_ranked.h.diff>) |
| `port/runtime/hle/yougame_ranked_rules.h` | added | 88 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/137-port_runtime_hle_yougame_ranked_rules.h.diff>) |
| `port/runtime/hle/yougame_relay_pads.h` | added | 206 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/138-port_runtime_hle_yougame_relay_pads.h.diff>) |
| `port/runtime/hle/yougame_room.h` | added | 298 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/139-port_runtime_hle_yougame_room.h.diff>) |
| `port/runtime/hle/yougame_transport.h` | added | 422 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/140-port_runtime_hle_yougame_transport.h.diff>) |
| `port/runtime/hle/yougame_ux.h` | added | 75 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/141-port_runtime_hle_yougame_ux.h.diff>) |
| `port/runtime/host/audio.cpp` | modified | 10 / 5 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/142-port_runtime_host_audio.cpp.diff>) |
| `port/runtime/host/audio.h` | modified | 3 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/143-port_runtime_host_audio.h.diff>) |
| `port/runtime/host/controller_profiles.cpp` | modified | 11 / 7 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/144-port_runtime_host_controller_profiles.cpp.diff>) |
| `port/runtime/host/controller_profiles.h` | modified | 7 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/145-port_runtime_host_controller_profiles.h.diff>) |
| `port/runtime/host/cosmetic_mods.cpp` | modified | 64 / 21 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/146-port_runtime_host_cosmetic_mods.cpp.diff>) |
| `port/runtime/host/cosmetic_mods.h` | modified | 21 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/147-port_runtime_host_cosmetic_mods.h.diff>) |
| `port/runtime/host/dolphin_profile.cpp` | modified | 4 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/148-port_runtime_host_dolphin_profile.cpp.diff>) |
| `port/runtime/host/early_input.cpp` | modified | 3 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/149-port_runtime_host_early_input.cpp.diff>) |
| `port/runtime/host/exact_look_policy.h` | added | 14 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/150-port_runtime_host_exact_look_policy.h.diff>) |
| `port/runtime/host/gc_adapter.cpp` | modified | 165 / 13 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/151-port_runtime_host_gc_adapter.cpp.diff>) |
| `port/runtime/host/gc_adapter_mirror_mac.h` | added | 65 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/152-port_runtime_host_gc_adapter_mirror_mac.h.diff>) |
| `port/runtime/host/gc_content_sha1.h` | added | 91 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/153-port_runtime_host_gc_content_sha1.h.diff>) |
| `port/runtime/host/hackpack_ai.cpp` | modified | 16 / 4 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/154-port_runtime_host_hackpack_ai.cpp.diff>) |
| `port/runtime/host/host.cpp` | modified | 102 / 34 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/155-port_runtime_host_host.cpp.diff>) |
| `port/runtime/host/host.h` | modified | 9 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/156-port_runtime_host_host.h.diff>) |
| `port/runtime/host/input_binding_types.h` | added | 51 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/157-port_runtime_host_input_binding_types.h.diff>) |
| `port/runtime/host/input_bindings.h` | modified | 1 / 42 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/158-port_runtime_host_input_bindings.h.diff>) |
| `port/runtime/host/layer_fingerprint.h` | added | 30 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/159-port_runtime_host_layer_fingerprint.h.diff>) |
| `port/runtime/host/lcancel.cpp` | modified | 10 / 11 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/160-port_runtime_host_lcancel.cpp.diff>) |
| `port/runtime/host/lcancel.h` | modified | 7 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/161-port_runtime_host_lcancel.h.diff>) |
| `port/runtime/host/lcancel_policy.h` | added | 32 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/162-port_runtime_host_lcancel_policy.h.diff>) |
| `port/runtime/host/mod_scan.cpp` | modified | 55 / 3 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/163-port_runtime_host_mod_scan.cpp.diff>) |
| `port/runtime/host/mod_scan.h` | modified | 7 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/164-port_runtime_host_mod_scan.h.diff>) |
| `port/runtime/host/net_trace_file.cpp` | modified | 29 / 3 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/165-port_runtime_host_net_trace_file.cpp.diff>) |
| `port/runtime/host/netplay_state.h` | modified | 1 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/166-port_runtime_host_netplay_state.h.diff>) |
| `port/runtime/host/pad_mirror_posix.h` | added | 143 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/167-port_runtime_host_pad_mirror_posix.h.diff>) |
| `port/runtime/host/replay_control.cpp` | modified | 5 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/168-port_runtime_host_replay_control.cpp.diff>) |
| `port/runtime/host/rollback_perf.h` | added | 145 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/169-port_runtime_host_rollback_perf.h.diff>) |
| `port/runtime/host/sim_work.h` | added | 107 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/170-port_runtime_host_sim_work.h.diff>) |
| `port/runtime/host/source_mod_overlay.cpp` | modified | 4 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/171-port_runtime_host_source_mod_overlay.cpp.diff>) |
| `port/runtime/host/source_mod_overlay.h` | modified | 2 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/172-port_runtime_host_source_mod_overlay.h.diff>) |
| `port/runtime/host/stage_read_pin.h` | added | 26 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/173-port_runtime_host_stage_read_pin.h.diff>) |
| `port/runtime/host/switch_pro.cpp` | modified | 6 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/174-port_runtime_host_switch_pro.cpp.diff>) |
| `port/runtime/host/sysgen.cpp` | added | 312 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/175-port_runtime_host_sysgen.cpp.diff>) |
| `port/runtime/host/sysgen.h` | added | 39 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/176-port_runtime_host_sysgen.h.diff>) |
| `port/runtime/host/sysgen_boot.cpp` | added | 52 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/177-port_runtime_host_sysgen_boot.cpp.diff>) |
| `port/runtime/host/sysgen_boot.h` | added | 11 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/178-port_runtime_host_sysgen_boot.h.diff>) |
| `port/runtime/host/sysgen_paths.h` | added | 120 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/179-port_runtime_host_sysgen_paths.h.diff>) |
| `port/runtime/host/test_env.h` | added | 12 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/180-port_runtime_host_test_env.h.diff>) |
| `port/runtime/host/user_gecko.cpp` | modified | 6 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/181-port_runtime_host_user_gecko.cpp.diff>) |
| `port/runtime/host/user_gecko.h` | modified | 2 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/182-port_runtime_host_user_gecko.h.diff>) |
| `port/runtime/host/window.cpp` | modified | 314 / 28 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/183-port_runtime_host_window.cpp.diff>) |
| `port/runtime/host/window.h` | modified | 19 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/184-port_runtime_host_window.h.diff>) |
| `port/runtime/ppc/host_stub_policy.h` | added | 36 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/185-port_runtime_ppc_host_stub_policy.h.diff>) |
| `port/runtime/ppc/ppc_runtime.cpp` | modified | 27 / 4 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/186-port_runtime_ppc_ppc_runtime.cpp.diff>) |
| `port/slippi_sys/GameFiles/GALE01/GameSetup_gui.dat` | deleted | binary 1031905 → 0 bytes | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/187-port_slippi_sys_GameFiles_GALE01_GameSetup_gui.dat.diff>) |
| `port/slippi_sys/GameFiles/GALE01/slpCSS.dat` | deleted | binary 254495 → 0 bytes | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/188-port_slippi_sys_GameFiles_GALE01_slpCSS.dat.diff>) |
| `port/slippi_sys_general/GameFiles/GALE01/GameSetup_gui.dat` | deleted | binary 1031905 → 0 bytes | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/189-port_slippi_sys_general_GameFiles_GALE01_GameSetup_gui.dat.diff>) |
| `port/slippi_sys_general/GameFiles/GALE01/slpCSS.dat` | deleted | binary 254495 → 0 bytes | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/190-port_slippi_sys_general_GameFiles_GALE01_slpCSS.dat.diff>) |
| `port/slippi_sys_playback/GameFiles/GALE01/GameSetup_gui.dat` | deleted | binary 1031905 → 0 bytes | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/191-port_slippi_sys_playback_GameFiles_GALE01_GameSetup_gui.dat.diff>) |
| `port/slippi_sys_playback/GameFiles/GALE01/slpCSS.dat` | deleted | binary 254495 → 0 bytes | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/192-port_slippi_sys_playback_GameFiles_GALE01_slpCSS.dat.diff>) |
| `port/sys_deltas/GameSetup_gui.dat.vcdiff` | added | binary 0 → 189688 bytes | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/193-port_sys_deltas_GameSetup_gui.dat.vcdiff.diff>) |
| `port/sys_deltas/README.md` | added | 59 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/194-port_sys_deltas_README.md.diff>) |
| `port/sys_deltas/check_sys_deltas.py` | added | 47 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/195-port_sys_deltas_check_sys_deltas.py.diff>) |
| `port/sys_deltas/make_sys_deltas.py` | added | 168 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/196-port_sys_deltas_make_sys_deltas.py.diff>) |
| `port/sys_deltas/manifest.json` | added | 194 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/197-port_sys_deltas_manifest.json.diff>) |
| `port/sys_deltas/slpCSS.dat.vcdiff` | added | binary 0 → 57663 bytes | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/198-port_sys_deltas_slpCSS.dat.vcdiff.diff>) |
| `port/sys_deltas/sys_delta_common.py` | added | 361 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/199-port_sys_deltas_sys_delta_common.py.diff>) |
| `port/sys_deltas/test_sys_deltas.py` | added | 308 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/200-port_sys_deltas_test_sys_deltas.py.diff>) |
| `port/tests/agent_script_absent_test.sh` | added | 25 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/201-port_tests_agent_script_absent_test.sh.diff>) |
| `port/tests/agent_test_hook_absent.sh` | added | 40 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/202-port_tests_agent_test_hook_absent.sh.diff>) |
| `port/tests/agent_test_hook_absent_test.sh` | added | 42 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/203-port_tests_agent_test_hook_absent_test.sh.diff>) |
| `port/tests/agent_test_static_absent_test.sh` | added | 25 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/204-port_tests_agent_test_static_absent_test.sh.diff>) |
| `port/tests/akaneia_probe_wiring_test.py` | added | 31 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/205-port_tests_akaneia_probe_wiring_test.py.diff>) |
| `port/tests/controller_profiles.cmake` | added | 27 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/206-port_tests_controller_profiles.cmake.diff>) |
| `port/tests/controller_profiles_test.cpp` | modified | 41 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/207-port_tests_controller_profiles_test.cpp.diff>) |
| `port/tests/cosmetic_cli_test.cpp` | added | 35 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/208-port_tests_cosmetic_cli_test.cpp.diff>) |
| `port/tests/cosmetic_mods_test.cpp` | modified | 40 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/209-port_tests_cosmetic_mods_test.cpp.diff>) |
| `port/tests/dolphin_profile_test.cpp` | modified | 3 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/210-port_tests_dolphin_profile_test.cpp.diff>) |
| `port/tests/embed_view.cmake` | added | 13 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/211-port_tests_embed_view.cmake.diff>) |
| `port/tests/embed_view_wiring_test.py` | added | 79 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/212-port_tests_embed_view_wiring_test.py.diff>) |
| `port/tests/exact_look_policy_test.cpp` | added | 29 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/213-port_tests_exact_look_policy_test.cpp.diff>) |
| `port/tests/frame_queue_test.cpp` | modified | 11 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/214-port_tests_frame_queue_test.cpp.diff>) |
| `port/tests/gc_adapter_guard_test.py` | added | 91 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/215-port_tests_gc_adapter_guard_test.py.diff>) |
| `port/tests/gc_adapter_reset_test.cpp` | modified | 50 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/216-port_tests_gc_adapter_reset_test.cpp.diff>) |
| `port/tests/gc_content_sha1_test.cpp` | added | 71 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/217-port_tests_gc_content_sha1_test.cpp.diff>) |
| `port/tests/host_stub_policy_test.cpp` | added | 47 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/218-port_tests_host_stub_policy_test.cpp.diff>) |
| `port/tests/identity_stubs/bcrypt.h` | added | 34 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/219-port_tests_identity_stubs_bcrypt.h.diff>) |
| `port/tests/identity_stubs/commdlg.h` | added | 10 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/220-port_tests_identity_stubs_commdlg.h.diff>) |
| `port/tests/identity_stubs/hash_apple.cpp` | added | 5 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/221-port_tests_identity_stubs_hash_apple.cpp.diff>) |
| `port/tests/identity_stubs/shellapi.h` | added | 2 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/222-port_tests_identity_stubs_shellapi.h.diff>) |
| `port/tests/identity_stubs/windows.h` | added | 35 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/223-port_tests_identity_stubs_windows.h.diff>) |
| `port/tests/lcancel_policy_test.cpp` | added | 40 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/224-port_tests_lcancel_policy_test.cpp.diff>) |
| `port/tests/local_build_engine_wiring_test.py` | added | 52 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/225-port_tests_local_build_engine_wiring_test.py.diff>) |
| `port/tests/memcard_stubs/host.h` | added | 16 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/226-port_tests_memcard_stubs_host.h.diff>) |
| `port/tests/native_online_policy_test.cpp` | modified | 11 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/227-port_tests_native_online_policy_test.cpp.diff>) |
| `port/tests/pad_mirror_posix_test.cpp` | added | 208 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/228-port_tests_pad_mirror_posix_test.cpp.diff>) |
| `port/tests/production_env_wiring_test.py` | added | 34 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/229-port_tests_production_env_wiring_test.py.diff>) |
| `port/tests/production_integrity.cmake` | added | 50 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/230-port_tests_production_integrity.cmake.diff>) |
| `port/tests/production_integrity_test.cpp` | added | 247 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/231-port_tests_production_integrity_test.cpp.diff>) |
| `port/tests/relay_pads_wiring_test.py` | added | 97 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/232-port_tests_relay_pads_wiring_test.py.diff>) |
| `port/tests/rollback_perf_test.cpp` | added | 80 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/233-port_tests_rollback_perf_test.cpp.diff>) |
| `port/tests/settings_mouse.cmake` | added | 11 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/234-port_tests_settings_mouse.cmake.diff>) |
| `port/tests/settings_mouse_test.cpp` | added | 149 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/235-port_tests_settings_mouse_test.cpp.diff>) |
| `port/tests/settings_profile_bindings_test.cpp` | added | 53 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/236-port_tests_settings_profile_bindings_test.cpp.diff>) |
| `port/tests/sim_work_test.cpp` | added | 113 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/237-port_tests_sim_work_test.cpp.diff>) |
| `port/tests/slippi_game_prep_test.cpp` | added | 40 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/238-port_tests_slippi_game_prep_test.cpp.diff>) |
| `port/tests/slippi_netplay_receive_test.cpp` | added | 136 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/239-port_tests_slippi_netplay_receive_test.cpp.diff>) |
| `port/tests/source_mod_view_test.cpp` | added | 65 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/240-port_tests_source_mod_view_test.cpp.diff>) |
| `port/tests/stage_read_pin_test.cpp` | added | 62 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/241-port_tests_stage_read_pin_test.cpp.diff>) |
| `port/tests/static_mod_online_test.cpp` | modified | 3 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/242-port_tests_static_mod_online_test.cpp.diff>) |
| `port/tests/sysgen.cmake` | added | 14 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/243-port_tests_sysgen.cmake.diff>) |
| `port/tests/yg_enet_posix_ring_test.c` | added | 252 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/244-port_tests_yg_enet_posix_ring_test.c.diff>) |
| `port/tests/yg_enet_socket_test.c` | added | 190 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/245-port_tests_yg_enet_socket_test.c.diff>) |
| `port/tests/yougame_0956_merge_test.py` | added | 59 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/246-port_tests_yougame_0956_merge_test.py.diff>) |
| `port/tests/yougame_0962_merge_test.py` | added | 191 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/247-port_tests_yougame_0962_merge_test.py.diff>) |
| `port/tests/yougame_0963_merge_test.py` | added | 84 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/248-port_tests_yougame_0963_merge_test.py.diff>) |
| `port/tests/yougame_agent_fight_test.cpp` | added | 213 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/249-port_tests_yougame_agent_fight_test.cpp.diff>) |
| `port/tests/yougame_agent_keys_test.cpp` | added | 210 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/250-port_tests_yougame_agent_keys_test.cpp.diff>) |
| `port/tests/yougame_agent_pad_test.cpp` | added | 81 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/251-port_tests_yougame_agent_pad_test.cpp.diff>) |
| `port/tests/yougame_agent_pad_ui_test.cpp` | added | 171 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/252-port_tests_yougame_agent_pad_ui_test.cpp.diff>) |
| `port/tests/yougame_agent_present_test.cpp` | added | 153 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/253-port_tests_yougame_agent_present_test.cpp.diff>) |
| `port/tests/yougame_agent_root_win.cmake` | added | 14 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/254-port_tests_yougame_agent_root_win.cmake.diff>) |
| `port/tests/yougame_agent_root_win_test.cpp` | added | 173 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/255-port_tests_yougame_agent_root_win_test.cpp.diff>) |
| `port/tests/yougame_agent_script_test.cpp` | added | 414 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/256-port_tests_yougame_agent_script_test.cpp.diff>) |
| `port/tests/yougame_agent_static_test.cpp` | added | 244 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/257-port_tests_yougame_agent_static_test.cpp.diff>) |
| `port/tests/yougame_agent_test.cpp` | added | 206 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/258-port_tests_yougame_agent_test.cpp.diff>) |
| `port/tests/yougame_direct_test.sh` | added | 58 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/259-port_tests_yougame_direct_test.sh.diff>) |
| `port/tests/yougame_drop_room_test.cpp` | added | 308 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/260-port_tests_yougame_drop_room_test.cpp.diff>) |
| `port/tests/yougame_duel_mapping_test.cpp` | added | 172 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/261-port_tests_yougame_duel_mapping_test.cpp.diff>) |
| `port/tests/yougame_embed_view_test.cpp` | added | 94 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/262-port_tests_yougame_embed_view_test.cpp.diff>) |
| `port/tests/yougame_embed_volume_test.cpp` | added | 33 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/263-port_tests_yougame_embed_volume_test.cpp.diff>) |
| `port/tests/yougame_exit_reason_test.cpp` | added | 131 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/264-port_tests_yougame_exit_reason_test.cpp.diff>) |
| `port/tests/yougame_friends_test.cpp` | added | 233 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/265-port_tests_yougame_friends_test.cpp.diff>) |
| `port/tests/yougame_memcard_static_test.cpp` | added | 80 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/266-port_tests_yougame_memcard_static_test.cpp.diff>) |
| `port/tests/yougame_memcard_test.cpp` | added | 33 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/267-port_tests_yougame_memcard_test.cpp.diff>) |
| `port/tests/yougame_merge_test.py` | added | 125 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/268-port_tests_yougame_merge_test.py.diff>) |
| `port/tests/yougame_mod_online_test.cpp` | added | 121 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/269-port_tests_yougame_mod_online_test.cpp.diff>) |
| `port/tests/yougame_mode_policy_test.cpp` | added | 93 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/270-port_tests_yougame_mode_policy_test.cpp.diff>) |
| `port/tests/yougame_net_test.cpp` | added | 118 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/271-port_tests_yougame_net_test.cpp.diff>) |
| `port/tests/yougame_parity_test.cpp` | added | 139 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/272-port_tests_yougame_parity_test.cpp.diff>) |
| `port/tests/yougame_ranked_rules_test.cpp` | added | 114 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/273-port_tests_yougame_ranked_rules_test.cpp.diff>) |
| `port/tests/yougame_ranked_test.cpp` | added | 929 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/274-port_tests_yougame_ranked_test.cpp.diff>) |
| `port/tests/yougame_relay_pads_test.cpp` | added | 242 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/275-port_tests_yougame_relay_pads_test.cpp.diff>) |
| `port/tests/yougame_room_test.cpp` | added | 358 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/276-port_tests_yougame_room_test.cpp.diff>) |
| `port/tests/yougame_sysgen_test.cpp` | added | 287 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/277-port_tests_yougame_sysgen_test.cpp.diff>) |
| `port/tests/yougame_teams_test.cpp` | added | 159 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/278-port_tests_yougame_teams_test.cpp.diff>) |
| `port/tests/yougame_transport_test.cpp` | added | 302 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/279-port_tests_yougame_transport_test.cpp.diff>) |
| `port/tests/yougame_ux_test.cpp` | added | 200 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/280-port_tests_yougame_ux_test.cpp.diff>) |
| `port/third_party/enet/host.c` | modified | 3 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/281-port_third_party_enet_host.c.diff>) |
| `port/third_party/enet/include/enet/unix.h` | modified | 3 / 3 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/282-port_third_party_enet_include_enet_unix.h.diff>) |
| `port/third_party/enet/include/enet/yg_enet_socket.h` | added | 50 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/283-port_third_party_enet_include_enet_yg_enet_socket.h.diff>) |
| `port/third_party/enet/peer.c` | modified | 2 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/284-port_third_party_enet_peer.c.diff>) |
| `port/third_party/enet/protocol.c` | modified | 7 / 4 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/285-port_third_party_enet_protocol.c.diff>) |
| `port/third_party/enet/unix.c` | modified | 40 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/286-port_third_party_enet_unix.c.diff>) |
| `port/third_party/enet/win32.c` | modified | 28 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/287-port_third_party_enet_win32.c.diff>) |
| `port/third_party/enet/yg_enet_socket.c` | added | 345 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/288-port_third_party_enet_yg_enet_socket.c.diff>) |
| `sourceport/game/include/mu_native.h` | modified | 8 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/289-sourceport_game_include_mu_native.h.diff>) |
| `sourceport/game/include/mu_test_env.h` | added | 6 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/290-sourceport_game_include_mu_test_env.h.diff>) |
| `sourceport/game/shim/mu_20xx_ai.c` | modified | 2 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/291-sourceport_game_shim_mu_20xx_ai.c.diff>) |
| `sourceport/game/shim/mu_20xx_cpu.c` | modified | 2 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/292-sourceport_game_shim_mu_20xx_cpu.c.diff>) |
| `sourceport/game/shim/mu_card.c` | modified | 6 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/293-sourceport_game_shim_mu_card.c.diff>) |
| `sourceport/game/shim/mu_entry.c` | modified | 12 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/294-sourceport_game_shim_mu_entry.c.diff>) |
| `sourceport/game/shim/mu_hp.c` | modified | 2 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/295-sourceport_game_shim_mu_hp.c.diff>) |
| `sourceport/game/shim/mu_hp_menu.c` | modified | 2 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/296-sourceport_game_shim_mu_hp_menu.c.diff>) |
| `sourceport/game/shim/mu_lab.c` | modified | 4 / 3 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/297-sourceport_game_shim_mu_lab.c.diff>) |
| `sourceport/game/shim/mu_noslippi.c` | modified | 2 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/298-sourceport_game_shim_mu_noslippi.c.diff>) |
| `sourceport/game/shim/mu_practice.c` | modified | 7 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/299-sourceport_game_shim_mu_practice.c.diff>) |
| `sourceport/game/shim/mu_rng_trace.c` | modified | 2 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/300-sourceport_game_shim_mu_rng_trace.c.diff>) |
| `sourceport/game/shim/mu_slippi_css.c` | modified | 31 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/301-sourceport_game_shim_mu_slippi_css.c.diff>) |
| `sourceport/game/shim/mu_slippi_menu.c` | modified | 37 / 6 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/302-sourceport_game_shim_mu_slippi_menu.c.diff>) |
| `sourceport/game/shim/mu_te.c` | modified | 2 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/303-sourceport_game_shim_mu_te.c.diff>) |
| `sourceport/game/shim/mu_yougame_pause.h` | added | 30 / 0 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/304-sourceport_game_shim_mu_yougame_pause.h.diff>) |
| `sourceport/patches/melee-native.patch` | modified | binary 8117437 → 8118862 bytes | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/305-sourceport_patches_melee-native.patch.diff>) |
| `tools/build_linux.py` | modified | 15 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/306-tools_build_linux.py.diff>) |
| `tools/package_linux_release.py` | modified | 24 / 2 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/307-tools_package_linux_release.py.diff>) |
| `tools/test_linux_agent_hook.py` | added | 68 / 1 | [diff](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/308-tools_test_linux_agent_hook.py.diff>) |

Evidence: [manifest.json](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/manifest.json>), [critical-source-parity.json](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/critical-source-parity.json>), [sys-assets-parity.json](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/sys-assets-parity.json>), [parity-results.txt](<C:/Users/Chandler/NEW project/melee-hotfix-0964/run-current-reports/hbox-visuals/yougame/full-audit/parity-results.txt>).
