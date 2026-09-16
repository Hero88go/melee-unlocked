# Discord Rich Presence + game invites for Melee Unlocked

Branch `opus/discord-invite`. Status: designed and implemented behind an off-by-default setting.
Nothing here has been exercised against a real Discord application ID (see "Blocked on the owner").

---

## 1. Which Discord SDK, and why

Discord currently offers three things that could carry a "Join my game" button. Two of them cannot
be shipped in this repository.

### 1.1 Discord Social SDK (Discord's current recommendation) - REJECTED

The Social SDK is what Discord tells new integrations to use. It launched at GDC 2025, left closed
beta in August 2025, and is free for all developers. Rich Presence and deeplink game invites are
first-class features of it.

It is a **closed-source native library**. The C++ getting-started guide says you ship
`discord_partner_sdk.dll` (Windows), `libdiscord_partner_sdk.so` (Linux) or
`libdiscord_partner_sdk.dylib` (macOS) next to your executable and link the provided import library.
There is no source.

The Discord Social SDK Terms grant "a limited, non-exclusive, revocable, non-transferable license"
and prohibit "modifications, creation of derivative works, copying, reproduction, redistribution,
rental, lease, sale, or syndication", and prohibit reverse engineering or deriving source code.

Every file in this port carries `SPDX-License-Identifier: GPL-2.0-or-later`. Shipping that DLL is a
licence problem on two independent grounds:

1. **GPLv2 section 3.** Distributing the executable obliges us to distribute "the complete
   corresponding machine-readable source code" for the whole work, with a carve-out only for things
   "normally distributed (in either source or binary form) with the major components ... of the
   operating system". A Discord DLL is not an operating-system component, and we have no source for
   it. The FSF's position on dynamically linked plug-ins that "make function calls to each other and
   share data structures" is that they "form a single combined program".
2. **GPLv2 section 6.** We "may not impose any further restrictions on the recipients' exercise of
   the rights granted herein". The Social SDK Terms forbid downstream recipients from
   redistributing or modifying the DLL. Passing that restriction along with our GPL binary is
   exactly what section 6 forbids.

