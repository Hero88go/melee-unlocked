# mu_net gameplay protocol, version 1 (revision 1)

Revision 1 adds more than one game over one connection (section 14). It changes no layout and no rule of a session's first game, so a revision 0 peer and a revision 1 peer play that game together; see section 12 for what a revision may change.

This is the wire format of the Melee Unlocked peer-to-peer gameplay connection used by P2P Direct and P2P Unranked. It is written so that a second implementation can interoperate. The reference implementation is `port/runtime/mu_net/`.

The protocol is new. It shares no message ids, packet layouts or credentials with any other netplay system, and it is never sent to one.

## 1. Conventions

- All integers on the wire are little-endian. `u8`, `u16`, `u32`, `u64` are unsigned, `i32` is signed two's complement.
- `bytes[n]` is n raw bytes. `str8(max)` is a `u8` length followed by that many bytes of UTF-8, at most `max`.
- BLAKE2b always means BLAKE2b with a 32-byte output. Ed25519 is the standard (SHA-512) variant. X25519 is RFC 7748. The AEAD is XChaCha20-Poly1305 with a 24-byte nonce and a 16-byte tag.
- Domain strings are ASCII with no terminating zero.
- A frame number is the game's frame counter. Frame 1 is the first frame of the match.

## 2. Transport

- UDP, carried by ENet 1.3.13 framing. Each side opens one ENet host with 2 channels.
- Channel 0, "control": reliable and ordered (ENet reliable packets).
- Channel 1, "input": unreliable and unsequenced (ENet unsequenced packets). Loss, duplication and reordering are expected and handled by this protocol.
- Largest plain message: 1200 bytes. Largest protected packet: 1226 bytes. Anything larger, anything of length 0, and anything on a channel other than 0 or 1 is dropped before any parsing.
- A one-byte UDP datagram may arrive outside ENet framing during connection setup (section 3). It carries nothing and is ignored.

## 3. Connecting

Each peer knows zero or more candidate endpoints (IPv4 address and UDP port) of the other, from an invitation, the lobby, a LAN beacon or manual entry.

1. Both peers open their UDP port and start an ENet connection to every candidate of the other peer (at most 6). They also accept ENet connections that arrive, from any address.
2. While no connection is up, each peer sends a one-byte datagram (value 0) to every candidate every 100 ms. This keeps its own NAT mapping open so the other side's connection attempt can get in. The two peers share no start time: each does this from its own start until a connection is up or its limit passes, so the two windows only have to overlap.
3. From one second after the start, a peer may send the same one-byte datagram every 300 ms to at most 8 neighbouring ports of the candidates that are on a public address (port +1, +2, +3 and -1 of each, in the order given). No connection is dialed to them. This is for a router that has moved the other peer to the next port; when that peer's connection attempt then arrives from such a port, it is accepted like any other.
4. An ENet connection attempt that got no answer ends by itself after about 15 seconds. While no connection is chosen, the peer starts it again toward the same candidate.
5. More than one ENet connection between the two peers may come up (typically one dialed by each side). All of them are used for the hello (section 4) until one is chosen (section 5). The others are then closed.
6. If no connection is up when the limit passes, the attempt fails. The limit is 8 seconds unless the host sets another (the launcher asks for 45, because the two games start seconds apart). There is no relay.

A peer with no candidates only listens. That is enough when its port is reachable (LAN, or a forwarded port).

An address is never more than a place to send to. Whatever answers there is only accepted as the other player by the handshake of section 4, against the identity key the lobby or the invitation named.

Section 15 says where candidates come from, what keeps the path open, and which networks cannot be connected.

## 4. Handshake

### 4.1 HELLO

The only message that is ever sent unprotected. Channel 0. Exactly 160 bytes.

| Offset | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | magic | bytes[4] | `MUNH` |
| 4 | version | u16 | protocol version, 1 |
| 6 | flags | u16 | 0. A receiver refuses any other value |
| 8 | identity | bytes[32] | sender's Ed25519 public key (its persistent peer identity) |
| 40 | ephemeral | bytes[32] | sender's X25519 public key, new for every session |
| 72 | nonce | bytes[24] | random |
| 96 | signature | bytes[64] | Ed25519 by `identity` over `"MeleeUnlockedNet1 hello"` followed by bytes 0 to 95 |

