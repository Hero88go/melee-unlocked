# YouGame social calls for the launcher and online menu

For a launcher or online menu that wants to show the same numbers and friends YouGame shows: who is
online, who is in a match, players waiting in each queue, public rooms, and the player's friends.
The calls go through the `yougame.h` already in `port/third_party/yougame_start/`, so no new code
or library is needed. The full reference is the "Social" section of https://yougame.co/sdk.md.

Status: live on YouGame's staging site, not tested yet. They answer once the YouGame desktop app
build that lets a native program use them is out. Before that, every call answers "not available",
so a menu that uses them should treat that answer as "nothing to show".

## The calls

Every call is `yg_call(type, body_json, timeout_seconds, callback, user)`. The answer is the whole
result as JSON in `result_json`, read with `yg_json_*`.

| `type` | Body | Answer | Sign-in |
|---|---|---|---|
| `socialCounts` | none | `{online, inMatch, inRooms, waiting:{"<mode>:<queue>": n}}` for this game (`inRooms`: players in a live room); refreshed about every 20 s, so poll no faster | not needed |
| `socialRooms` | none | `{rooms:[{code, mode, queue, players, size}]}`: the game's public lobbies with someone seated, busiest first (private friends rooms are never listed). Join one by its `code` (below) | not needed |
| `socialPlayers` | `{"friends":true}` for friends only | `{players:[{username, name, status, friend, rating}]}`, friends first, at most 100; `status` is `online` or `in_match`; `friend` is `you`, `friends`, `requested`, `incoming` or `none`; `rating` is the best ranked rating in this game or null | needed |
| `friendsList` | none | `{friends:[{username, name, online, here, playing}], incoming:[…], outgoing:[…]}` | needed |
| `friendAction` | `{"action":"add","username":"ada"}` (`add`, `accept`, `decline`, `cancel`) | `{state}` (`requested` or `friends`). YouGame asks the player yes/no in its own sheet first; a no fails with code `cancelled` | needed |

```c
static void on_counts(void *user, const yg_result *r) {
  if (!r->ok) return;                       /* not available yet, or no YouGame: show nothing */
  /* read r->result_json with yg_json_*: online, inMatch, waiting */
}

yg_call("socialCounts", NULL, 0, on_counts, NULL);
yg_call("socialPlayers", "{\"friends\":true}", 0, on_players, NULL);
```

## Notes

- Joining a listed public lobby: the same `mpTicket` the game already sends, with the row's lobby in it:
  `yg_call("mpTicket", "{\"room\":\"<code>\",\"players\":<size>,\"mode\":\"<mode>\",\"queue\":\"<queue>\"}", ...)`.
  YouGame checks the code before the player leaves the room they are in.
- The friend actions only ask: the player confirms in YouGame. Outside the YouGame app they fail with
  `not_available` until the app build that supports them.

- The signed-in calls fail with code `sign_in` for a signed-out player. Offer YouGame's sign-in
  there rather than an error.
- Players appear only while they let friends see that they are online; appear-offline and blocks
  hide them. Games get names and usernames only, never account ids, emails or pictures.
- Removing a friend stays on YouGame; a game can add, accept, decline and cancel requests.