This is not a novel argument. 86Box (GPLv2) had the same problem filed against it as
[#1606 "Bundling of the Discord Game SDK violates the GPL"](https://github.com/86Box/86Box/issues/1606).

### 1.2 Legacy Discord Game SDK - REJECTED

`discord_game_sdk.dll`, superseded by the Social SDK and no longer maintained. It is the exact
object the 86Box issue was about, so it carries the same GPL problem as 1.1, plus most of its
managers (Achievements, Applications, Voice, Images, Lobbies, Networking, Storage, Store) were
decommissioned on 2 May 2023. Dead end.

### 1.3 discord-rpc (MIT) - viable but not chosen

[github.com/discord/discord-rpc](https://github.com/discord/discord-rpc) is MIT-licensed
("Copyright 2017 Discord, Inc.") and therefore GPL-compatible. This is what **Dolphin**, itself
GPLv2-or-later, vendors in `Externals/discord-rpc` to drive its Rich Presence and its "join this
netplay session" button - the closest existing precedent to what we want, in a project this port is
directly descended from. Its README says "This library has been deprecated in favor of Discord's
GameSDK", and the repository is unmaintained.

It is worth being precise about what discord-rpc actually *is*: a few hundred lines that open a
local named pipe, exchange length-prefixed JSON, and invoke callbacks. It contains no Discord
intellectual property beyond an implementation of a protocol Discord documents publicly. It also
drags in RapidJSON and its own thread/backoff machinery.

### 1.4 CHOSEN: speak Discord's documented local RPC/IPC protocol directly

Discord publishes the local IPC protocol at <https://docs.discord.com/developers/topics/rpc>:

* the socket, `\\?\pipe\discord-ipc-{n}` on Windows (n = 0..9), `$XDG_RUNTIME_DIR/discord-ipc-{n}`
  and friends elsewhere;
* the frame format, a little-endian `u32 opcode` + `u32 length` header followed by a JSON body;
* opcodes 0 HANDSHAKE, 1 FRAME, 2 CLOSE, 3 PING, 4 PONG;
* the handshake body `{"v":1,"client_id":"..."}`;
* `SET_ACTIVITY`, whose activity object carries `state`, `details`, `timestamps`, `assets`,
  `party {id, size:[cur,max]}`, `secrets {join, spectate, match}` and `instance`;
* `SUBSCRIBE` to `ACTIVITY_JOIN` ("Sent when the user clicks a Rich Presence join invite in chat to
  join a game"), `ACTIVITY_SPECTATE` and `ACTIVITY_JOIN_REQUEST`.

Implementing that against the pipe is roughly 350 lines. It brings in **no third-party code, no
redistributable binary, and no licence obligations at all**, which for a GPL-2 project is decisive.
We already vendor `nlohmann/json` (used by `slippi_net.cpp` and `updater.cpp`) for the JSON, and
`CreateFile`/`ReadFile`/`WriteFile`/`PeekNamedPipe` for the transport.

**Summary: no SDK. Talk to the pipe.** If the protocol ever changes under us, the feature is opt-in
and fails closed, and we would then reassess discord-rpc (1.3) as the fallback, never 1.1 or 1.2.

### 1.5 The one thing that could invalidate this choice

The RPC page says: *"We currently do not allow access to RPC for unapproved apps without being on
the game's list of testers. We grant 50 testing spots."*

My reading is that this gate is on the **OAuth2-authenticated** RPC surface - `AUTHORIZE` /
`AUTHENTICATE` and the `rpc.*` scopes that let an application drive somebody's Discord client (join
voice channels, read guilds). The Rich Presence subset - handshake with a client ID, `SET_ACTIVITY`,
and the activity event subscriptions - requires no OAuth token, and is what every third-party
presence tool on the internet does today with an arbitrary application ID and no approval.

**I could not verify this without an application ID, and it is the single most important thing to
test once one exists.** If it turns out the Join button specifically needs an approved app, the
dev-portal tester list (50 spots) covers a friends-only rollout, which is the realistic use case
here anyway.

A second unverified point: Discord's *current* invite documentation lists three requirements for an
invite button - party information, a join secret, **and supported platforms**. `supported_platforms`
is a Social-SDK-era activity field and is not among the legacy `SET_ACTIVITY` arguments. Whether the
Join button now insists on it is the other thing to check live.

---

## 2. What the application ID requires (BLOCKED ON THE OWNER)

Rich Presence needs an **Application ID** (a snowflake, also called the client ID). It identifies
the game to Discord and is the string in the handshake.

**This is a blocker only he can clear.** It needs his Discord account:

1. Go to <https://discord.com/developers/applications>, "New Application".
2. The application **Name** is what Discord prints after "Playing", so it should read
   `Melee Unlocked`.
3. Copy the **Application ID** from General Information.
4. Optional: Rich Presence > Art Assets, upload a large icon and any small icons. The presence
   payload references them by their asset *key*, not by URL. Without assets the presence still works,
   it is just text.
5. Paste the ID into `port-settings.ini` as `discord_app_id <id>` (or type it into the PC settings
   panel).

**Review requirements:** Rich Presence itself does not go through a Discord review. "Verification"
in the developer portal is a bot-side process for bots in 100+ servers and is unrelated. App
Directory listing is also unrelated. The only approval question is the one in 1.5 above, which I
could not settle from documentation.

**Deliberately not baked in.** The ID is a setting with an empty default, not a compiled-in
constant, so nothing about a Discord application ships in the binary until he decides it should. If
he later wants it baked in, it is a one-line default in `D3D12Options`.

---

## 3. Privacy: connect code, never an IP address

Rich Presence is public. Discord's own documentation warns that "Rich Presence data appears publicly
on your Discord profile". Anything we put in a join secret is visible to anyone who can see the
presence, and the secret is handed to whoever clicks Join.

Therefore the join secret is **the host's Slippi connect code and nothing else** (`HERO#162`).
A connect code is designed to be shared and reveals no network location. Dolphin's equivalent
encodes either `0\n<ip>:<port>` or `1\n<room code>`; we implement only the moral equivalent of the
second form, and there is no code path that can put an IP address into a presence payload.

The port *does* have a raw-IP peering path (`--local-peer idx:local_port:remote_ip:remote_port` in
`port/app/main.cpp`). It is deliberately not wired to Discord. Publishing it would hand the host's
home IP address to every person who can see the invite.

Secondary hardening: an incoming join secret is validated as a connect code before it is used -
1 to 8 uppercase alphanumerics, `#`, 1 to 8 digits, 18 bytes maximum. Anything else is dropped with
a log line. The secret arrives from another machine, so it is untrusted input.

---

## 4. The presence payload

Published through `SET_ACTIVITY`, coalesced and rate-limited to one update every 4 seconds
(Discord's documented ceiling is 5 updates per 20 seconds).

| state | `details` | `state` | `party` | `secrets.join` |
|---|---|---|---|---|
| menus, not logged in | `In the menus` | (none) | (none) | (none) |
| menus, logged in | `In the menus` | `HERO#162` | `1/2`, id `mu-HERO#162` | `HERO#162` |
| waiting on a direct match | `Direct match` | `Waiting for GUES#123` | `1/2` | `HERO#162` |
| ranked / unranked search | `Ranked` / `Unranked` | `Searching` | `1/2` | (none) |
| in a match | `Direct match` | `vs GUEST` | `2/2` | (none) |

Notes on the choices:

* `party.id` is derived from the host's own connect code so Discord treats one player's sessions as
  one party. It is not a secret and does not need to be.
* The join secret is present exactly when a friend could usefully act on it: the local player is
  logged in to Slippi and is not already in a full match. In a match the party reads `2/2`, which is
  how Discord decides to grey the Join button out.
* Matchmaking modes other than Direct do not get a join secret. Nobody can join your ranked queue.
* No `timestamps.start` in the menus; a "for 3 hours" counter on the main menu is noise. A match
  start time would be nice and is easy to add later.

---

## 5. End-to-end flow

### Host

1. Host opens PC settings (F1), ticks **Discord presence**, and has pasted an application ID.
   Default is off; without the tick nothing below happens and no thread exists.
2. `host::discord::enable(true)` starts one dedicated thread. It scans
   `\\.\pipe\discord-ipc-0` .. `-9` with a plain `CreateFile`, handshakes
   `{"v":1,"client_id":"<app id>"}`, then `SUBSCRIBE`s to `ACTIVITY_JOIN`.
3. `slippi::online` pushes presence snapshots at state changes (login, find-match, connection
   success, cleanup, shutdown). Pushing is a mutex-guarded copy of a small struct; no I/O.
4. The presence thread turns the newest snapshot into `SET_ACTIVITY`.
5. Discord shows Melee Unlocked on the host's profile with a **Join** button, and the host can use
   Discord's own "Invite to play" to drop an invite into a channel or DM.

### Friend

6. Friend clicks **Join** in Discord. Their Discord client dispatches
   `{"cmd":"DISPATCH","evt":"ACTIVITY_JOIN","data":{"secret":"HERO#162"}}` over the friend's own IPC
   connection - which exists because their Melee Unlocked is running with the feature on.
7. The friend's presence thread validates the secret and parks it.
8. The friend's simulation thread picks it up on the next EXI command (`slippi::online::handle`)
   and calls `DirectCodes::AddOrUpdateCode("HERO#162")`, which writes it to the front of
   `direct-codes.json`.
9. That file is exactly what the in-game **Online > Direct** name-entry screen autocompletes from
   (`handle_name_entry_load` in `slippi_online.cpp` serves the game's
   `CMD_FETCH_CODE_SUGGESTION` out of it). The host's code is therefore the first suggestion on
   screen.
10. The friend's own connect code is copied to the clipboard, and the PC settings panel shows
    `Discord: joining HERO#162 - your code GUES#123 is on the clipboard`.
11. Friend opens Online > Direct, the code is already filled in, confirm.

### The honest limitation: Slippi Direct is symmetric

`Matchmaking::startMatchmaking` builds the ticket
`{"search": {"mode": 2, "connectCode": <the other player's code>}}`
(`port/runtime/hle/slippi_net.cpp:884`). `mm.slippi.gg` pairs two tickets only when **each names the
other**. So the host must also enter the joiner's code before the match forms.

Discord's invite channel is strictly one-way, host to joiner, and the RPC protocol has no reply
path. `ACTIVITY_JOIN_REQUEST` ("Ask to Join") tells the host that a named Discord user wants in but
carries no Slippi code, so it does not close the loop either.

So the invite gets the joiner all the way to "the host's code is on screen, press confirm", and
leaves the host one paste short. Step 10 exists to make that paste a single ctrl-V into the Discord
chat rather than the joiner reading their code out of a menu. This is a genuine protocol limitation,
not an implementation shortcut, and it should be described to users that way.

If we ever want a true one-click join, the options are (a) a tiny rendezvous service of our own that
the join secret points at, which reintroduces a server dependency, or (b) putting `ip:port` in the
secret, which is the privacy leak we refused in section 3.

### Deliberately NOT implemented: launching the game from Discord

The legacy Game SDK's `RegisterCommand` wrote
`HKCU\Software\Classes\discord-<app id>\shell\open\command` so Discord could start a game that was
not running. We do not write to the registry. Consequences: the friend must already have Melee
Unlocked open when they click Join. Given the game needs an ISO path and a launcher, auto-start
would be fragile, and a silent registry write for an optional cosmetic feature is not a trade worth
making. Whether Discord still offers the Join button at all for an app with no registered command is
one more thing to confirm live.

---

## 6. Threading, failure and off-by-default

**Threading.** One `std::thread`, created only by `enable(true)`. It owns the pipe handle
exclusively. The simulation thread only ever takes a small mutex to copy a POD snapshot in, and to
take a join code out. The render thread only reads a short status string for the settings panel. No
socket, pipe, registry or JSON work happens on either.

**Never blocks.**
* Pipes are opened with a plain `CreateFile`. `WaitNamedPipe` is never called, so a missing Discord
  fails in microseconds with `ERROR_FILE_NOT_FOUND` and a busy one fails with `ERROR_PIPE_BUSY`.
* Reads are gated behind `PeekNamedPipe`, so the thread never sits in a blocking `ReadFile`.
* The loop wakes on a condition variable with a 250 ms timeout, so shutdown latency is bounded.
* Any error at all closes the handle and schedules a reconnect 15 seconds later. Forever, quietly.

**No dialogs, ever.** Failures are `host::log` lines and a one-line status in the settings panel.
Discord not being installed is the normal case, not an error.

**Off by default.**
* `D3D12Options::discord_presence` defaults to `false` and `discord_app_id` to `""`.
* `load_pc_settings` only sets them if `port-settings.ini` contains `discord 1` / `discord_app_id`.
  An existing settings file has neither, so upgrading changes nothing.
* With the feature off, `enable()` is never called, no thread is created, no pipe is opened, and
  `publish()` returns on an `std::atomic<bool>` load before touching the mutex.
* Automated/headless runs (`--headless`, `--hidden`) never load PC settings at all, so test runs can
  never publish.

---

## 7. Files

| file | change |
|---|---|
| `port/runtime/host/discord_presence.h` | new: the whole public surface |
| `port/runtime/host/discord_presence.cpp` | new: IPC client, worker thread, payload builder |
| `port/runtime/gx/gx_d3d12.h` | `discord_presence`, `discord_app_id` on `D3D12Options` |
| `port/runtime/gx/pc_settings.cpp` | load, save and the settings-panel checkbox + ID field |
| `port/runtime/hle/slippi_online.cpp` | publish snapshots, consume incoming join codes |
| `port/app/main.cpp` | `enable()` after settings load, `shutdown()` before exit |

`port/CMakeLists.txt` needs no change: `runtime` is built from
`file(GLOB ... runtime/host/*.cpp ...)`, so the new file is picked up on reconfigure.

---

## 8. What was actually verified, and what was not

Verified against the real Discord client running on this machine (a throwaway harness that included
`discord_presence.cpp` directly; it used a deliberately invalid Application ID so it could never put
an activity on a real account):

* Pipe discovery finds `\\.\pipe\discord-ipc-0`, and the handshake frame is accepted and answered.
* A wrong Application ID comes back as opcode 2 with
  `{"code":4000,"message":"Invalid Client ID"}`, and the settings panel now reports exactly that
  instead of a misleading "Discord is not running".
* **This found a real bug.** The first version reported a dead pipe by returning `false` from the
  reader's `pump()`, and the caller then bailed out without draining the bytes it had just read.
  Discord sends the CLOSE frame and drops the pipe in the same breath, so the one frame that says
  what went wrong was always thrown away and every failure looked like "Discord is not running".
  The reader now reports a dead pipe through a flag and callers drain the buffer first.
* Probing pipes that do not exist (Discord not installed or not running) costs **2.6 us per probe**,
  26 us for a full scan, once every 15 seconds, on the presence thread. No hang.
* `enable(false)` from the render thread returned in **0 ms** in every run.
* `publish()` while the feature is off: 200,000 calls in 1-2 ms, i.e. one relaxed atomic load.
* Repeated enable/disable churn, a double `shutdown()`, and `take_join_code()` while disabled are
  all safe.
* The payload never contains an address; `valid_connect_code` rejects `192.168.1.5:41000`,
  lowercase tags, missing `#`, missing digits and over-long strings.
* A full party (2/2) publishes no join secret; an empty presence serialises to `null`, which is how
  SET_ACTIVITY clears a presence.

**Not verified, because it needs a real Application ID:**

* That a READY handshake succeeds at all for an unapproved app (section 1.5). This is the one thing
  that could still sink the approach.
* That `SET_ACTIVITY` is accepted and the presence renders.
* That a **Join** button appears without the Social-SDK-era `supported_platforms` field.
* That `SUBSCRIBE`/`ACTIVITY_JOIN` delivers the secret when a friend presses Join.
* That Discord offers Join for an application with no registered launch command.
* The end-to-end join: two machines, two Discord accounts, two Slippi logins.

Also untested by me: the settings panel itself, which needs a windowed session. The load, save and
UI code follows the existing pattern in `pc_settings.cpp` exactly and compiles, but no human has
clicked the checkbox yet.

Unrelated pre-existing failures noticed while running the suite: `port_frame_queue` aborts (the test
still asserts a queue capacity of 2 while `frame_queue.h` caps at 32) and `port_texture_snapshots`
fails with "different palette or mip was reused". Neither test compiles any file this branch
touches, and both predate it.

## 9. Sources

* Rich Presence overview - <https://docs.discord.com/developers/platform/rich-presence>
* RPC / IPC protocol - <https://docs.discord.com/developers/topics/rpc>
* Social SDK, C++ getting started (the DLL list) -
  <https://docs.discord.com/developers/discord-social-sdk/getting-started/using-c++>
* Social SDK, managing game invites -
  <https://docs.discord.com/developers/discord-social-sdk/development-guides/managing-game-invites>
* Discord Social SDK Terms -
  <https://support-dev.discord.com/hc/en-us/articles/30225844245271-Discord-Social-SDK-Terms>
* Migrating from the Legacy Game SDK -
  <https://support-dev.discord.com/hc/en-us/articles/30125671534359-Migrating-from-the-Legacy-Game-SDK-to-the-Discord-Social-SDK>
* Announcing the Social SDK - <https://discord.com/blog/announcing-discords-social-sdk-helping-power-your-games-social-experiences>
* discord-rpc, MIT, deprecated - <https://github.com/discord/discord-rpc>
* Legacy Game SDK Activities reference -
  <https://github.com/discord/discord-api-docs/blob/legacy-gamesdk/docs/game_sdk/Activities.md>
* GPL precedent, 86Box - <https://github.com/86Box/86Box/issues/1606>
* GPLv2-era precedent in a sibling project, Dolphin's `DiscordPresence.cpp` -
  <https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/UICommon/DiscordPresence.cpp>
