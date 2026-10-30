# YouGame launch findings : 2026-10-28

Your subframe rollback and early-input work is retained and working in the tested Windows
YouGame build. No Brawlback gameplay rollback replacement was found in its supplied patch.
The downloaded game is upstream 0.9.63 plus a substantial YouGame integration, not an
unchanged copy of our release binary.

| Priority | Finding | Status |
|---|---|---|
| P0, controller parity | Relayed PlayStation/Switch pads use the XInput path, so their native family bindings/options can be bypassed. Nonstandard DInput/HID pads are filtered from the standard relay; their sandbox path is unverified. | Reproduced mapping difference; do not claim all controller settings work. |
| P1, NVIDIA features | Executable is compiled without Streamline support despite bundled runtime DLLs. | Confirmed; missing build-time SDK/configuration is plausible, deliberate removal not established. |
| P1, tearing | Crunch reports VSync on eliminates the artifacts. Shipped default is VSync off; Match monitor alone does not synchronize scanout. | Upstream fresh-install default changed to VSync on; explicit saved choices remain. YouGame build is unchanged until rebuilt. |
| P1, online compatibility | Existing standalone Slippi gate relied on startup/cached latestVersion, allowing stale or failed refreshes. | Local fix performs fresh fail-closed checks before search/connection and each rematch; tests pass. |
| P2, temporal edges | Unshown catch-up frames polluted AA motion history. | Actual GPU before/after reproduction; fixed locally, 17/17 comparison frames match control. |
| P2, offline delay UX | Shared online slider required a separate offline checkbox. | Separate Offline and Online delay controls; migration/settings tests pass. |
| P2, feature differences | Standard-relay rumble absent; spectator disabled; exact looks and implicit gameplay-mod loaders restricted; app owns accounts/matchmaking/results. | Verified source differences, reasons documented in full audit. |
| Platform | Crunch reports Mac slower than emulator. | Windows evidence does not certify Mac performance; no Mac hardware was available for execution. |

Rollback validation used the actual staging game and DLL, two hidden muted clients, ordinary
relayed synthetic pads, its real shared-memory ENet adapter, and a local app/room fixture.
There were 24 accepted client runs, 39,520 matching confirmed-state checks and 25,357 identical
shared replay frames. Delay/jitter and subframes/early-input toggles were covered. Deliberately
bad early pads triggered the retained trust fallback. Timed samples showed 13.7% fewer rollback
events at fixed delay and 37.5% fewer with jitter when early inputs were on. These measurements
are local scenario results, not a Slippi Dolphin comparison or full public-service certification.

[Full audit and exact file inventory](yougame-audit-2026-10-28.md) ·
[Visual/input timing triage](hbox-triage-2026-10-28.md)

No remote deployment, message to Crunch, or release publication was performed. The YouGame
installed application/account files were not replaced. Owned visible test copies were closed;
subsequent game tests used hidden windows and volume zero.
