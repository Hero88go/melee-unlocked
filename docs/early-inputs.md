# Early inputs

Online, each game sends the other player's game one pad per frame, and when a pad has not arrived in time the game guesses by repeating the last one it has. A press that happens right after a poll waits almost a whole frame before it is even sent, and the other side guesses wrong for every frame in between. Early inputs close most of that gap: the moment the controller changes in a way the game acts on, the new pad is sent at once, stamped with the frame it will first appear in. The other game predicts from it until the real pad arrives. The real pad always decides, so nothing about the match's result, replay or checksums changes; only how often the other side guesses right.

Setting: PC settings, Online, "Early inputs" (on by default; ini key `early_inputs`). Test override for hidden runs: `MELEE_EARLY_INPUTS=0|1`.

## Credit

Early inputs are based on Arte's subframe rollback (JulienBernard3383279, the Kristal build and the
"Subframe rollback" draft for Slippi's Dolphin), which first showed that sending meaningful
controller changes between frames lets the other side predict from them. His timing work (input
timing dispersion reduction) and his audio latency work (timer-driven exclusive WASAPI, forwarding
the audio DMA at once) also shaped the port's input and audio paths. Thanks, Arte.

## Trust

Early pads are optional data a peer volunteers. A remote whose early guesses are mostly wrong (over
60% after 64 guesses; honest play measured under 26% in every such run) has its early pads ignored
for the rest of the match, logged once. Test switch: `MELEE_TEST_EARLY_LIE=1` makes a game send
made-up early pads.

## What is sent

A controller change counts when it changes what the game does this frame (`port/runtime/host/early_input_rules.h`, `matters`). Two rule sets exist for measurement; `MELEE_EARLY_RULES=all|presses` picks one (default `all`).

| Change | all | presses |
|---|---|---|
| A, B, Start | pressed or released | pressed |
| X, Y, Z, L, R | pressed or released | pressed or released |
| D-pad | never | never |
| Main stick X: 23 (walk), 64 (dash), both signs | crossed either way | crossed outward |
| Main stick Y: 23 (tilt), 53 (tap jump), -23, -56 (crouch) | crossed either way | crossed outward |
| Main stick Y -53 while L, R, Z or a trigger shield is held (shield drop) | crossed either way | crossed outward |
| C-stick X: 23, 64, both signs | crossed either way | crossed outward |
| C-stick Y: 23, 53, both signs | crossed either way | crossed outward |
| Analog trigger: 43 (light shield) | crossed either way | crossed either way |

Values are the controller's own units (the game divides by 80). Sticks within two of neutral are read as zero first, as the online code does before it sends a pad. A change inside one zone is not sent on its own.

Follow-ups: the game counts a guess wrong when any stick byte differs from the confirmed pad, and a stick keeps moving after it crosses a line. So once a change crossed a line for frame F, any further byte change before F's poll is sent too, at most one every 2 ms; the newest one for a frame replaces the older one at the receiver.

After a stalled frame (the host sent no pad for a poll), the next reading is sent again if it differs at all from what the other side has.

The payload is 13 bytes: the target pad frame (big-endian i32), the sender's in-game port, the eight pad bytes (buttons big-endian, main stick, C-stick, triggers). Nothing at all is sent while the controller rests.

## The target frame

The host owns the controller poll, so the frame is exact: the pad polled at frame F goes out as pad frame F + delay, and every change seen after that poll lands in pad frame F + delay + 1. The sampler stamps a change with `last sent pad frame + 1`, or `+ 2` between the moment a poll reads the controller and the moment its pad is sent (`note_poll`: a change then is not in that pad). A skipped poll clears that state, since its pad is never sent.

`MELEE_EARLY_GUARD_MS=N` (measurement only, off by default) holds back changes within N ms of when the next poll would come at an even 60 Hz. Online the poll often comes later than that (the game waits for the other player), so the guard is off.

## Who sends, who uses

- Both engines send: the sampler is host code (`port/runtime/host/early_input.cpp`, a thread reading the controllers every millisecond while a match runs).
- The Source Port uses them: its online code is native (`sourceport/game/shim/mu_online.c`). The host appends the other players' early pads after Slippi's fixed inputs reply, and `load_opponent_inputs` predicts a not-yet-confirmed frame from the newest early pad for a frame up to it.
- The Static Recomp does not use them: its prediction is Slippi's own game code, which reads the fixed reply. It still sends, so a Static Recomp player helps a Source Port opponent.

## Transports

- Slippi netplay (`port/runtime/hle/slippi_net.cpp`): the build message (0xE0, which Slippi Dolphin ignores) gained a trailing capability byte; bit 0 says the sender uses early pads. Message 0xE1 carries one early pad and goes only to peers whose build message set the bit. Slippi Dolphin and older Melee Unlocked builds never receive a 0xE1, and a received 0xE1 is dropped unless the sender announced the bit and this side has the setting on.
- Peer to peer (`port/runtime/mu_net`): SAMPLE (0x14) on the input channel, protocol revision 2, sent only to a peer whose INFO said revision 2 or later. See `docs/mu-net-protocol.md`, section 16.
- Any other rollback transport can carry the 13-byte payload the same way: send it unreliably, hand received ones to a per-player `early_input::Buffer`, and let prediction read `Buffer::best(frame, latest)`.

## Receiver rules

Per remote player at most 8 entries, oldest target first. A second message for the same target replaces the first. Entries for frames at or before the remote's latest confirmed frame are dropped as confirmed pads arrive. Prediction for frame N with confirmed pads through L < N uses the newest entry with L < target <= N; none means the last confirmed pad, as before.

## What to read in the log

At the end of an online match the Source Port's game logs `online: N predictions compared, M wrong; K frames predicted from early pads` and `online: early pad predictions right R, wrong W`; the host logs `slippi: early inputs on: C changes, P packets, G guarded, K resent after a skip, F follow-ups, T received, ...` (or `p2p: early inputs ...`). `changes` are the pads handed to the transport, `packets` the ones that went on the wire (a peer takes them). Rollback counts are on the line before them, as they always were.

Early rollback (`MELEE_EARLY_ROLLBACK=1`, off by default): an early pad that contradicts a frame already run starts the rollback at once instead of when the confirmed pad comes. It adds rollbacks of its own, so it stays off unless measurement shows otherwise.

## Tests

- `port_early_input` (ctest): every line of the table above, both ways and in both rule sets, the shield drop; the rest clamp; the wire form; the buffer's order, replacement, cap and prediction choice.
- `port_mu_net`: SAMPLE taken, spent and refused.
- Hidden pair runs: `tools/online_pair.py` and `tools/online_quad.py` with `MELEE_NET_LAG_MS` for held packets and `MELEE_TEST_EARLY_LEAD_MS=N` so a scripted run (whose pads change exactly at the poll) sends the script's next pad N ms early; `tools/p2p_pair.py --lag-ms` for the peer-to-peer path. Checksums must agree with the feature on and off, and a peer with `MELEE_TEST_NO_BUILD_MESSAGE=1` (a stock Slippi client, as far as the protocol goes) must get no 0xE1.