Each peer sends its HELLO on every connection that comes up, as the first message on it.

A receiver checks, in this order, and stops at the first failure:

1. At least 6 bytes and the magic.
2. `version` equals its own. Otherwise the peer runs another protocol version: there is no negotiation, the session fails with a "different version" reason.
3. Length is exactly 160 and `flags` is 0.
4. If a HELLO was already accepted: byte-for-byte the same one is ignored (it came over a second connection), a different one is refused.
5. `ephemeral` differs from its own (a reflected HELLO is refused).
6. If the receiver was given an expected peer identity (from an invitation or the lobby), `identity` equals it.
7. The signature verifies.
8. The X25519 shared secret is not all zero.

### 4.2 Keys

Let `shared = X25519(own ephemeral secret, peer ephemeral public)`. Sort the two ephemeral public keys as byte strings; call the smaller `E1` and the larger `E2`, and the HELLO messages that carried them `H1` and `H2`.

```
master     = BLAKE2b("MeleeUnlockedNet1 session key" || shared || E1 || E2)
transcript = BLAKE2b("MeleeUnlockedNet1 transcript" || H1 || H2)
key(E)     = BLAKE2b keyed with master, over ("MeleeUnlockedNet1 direction" || transcript || E)
```

A peer encrypts what it sends with `key(its own ephemeral public key)` and decrypts what it receives with `key(the peer's ephemeral public key)`. `shared`, `master` and the ephemeral secret are erased once the two keys exist.

The peer whose ephemeral key is `E1` is the **decider**.

### 4.3 Protected packets

Every message after the HELLO, on both channels:

| Offset | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | packet version | u8 | 1 |
| 1 | channel | u8 | 0 or 1, and equal to the ENet channel the packet travels on |
| 2 | sequence | u64 | per sender, starts at 0, plus 1 for every packet on either channel |
| 10 | ciphertext | bytes[n] | n is 0 to 1200 |
| 10 + n | tag | bytes[16] | |

- Nonce (24 bytes): `sequence` as u64, then `channel`, then 15 zero bytes.
- Additional data (6 bytes): `MUN1`, then the packet version byte, then the channel byte.

A receiver checks, in this order: length at least 26; length at most 1226; packet version is 1; channel byte equals the arrival channel; a handshake is complete; the sequence passes the replay rule; the tag verifies. The replay state is updated only after the tag verified.

Replay rule:

- Channel 0: the sequence must be greater than the last accepted sequence on channel 0.
- Channel 1: a sliding window of 64. A sequence greater than the highest accepted is new. A sequence within 63 of the highest is accepted once. Anything older, or seen before, is dropped.

The byte 0 value tells the two kinds of channel 0 traffic apart: `M` (0x4D) starts a HELLO, 1 starts a protected packet. Any other first byte is dropped.

## 5. Session descriptor and VERIFY

### 5.1 Canonical descriptor

Both peers hold a session descriptor before they connect (agreed through the invitation or lobby). Its canonical bytes:

| Field | Type | Notes |
| --- | --- | --- |
| magic | bytes[4] | `MUSD` |
| format | u16 | 1 |
| protocol version | u16 | 1 |
| route | u8 | 1 = P2P Direct, 2 = P2P Unranked |
| build id | str8(64) | the build both sides run |
| content hash | bytes[32] | game content both sides load |
| rules profile | u32 | 0 = default singles rules |
| match id | bytes[16] | |
| 4 player slots | see below | always four, present or not |
| stage | u16 | |
| alt stage | u8 | |
| rng seed | u32 | |
| input delay | u8 | 1 to 15, default 2 |
| rollback horizon | u8 | 7 |
| stocks | u8 | |
| timer seconds | u32 | |
| features | u32 | bit 0 reserved for sub-frame samples and must be 0; bit 1 pause allowed |

Player slot: `present u8` (0 or 1), `character u8`, `color u8`, `name str8(31)`, `identity key bytes[32]`, `session key bytes[32]`.

Version 1 plays two players, in slots 0 and 1.

