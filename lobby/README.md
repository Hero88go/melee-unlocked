# Melee Unlocked lobby and friends

The launcher has a **Multiplayer Lobby** tab. It uses the peer-to-peer network
(no Melee Unlocked service); the connection mode and bootstrap address are not shown
in the normal UI. Public lobby membership is opt-in: **Go Online** starts unchecked. Checking
it shows your name in reachable players' rosters and lets them send match requests
while the launcher is open. Unchecking it removes you from the public roster.
Players choose a display name, a self-reported location, and three different mains.
The first main is the profile avatar and the initial fighter for an accepted match.
The Slippi connect code is read from the Slippi account linked on this PC and cannot
be edited in the lobby.

The roster shows location, mains and availability; hover a player to see their connect
code, and select them to see your local win/loss record against them. Ping is not shown
in the roster: it is measured directly between the two launchers once a match request
exists, and an incoming request shows the sender's profile, location, mains and that
**measured direct ping** before acceptance. Blocked UDP/NAT produces “Ping unavailable”;
service latency is never substituted for opponent ping. Chat lines carry timestamps;
selecting a chat sender selects them in the roster. Profile options: auto reject
invites, request sound and its volume. Requests expire after 30 seconds and either side can cancel. Acceptance
launches both clients through Slippi Direct. Requests currently require matching
engine/version identifiers; cross-engine gameplay parity is a separate concern.

Lobby chat is limited to 300 characters per message, one message per second. It is
ephemeral (up to 100 messages, at most one hour). Friend requests require acceptance.
Friendships survive launcher restarts. Friends see **Online**, **In game**,
**In match**, or **Offline**, and the friend's own remaining stocks during an online
match. Presence continues outside the public lobby while the launcher is running.
Switching tabs keeps presence running; unchecking **Go Online** hides the public
roster entry while friend presence remains available. Closing the launcher
disconnects presence. Unreachable clients expire from view in 20 seconds. Peer
chat is best-effort and has no synchronized history.

## Peer-to-peer mode: no shared lobby service

Open the launcher, choose a game build, select **Multiplayer Lobby**, fill out
your profile and check **Go Online**. Discovery uses the public BitTorrent DHT.
The launcher announces a versioned rendezvous topic and probes candidates with
signed introductions. Initial discovery can take tens of seconds; a DHT result
is not considered online until the peer answers an encrypted message.

For a LAN or direct test (developer use; the field is hidden in the normal UI),
set `"mode": "peer"` and `"url": "IP:port"` (an already joined player) in
`lobby-profile.json` while the launcher is closed. That address is used for immediate direct contact and as a DHT
seed; no HTTP lobby service is needed. To keep a stable local UDP port, add `"peer_port": 49140`
to `lobby-profile.json` while the launcher is closed. The Windows firewall must
allow the launcher to receive UDP on that port.

The mode stores an Ed25519 signing seed, a separate X25519 key and accepted
friends in `lobby-peer-identity.json`. Keep that file private and back it up;
losing it changes your identity and friend relationships. Peer messages use
X25519-derived authenticated encryption. The publicly exchanged display name,
region and Slippi code remain self-reported. While a friend has the launcher
open but has left the public lobby, both launchers announce a pairwise DHT
topic so their status can still reconnect without a shared server.

The public DHT is existing third-party infrastructure, not a Melee Unlocked
service. There is currently **no relay**, so restrictive NATs/firewalls can
prevent discovery or direct messages, and the roster/chat cannot guarantee
completeness or offline delivery. A manually entered peer address only works
when that address is reachable. Accepted games still use Slippi Direct and its
existing infrastructure. The DHT is never used to carry gameplay.

## Optional hosted service: local test (developer use)

The hosted mode is kept for development and is hidden in the launcher UI. Select it by
setting `"mode": "service"` and `"url"` in `lobby-profile.json` while the launcher is
closed.

Python 3.11 or newer, no third-party Python packages:

```powershell
py lobby/server.py --host 127.0.0.1 --port 8787 --database lobby/lobby.sqlite3
```

Set `"mode": "service"` and `"url": "http://127.0.0.1:8787"` in `lobby-profile.json`,
open the launcher, select the intended game build, then **Multiplayer Lobby**,
fill out the profile and check **Go Online**. Two launchers
need separate install/profile folders and separate Slippi accounts for real online
games. The local tests use loopback peering instead of public matchmaking.

For static recomp, finish the game's first-run save setup with Play before requesting
matches. Until the launcher finds that save and a Slippi login, chat and friends remain available
but match requests are disabled. Reopen/rejoin the lobby after setup.

The lobby's identity is separate from Slippi authentication. No Slippi password,
refresh token, or `user.json` is uploaded to this service. Slippi codes and locations
are player supplied, **not verified identities**. An existing lobby identity reserves
its code. The bearer identity in `lobby-profile.json` is the login for that service;
preserve it across launcher updates and keep it private. Losing it loses access to
that profile. The SQLite database stores identities and social relationships, not
chat history. Back it up and keep it private too.

## Optional hosted service: shared Internet origin

For the no-service design and limitations, see
[DECENTRALIZED_NETWORK.md](DECENTRALIZED_NETWORK.md).

