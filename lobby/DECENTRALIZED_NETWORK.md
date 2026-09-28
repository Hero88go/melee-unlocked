# Lobby without an operator-run server

The older `DECENTRALIZED MULTIPLAYER NETWORK (2).md` is a design proposal, not
an implementation requirement. Its DHT discovery, signed identity and peer
messaging ideas apply to this lobby. Its ranked receipts, distributed ratings,
blind pairing and replacement gameplay transport are separate future projects.
Accepted matches currently use Slippi Direct.

## Decision

The lobby can avoid a server **operated by Melee Unlocked**, but an Internet-wide
public roster, chat and invitations cannot be made reliably with only two
arbitrary Windows clients. Peers need a way to discover each other, and some
networks cannot receive unsolicited UDP. Any Internet-wide design relies on
existing bootstrap infrastructure and, for some users, relays. The practical
serverless approach is **DHT rendezvous + direct encrypted peer messaging**,
with LAN/manual bootstrap as a fallback. The launcher now offers this as an
optional mode. Community relays are not implemented, so it should be tested on
real home, campus and mobile networks before promising universal reachability.

| Approach | Your own service | Public roster/chat | Main limitation |
| --- | --- | --- | --- |
| Local network discovery (mDNS) | None | Same LAN only | No Internet discovery |
| Manual Slippi code / direct IP | None | No | Already needs another way to contact the player |
| Mainline DHT + peer overlay | None | Possible, best effort | Bootstrap, NAT, privacy and availability |
| Community-operated messaging relays | None | Possible | Relays are still servers with policies and outages |
| Managed realtime service such as Convex | No infrastructure to operate | Yes | Central provider and usage limits |
| Existing self-hosted lobby service | Yes | Yes | Hosting and moderation required |

Convex is a workable *managed* option for a small beta, but it is still a
central service. The free plan currently includes 1 million function calls per
month. The current launcher polls its HTTP service every 800 ms while joined;
that would be about 3.24 million calls in a 30-day month for **one** continuously
connected player, before writes. A Convex version would need reactive
subscriptions and much less frequent presence writes, not a URL change.

## Current peer mode and remaining work

1. The launcher generates a persistent Ed25519 signing identity and a separate
   X25519 key locally. Display name, Slippi code, region and mains are signed
   self-reported claims; a Slippi code does not authenticate a person. Accepted
   friends are pinned to public keys in the local identity file.
2. Joining announces an IPv4 UDP endpoint under one versioned public DHT topic.
   Accepted friends also announce under pairwise topics while the launcher is
   open, even after leaving the public lobby. DHT results are candidate
   endpoints only. A manually supplied reachable peer address connects faster.
3. Peers exchange signed introductions containing public profiles, then use
   X25519-derived authenticated encryption for chat, presence, stock counts,
   friend actions and match requests. The initial profile introduction is
   public to a contacting peer. Messages are bounded and replay IDs expire.
4. The roster contains peers verified by an encrypted response in the last 20
   seconds. Chat fans out to currently connected peers and has no guaranteed
   history or delivery. Friendships persist locally on both clients. Requests
   require explicit acceptance and use acknowledgements and retries before the
   Slippi Direct handoff.
5. IPv6, stronger NAT traversal, optional relays, larger-population gossip and
   community moderation remain future work. Gameplay still uses Slippi Direct.

For a reliable global roster or synchronized chat history, a small managed
service remains much simpler. Even a DHT overlay cannot force delivery to a
friend behind symmetric NAT when neither side nor a relay is reachable. Public
relay participation also needs abuse limits and moderation decisions. The peer
mode should not be advertised as an exhaustive or always-reachable global roster.

## Probe and acceptance gates

`port_lobby_dht_probe` is a standalone Windows diagnostic linked against the
MIT-licensed `jech/dht` implementation. It only tests BEP 5 peer discovery;
it currently probes IPv4 only, does not exchange profiles or lobby messages,
and is not an end-user mode.
Build it with:

```powershell
cmake --build build-sourceport-slippi --config Release --target port_lobby_dht_probe
```

Run two instances with distinct ports and each other's reachable LAN address
as `--bootstrap host:port`; add `--announce` to both and `--seconds 45`.
Loopback addresses are rejected by this DHT implementation. A public test
should use established DHT bootstrap hosts and assess endpoint discovery and
stale results over multiple networks. The probe does not open firewall ports or
prove NAT traversal. Before treating the launcher mode as reliable across the
Internet, test two-home-network discovery, common NAT types, bounded fanout
under a populated lobby and a relay fallback.

The opt-in LAN smoke runs the two nodes and asserts one discovers the other's
endpoint. This passed on one Windows machine using its LAN interface; it does
not exercise two NATs or Internet bootstrap:

```powershell
$env:MELEE_DHT_PROBE_EXE = (Resolve-Path build-sourceport-slippi/port/Release/port_lobby_dht_probe.exe).Path
$env:MELEE_DHT_TEST_HOST = '192.168.0.10' # use this machine's real LAN address
py -m unittest discover -s lobby -p test_dht_probe.py -v
```

## References

- [BEP 5: DHT protocol](https://www.bittorrent.org/beps/bep_0005.html)
- [BEP 44: signed mutable DHT items](https://www.bittorrent.org/beps/bep_0044.html)
- [RFC 8445: ICE connectivity checks](https://datatracker.ietf.org/doc/html/rfc8445)
- [RFC 8656: TURN relay protocol](https://datatracker.ietf.org/doc/html/rfc8656)
- [Convex free-plan limits](https://www.convex.dev/pricing)
- [jech/dht source and license](https://github.com/jech/dht)