After the handshake each peer writes into its copy, for both slots, the identity key and the ephemeral (session) key it saw in the handshake. A key that was already non-zero in the descriptor must equal the handshake's value or the session fails. Then:

```
digest = BLAKE2b("MeleeUnlockedNet1 descriptor" || canonical bytes)
```

Because the digest covers both identities and both session keys, equal digests mean the same setup and the same two peers on this very connection.

### 5.2 VERIFY (control, type 0x01, 34 bytes)

`type u8 = 0x01`, `digest bytes[32]`, `slot u8` (the sender's own slot).

- The decider, as soon as its handshake completes: chooses the connection the accepted HELLO arrived on, closes its other connections, sends its HELLO again on the chosen connection, then VERIFY.
- The other peer, on the first VERIFY that decrypts: chooses the connection it arrived on, closes the others, and answers with its own VERIFY.
- Each peer compares the received digest with its own, and the received slot with the slot it expects the peer in. On a difference it sends BYE with reason 1 and fails. On equality the session is **ready**. No frame is simulated before that.

The whole of sections 3 to 5 must finish within 8 seconds of the start.

## 6. Control messages (channel 0)

| Type | Name | Body | Meaning |
| --- | --- | --- | --- |
| 0x01 | VERIFY | digest bytes[32], slot u8 | section 5.2 |
| 0x02 | BYE | reason u8 | the sender ends the session. 0 = left, 1 = descriptor mismatch |
| 0x03 | RESULT | frames i32, digest bytes[32] | sender's input transcript digest after `frames` confirmed frames (section 9) |
| 0x04 | INFO | revision u8, games u16 | revision 1. The sender's protocol revision and the most games it plays in this session, 0 for no limit (section 14) |
| 0x05 | NEXT | game u16, digest bytes[32] | revision 1. The sender's game has ended and it is ready for game `game`, whose descriptor digest is `digest` (section 14) |

An unknown control type is counted and ignored. That is what a revision 0 peer does with INFO and NEXT.

## 7. Input messages (channel 1)

### 7.1 COMMIT (type 0x10, 22 + 8 x count bytes)

| Offset | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | type | u8 | 0x10 |
| 1 | frame | i32 | newest committed frame in this message |
| 5 | count | u8 | frames carried, 1 to 128 |
| 6 | checksum frame | i32 | newest finalized frame the sender has a state checksum for, 0 if none |
| 10 | checksum | u32 | that frame's checksum |
| 14 | ack | i32 | highest frame F such that the sender holds every frame 1..F of the receiver |
| 18 | stamp | u32 | low 32 bits of the sender's microsecond clock at send |
| 22 | pads | bytes[8] x count | pad of `frame`, then `frame - 1`, and so on |

Rules:

- A frame is committed once and never changes. Frames are committed in order from 1.
- The pad sampled for game frame f is committed as frame f + input delay. Frames 1 to input delay carry a zero pad.
- A COMMIT carries every frame the receiver has not acknowledged, newest first, at most 128. When everything is acknowledged it carries the newest frame alone.
- One COMMIT is sent on every simulation tick, including ticks the game is held on.
- The receiver refuses the message unless: the length is exactly 22 + 8 x count; count is 1 to 128; frame is at least count; checksum frame and ack are not negative; ack is not above the newest frame the receiver itself has committed.
- If the oldest carried frame is more than one past what the receiver holds, nothing is taken (a gap).
- Frames the receiver already holds are not replaced. Different bytes for a held frame are counted as a conflict.
- Frames more than 200 past the receiver's own finalized frame are not taken.
- A pad is 8 bytes: the first 8 bytes of the game's 12-byte pad. The receiver hands the game those 8 bytes followed by 4 zero bytes.
- In game k of a session (section 14) the fields `frame`, `checksum frame` (when not 0) and `ack` travel raised by (k - 1) x 16777216. For the first game that is the plain frame number. Every rule above is about the number with that base taken off.
- A COMMIT whose `frame` is not in base + 1 to base + 16777215, or whose `ack` is not in base to base + 16777215, for the game the receiver is in, belongs to another game of the session. It is dropped whole (no frame, no acknowledgment, no checksum pair is taken from it), it is not answered, and it is not an error.

### 7.2 ACK (type 0x11, 9 bytes)

`type u8`, `ack i32`, `echo u32`. Sent at once in answer to a COMMIT that delivered at least one new frame. `ack` is as in COMMIT, with the same base in a later game, and an ACK whose `ack` is outside the receiver's game is dropped the same way. `echo` is that COMMIT's `stamp`. The receiver's round trip time is its clock (low 32 bits) minus `echo`; values above 2 seconds are discarded.

### 7.3 PING (0x12) and PONG (0x13), 5 bytes each

`type u8`, `stamp u32`. PONG echoes the PING's stamp. Sent every 250 ms between ready and frame 1, so a round trip time exists before the match.

### 7.4 SAMPLE (type 0x14, 21 bytes), reserved

`type u8`, `sample sequence u32`, `sender time u32`, `target frame i32`, `pad bytes[8]`. Provisional sub-frame input. Version 1 checks the length and ignores the content. A SAMPLE never confirms a frame. It may only be acted on when feature bit 0 is set in the descriptor, which version 1 refuses.

An unknown input type, or a known type with the wrong length, is counted and dropped.

## 8. Rollback session rules

These decide the answers the game is given. Both peers must follow them for matches to feel the same, though only the commit rules above affect correctness.

- **Input window.** The game is shown at most 7 of the peer's frames at a time, starting at the frame after the last one it has confirmed and never at the newest frame received. When 8 or more frames arrive at once (the peer catching up after a hitch), a window anchored at the newest frame would drop the oldest unconfirmed one and the game would confirm a wrong pad; anchored at the confirmed frame, the extra frames wait and are shown as the window moves.
- **Stall rule.** The game may not simulate frame f while the newest frame of that window is less than f - 7. It is held (asked again next tick) until inputs arrive.
- **First frame.** The pad queue is empty when a match starts: the first frame the game samples is answered with a neutral pad, and the first real pad belongs to the frame after it. Both peers do this, so the frame numbering of the two transcripts is the same.
- **Input timeout.** A hold that lasts more than 7 seconds ends the match as disconnected. A BYE or a lost connection does the same at once.
- **Time offset.** On the first arrival of each new peer frame F: `offset = (now - round trip / 2) - T + 16683 x (L - F)` microseconds, where L and T are the frame and send time of this side's own latest COMMIT. Keep the last 30 samples; the estimate is the mean of the middle third. Positive means this side is ahead.
- **Time sync, every 30 frames.** Ahead by more than 10 ms in the first 120 frames: hold up to 5 frames. Later, ahead by more than 10 ms plus two frames: hold 1 frame. Behind by more than 10 ms plus one frame after frame 120: run up to 3 extra frames, one every fifth frame. In between, adjust simulation speed: up to 1 % faster when behind by 0.25 ms or more, up to 0.5 % slower when ahead by 8 ms or more, both scaled over three frames of offset.
- **Checksums.** When the peer's (checksum frame, checksum) is for a frame this side also has a checksum for, compare them. A difference of more than 1 in the low 16 bits (as signed values) is a desync; any other difference is logged once as a risk.

## 9. Input transcript and RESULT

Each peer keeps a running BLAKE2b state, started with `"MeleeUnlockedNet1 inputs" || descriptor digest`. For every frame f, in order, once it holds both pads for f, it appends `f` as u32, then slot 0's 8 pad bytes, then slot 1's. The transcript digest after n frames is the BLAKE2b output at that point.

When its game ends, a peer sends RESULT with its confirmed frame count and digest. A peer that has itself reached the other's frame count compares the two digests.

Every game of a session has a transcript of its own: it starts from that game's descriptor digest, and `f` is the game's own frame number (counted from 1, without the base of section 7.1).

## 10. LAN beacon

UDP broadcast to port 47633, not inside ENet, not encrypted:

`magic bytes[4] = "MUNB"`, `version u16`, `game port u16`, `identity bytes[32]`, `name str8(31)`, `build id str8(64)`, `signature bytes[64]`.

The signature is Ed25519 by `identity` over `"MeleeUnlockedNet1 beacon"` followed by all bytes in front of the signature. The sender's address is the datagram's source address. A beacon can be replayed by anyone on the LAN, so it is only a hint where to connect: the handshake, with the beacon's identity as the expected peer, is what authenticates.

## 11. Result file

After a game each peer may write a JSON file:

```
{"body":{...},"identity":"<64 hex>","signature":"<128 hex>"}
```

The signature is Ed25519 by `identity` over `"MeleeUnlockedNet1 result"` followed by the exact bytes of the body object as they stand in the file. The body has a fixed key order and no optional whitespace: `format`, `protocol`, `route`, `build_id`, `descriptor_digest`, `match_id`, `local_slot`, `peer_identity`, `transcript_digest`, `transcript_frames`, `transcript_complete`, `peer_agreement` (`match`, `mismatch` or `unknown`), `desync`, `disconnected`, `has_outcome`, `mode`, `frame_length`, `game_index`, `tiebreak_index`, `winner`, `end_method`, `lras_initiator`, `synced_timer`, `players` (four objects: `slot_type`, `stocks`, `damage_done`, `synced_stocks`, `synced_damage`), `game_info_hash`.

One file is one player's signed claim. Two files with the same descriptor digest and transcript digest, signed by the two identities the descriptor names, are a two-sided record.

A session of several games writes one file per game. Each game has a descriptor digest and a match id of its own (section 14), so the files of one session cannot be mistaken for each other. `disconnected` is about the game: a peer that leaves after the game reported its end does not set it.

## 12. Version rules

- The protocol version is one number. It appears in the HELLO, in the descriptor and in the beacon. Peers with different numbers do not play; there is no downgrade.
- Any change to a message layout, a size limit, a key derivation, the commit rules or the canonical descriptor is a new protocol version.
- The descriptor `format` number changes when only the descriptor's serialization changes.
- New optional behaviour is negotiated through descriptor feature bits. A peer refuses a descriptor with a bit it does not implement. Commit rules never change during a match.
- Unknown message types are dropped and counted, never treated as fatal, so a later version can add messages behind a feature bit.
- A revision is a change inside one protocol version that leaves the first game of a session exactly as it was: new control messages, and behaviour that only starts after both peers have said they know it. The revision is not in the HELLO, the descriptor or the beacon. A peer says its revision in INFO; a peer that sends no INFO is revision 0. Nothing of a revision is used toward a peer that has not announced it, so a peer of an older revision keeps working and simply does not get the addition.
- Revision 0: sections 1 to 13 without INFO, NEXT and the frame base. Revision 1: section 14.

## 13. Limits a receiver enforces

| Limit | Value |
| --- | --- |
| Plain message | 1200 bytes |
| Protected packet | 1226 bytes |
| HELLO | exactly 160 bytes |
| Frames per COMMIT | 128 |
| Peer frames past own finalized frame | 200 |
| Replay window, input channel | 64 packets |
| Candidate endpoints dialed | 6 |
| Neighbouring ports sent the one-byte datagram | 8 |
| Simultaneous connections | 8 |
| Packets handled per pump | 256 events, 512 queued |
| Connect and handshake | 8 seconds, or the host's own limit (1 to 600 seconds) |
| Wait for inputs | 7 seconds |
| Round trip time accepted | up to 2 seconds |
| Games in one session | 127 |
| Wait for the peer's NEXT | 20 seconds |

## 14. More than one game in a session (revision 1)

After a game ends the two peers can play another one over the same connection, with the same keys, without a new handshake. Both must agree on the next game's descriptor before its frame 1, and nothing that differs between the two machines enters it.

### 14.1 INFO

`type u8 = 0x04`, `revision u8`, `games u16`. 4 bytes. Sent once, on the control channel, when the sender becomes ready (after it has sent its own VERIFY and accepted the peer's). `games` is the most games the sender plays in this session, 0 for no limit.

The session plays at most the smaller of the two `games` values (0 counts as 127). With a peer that sent no INFO, or one whose revision is 0, the session is one game. A second INFO in a session is ignored.

### 14.2 The next descriptor

The descriptor of game n + 1 is made from the descriptor of game n (with the handshake's keys filled in) and that descriptor's digest:

```
h = BLAKE2b("MeleeUnlockedNet1 next" || digest of game n || (n + 1) as u32)
match id = h[0..15]
rng seed = h[16..19] as u32
stage    = pool[(h[20..23] as u32) mod pool size]
```

Every other field carries over: route, build id, content hash, rules profile, both players (character, color, name, identity key, session key), alt stage, input delay, rollback horizon, stocks, timer, features. The pool is the stage list of the rules profile, in a fixed order; for the default singles rules it is the stage ids 0x1F, 0x20, 0x1C, 0x08, 0x02, 0x03. With no pool the stage carries over too.

The first game's seed and both session keys are inside the first digest, and each draw is chained to the digest before it, so the whole series of stages and seeds is fixed by the handshake and is the same on both sides.

### 14.3 NEXT

`type u8 = 0x05`, `game u16`, `digest bytes[32]`. 35 bytes. `game` is the number of the game the sender is ready for (2 after the first game) and `digest` is that game's descriptor digest (section 5.1, over the descriptor of 14.2).

A peer sends NEXT when its own game has reported its end, after its RESULT, and only if all of these hold:

- the session's game limit (14.1) is not reached,
- the peer is still there,
- no state checksum disagreed during the game,
- the game ended by itself: it has an outcome, and the end was not a quit from the pause screen.

Otherwise it sends nothing and the session is over for it. It closes the connection with BYE when it leaves.

A peer that has sent NEXT and holds the other's NEXT compares game number and digest:

- Equal: the next game is agreed. Each peer then starts it on its own: descriptor and digest of 14.2, frame 1 again, a new transcript (section 9), no checksums held, PING and PONG until its frame 1 as in 7.3, and the frame base of section 7.1 for game n + 1.
- Different: it sends BYE with reason 1 and plays no further game. The finished game's result stands.

A NEXT can arrive while the receiver's own game is still running (the two games end a few frames apart). It is kept and compared when the receiver sends its own. Only one NEXT per game is taken.

The session is over, without an error, when a BYE arrives or the connection closes between two games, or when the peer's NEXT has not arrived 20 seconds after this side's own.

The two peers do not begin the next game at the same instant. Until both have, the input messages of the one that is ahead carry a frame base the other does not accept yet, and are dropped (section 7.1); they are sent again until acknowledged, so nothing is lost, and the stall rule holds the peer that is ahead.

### 14.4 A revision 0 peer

It ignores INFO, sends none, and never sends NEXT. The revision 1 peer therefore plays one game with it (14.1) and sends it no NEXT. Game 1, its RESULT and its result file are exactly as in revision 0.

## 15. Getting through home routers

Nothing in this section changes a message of sections 2 to 14. It is about which addresses a peer is given as candidates and how the path between two routers is opened and kept open. The code is `port/runtime/mu_net/nat_discovery.*` (used by the launcher's lobby) and `peer_connector.*` (the game). The project runs no server for any of it.

### 15.1 One UDP port for the lobby and the game

The launcher's lobby talks to other launchers from one UDP socket. When a match is agreed, the lobby closes that socket and the game binds the same local port. A router keeps a mapping per local address and port for some time after the last packet (30 seconds on the strictest routers, 2 minutes or more on most), so the game's packets leave through the mapping the lobby's packets made, as long as the game sends its first datagram before that time is up.

This is why the first candidate is always the address and port the other launcher was heard from. The two lobbies were exchanging packets over exactly that pair of addresses a moment ago, so that path is known to work for both routers, including a router that gives each destination its own outside port: the destination is the same one.

### 15.2 Candidates

Each launcher sends its half of a match setup with the accept of a request, or with the acknowledgment of that accept. The fields about addresses:

| Field | Type | Meaning |
| --- | --- | --- |
| `port` | number | the game's local UDP port |
| `lobby` | boolean | true when that port is the lobby's own: the receiver uses the address and port it hears this launcher from |
| `addrs` | array of at most 4 `"a.b.c.d:port"` | this PC's own addresses (the same network) |
| `ext` | array of at most 4 `"a.b.c.d:port"`, optional | public addresses this launcher learned for the port (15.3, 15.4) |

A launcher that does not know `ext` ignores it, and a launcher that gets a setup without it dials what it always did. A receiver takes at most 2 entries of `ext`, and only those on a public address.

The receiver builds the list the game dials, at most 6, never one twice, in this order:

1. the address it hears the other launcher from (with `lobby`), or that address with `port`;
2. the entries of `ext`;
3. the entries of `addrs`.

### 15.3 STUN

The launcher may send an RFC 5389 Binding Request (20 bytes: type 0x0001, length 0, the magic cookie 0x2112A442, a random 96-bit transaction id, no attributes) from the lobby socket to public STUN servers, and read XOR-MAPPED-ADDRESS (or MAPPED-ADDRESS) from the answer. An answer is matched by its transaction id only; it is not authenticated, and what it says is only ever a candidate.

- It tells the launcher the address and port its router gave the lobby socket, before any other player has been heard from. That goes into `ext` and into the "Connect by address" line.
- Two servers at two different addresses are asked. When they report different ports, the router gives every destination its own port (often called symmetric NAT), and the launcher can say so before a match is tried.
- The default servers are `stun.l.google.com:19302`, `stun.cloudflare.com:3478` and `stun1.l.google.com:19302`. The player can name others or switch STUN off. A STUN server learns the player's IP address and nothing else.
- Names are looked up on a thread of their own and the answers are read as part of the lobby's normal receive loop, so a STUN server that is down or blocked costs nothing: the lobby goes online the same way, with one candidate fewer.

### 15.4 Port mapping

The launcher may ask the home router to send the lobby's UDP port to this PC: first NAT-PMP (RFC 6886, UDP port 5351 of the default gateway), then UPnP IGD (an SSDP search, the device description, `AddPortMapping` on its WANIPConnection or WANPPPConnection service). It asks for a lease of one hour, renews it at half time, and removes the mapping when the launcher closes. A router that only makes mappings without an end gets one without an end, and it stays in the router if the launcher is killed before it can remove it.

- Only a gateway on a private (or carrier-shared) address is asked, and a UPnP device is only talked to at the address its search answer came from.
- It runs on a thread of its own: under a second when the router has NAT-PMP, up to about ten seconds when the router answers neither. The lobby never waits for it.
- When it works and the router's outside address is public, that address and the mapped port go first into `ext`. Such a port is open from outside without any punch.
- When the router's outside address is itself private or carrier-shared, a second router stands in front of it. The mapping is kept but is not named as a candidate.

### 15.5 The punch

Section 3 is the whole of it: both games dial and send the one-byte datagram to every candidate from their own start until a connection is up, a few neighbouring ports are tried as well, and the first connection that carries the signed HELLO and VERIFY is chosen (section 5.2).

When no connection came up, the game says which of these it saw, in one sentence:

- nothing arrived from any of the other player's addresses (a router or firewall drops the game's UDP port);
- datagrams arrived only from a local network address although public ones were given;
- datagrams arrived from the other player's address but from a port that was not named (their router uses a different port for every destination);
- datagrams arrived from an address that was dialed and still no connection completed (one direction is blocked).

### 15.6 Keeping the path open

Once a connection is up, ENet sends a ping on it at least every 250 ms whenever nothing else was sent, for as long as the session exists: before the first game, during a game, and between two games of a session (section 14). A router's mapping is therefore never idle for more than a quarter of a second, well inside the shortest timeouts in use. A connection that hears nothing for 4 to 8 seconds is closed.

### 15.7 What cannot connect

- **Both players behind a router that gives every destination its own port, or behind carrier-grade NAT** (the provider's own router in front of the home router, common on mobile and some fibre and cable lines). Neither side can learn a port the other can reach, the port guesses of section 3 rarely hit, and no router on the path can be asked for a mapping. Such a pair needs a relay, and the project does not run one. If only one of the two is in that position, the match usually connects.
- **A firewall that drops UDP to or from the game** on either PC or router. Windows asks once, on the first match, to allow the game on the network; a "no" there blocks it.
- **Two players behind the same public address** (the same home, campus or carrier) whose router does not send a packet for its own outside address back inside. They connect over the `addrs` entries when they are on the same network, and not at all when they are on different networks behind that address.
- **IPv6 only.** Every address here is IPv4.

In all of these, forwarding the game's UDP port by hand on one of the two routers makes the match connect, when that router has a public address.