In hosted mode, all users must configure the same reachable HTTPS origin. No public service/domain
is provisioned by this change. `compose.yaml` and `Caddyfile` provide a deployable
single-instance setup: set `LOBBY_DOMAIN` to a DNS name pointing at the host, then run
`docker compose up -d --build` from this folder. Persist the named volumes. Open TCP
80/443 and UDP 8787; internal HTTP is not published. Set `udp_port` to `8787` in each
launcher's `lobby-profile.json` when using the HTTPS origin (the default is the URL's
port). Close/reopen the launcher after manually editing that file.

HTTPS is mandatory except on loopback. WinHTTP validates the server certificate and
does not follow redirects with credentials. UDP registration uses a separate scoped
secret, never the account token. Joining explicitly shares observed IPv4 endpoints
with other joined players for direct probing; endpoints are not shown in chat or
friend lists. Symmetric NAT/firewalls may prevent probing; this does not block a
Slippi match request. This initial service is single-process, with bounded online
roster, request/message sizes and join/chat rates. Public-community deployment should
also provide operator moderation and account recovery.

## Engine integration

Both hosts accept:

```text
--lobby-direct NAME#123 --lobby-character 2 --lobby-status-file <absolute path>
```

The launcher consumes an accepted request once and checks the engine/version again
before launching. It supplies the peer's code, never arbitrary server command-line
arguments. The static recomp uses the existing practice matchmaking coordinator to
enter the normal Slippi online flow. The source port uses its native negotiated-match
boot bridge; lobby launch is distinct from diagnostic input/self-test mode. The native
lobby path is one match, returning to the launcher after the session ends. Its loading
wait has a 90-second deadline. Existing native rollback/playability work remains a
separate subsystem.

The simulation writes a local JSON mailbox once per second. Stock values use the
static build's player state or the source build's `MuStatePod`, respectively. Only the
launcher contacts the selected lobby network. A stale mailbox expires after five seconds.
Accepted requests identify `transport: slippi-direct`; future custom P2P can replace
the launch adapter without changing chat, friends, or consent.

The launcher also keeps **local, unranked win/loss history** in `lobby-history.json`.
The static build records the Slippi game report's winner; the native source build
records its stable Game End placements. Only a normal Game End with one known
winner is counted. Started games that disconnect, have ambiguous results, or
exit early appear as `incomplete` and do not change the W/L total. A game that
never launches is not added to history. The history is
stored on your PC and can be edited, so it is not a verified ranking. The lobby
window shows the total and the 100 newest games, including opponent and date.
The game appends results to a local `.results` mailbox; the launcher records each
one while the game runs, including rematches in a single static recomp session.

## Validation

```powershell
py -m unittest discover -s lobby -v
cmake --build build-sourceport-slippi --config Release --target melee_unlocked port_launcher_lobby_client_test
$env:MELEE_LOBBY_CLIENT_TEST_EXE = (Resolve-Path build-sourceport-slippi/port/Release/port_launcher_lobby_client_test.exe).Path
py -m unittest discover -s lobby -v
```

The native client test starts a temporary HTTP/UDP service and exercises real WinHTTP,
direct peer RTT, chat, friend acceptance, match acceptance and stock presence.

The no-service peer test launches two native peers with private identity folders
and checks chat, friendship, match acceptance, stocks and direct RTT. It also
restarts both peers to check that friends reconnect and see status/stocks while
both are outside the public lobby:

```powershell
cmake --build build-sourceport-slippi --config Release --target port_launcher_lobby_peer_test
$env:MELEE_LOBBY_PEER_TEST_EXE = (Resolve-Path build-sourceport-slippi/port/Release/port_launcher_lobby_peer_test.exe).Path
py -m unittest discover -s lobby -p test_peer_client.py -v
```

Set `MELEE_DHT_TEST_HOST` to this PC's non-loopback LAN address to also run the
DHT-only two-peer test; that test omits the direct seed and waits for DHT
announcement and rediscovery. It does not establish WAN NAT compatibility.

The worker-level test verifies that the real launcher receives an accepted
peer-mode request once and prepares the Slippi Direct handoff:

```powershell
cmake --build build-sourceport-slippi --config Release --target port_launcher_lobby_peer_worker_test
$env:MELEE_LOBBY_PEER_WORKER_TEST_EXE = (Resolve-Path build-sourceport-slippi/port/Release/port_launcher_lobby_peer_worker_test.exe).Path
py -m unittest discover -s lobby -p test_peer_worker.py -v
```

For opt-in game handoff tests, set `MELEE_ISO` and `MELEE_LOBBY_RECOMP_EXE` and/or
`MELEE_LOBBY_SOURCE_EXE` to built executables. Run `test_game_handoff.py` through unittest.
Each test launches two headless loopback peers and requires both to publish a live
four-stock match. Logs are in `reports/lobby-smoke`. These tests do not validate WAN
NAT traversal or a full human-played match.

For static recomp, set `MELEE_LOBBY_TEST_CARD` to an initialized Melee `.gci`; the test
copies it into private test folders, leaving the original untouched. First-run card
dialogs are deliberately not auto-accepted on behalf of the user.
