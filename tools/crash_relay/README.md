# Crash report relay

Forwards crash report ZIPs from the launcher to a private Discord channel. The webhook is never in
the launcher. The source now attaches `crash-<id>.md` beside a scrubbed text-only `crash-<id>.zip`, so a
report can be attached directly to an assistant or issue. Both files use the same report id.

This relay update is **pending deployment**. The deployed 2026-09-30 Worker described below still
sends only the ZIP until the updated source is deployed. No real or test report was sent while
implementing or validating this change.

Old and new launchers use the same existing `application/zip` upload and headers. The relay reads
only the exact basenames `melee_port_crash.txt`, `melee_port.log` and `lobby.log`, scrubs them line
by line, then builds a fresh stored ZIP with new checksums. Markdown uses the same scrubbed text.
**The incoming ZIP is never forwarded. Binary minidumps are always dropped.** Raw diagnostics
remain on the player's computer; settings and unknown file payloads are excluded.

The privacy policy is "scrub and keep": a log line stays in the report unless it is private, and
personal details inside a kept line are removed. `src/privacy.js` does this, with the same rules as
the launcher (`port/app/launcher_crash_privacy.h`) and `tools/crash_report_privacy.py`. All three
are checked against the shared vectors in `test/privacy_vectors.json`. Per line, in order:

1. Control characters other than tab are removed. A line longer than 4096 characters is omitted.
2. Private categories are omitted whole: Slippi login, matchmaking and peer lines, Discord, lobby
   and address lines, and any line mentioning `displayName`, `connectCode`, `playKey`, `"uid"`,
   `password`, `token`, `secret` or `authorization`.
3. An absolute file location (drive root or UNC root) is cut through the last separator on the
   line, so only the final component remains. Relative paths such as `Mods\Discs\Pack.iso` stay.
   A line with text containing separators after the path loses that text too.
4. Connect codes become `[code]`, IPv4 addresses with an optional port become `[ip]`, email
   addresses become `[email]`.
5. User folder names found anywhere in the report (`Users\<name>\`, `/home/<name>/`) become
   `[user]` wherever they stand alone, in any case. The launcher adds the Windows user name and
   computer name. A name inside a longer identifier (an engine symbol) is left alone, and names
   shorter than 3 characters are only removed as part of a path.
6. A line that still shows a drive or UNC root, `Users\`, `/home/` or `AppData` is omitted.

Everything else is kept as written: `FATAL:` and `CRASH:` lines, stack lines, guest function
addresses and symbols, mod, graphics, audio, controller and scene lines. `lobby.log` is never
copied, only counted. Each file reports how many lines were omitted. When a byte cap cuts the start
of a file, the first (partial) line is omitted. Known limits: a `scheme://` web address is treated
as a drive root and loses everything up to its last separator, and a person's name that appears
only as free text, never as a user folder, cannot be recognized.
Metadata headers follow the same policy: `X-MU-Crash` goes through the line scrubber before it
becomes the visible message text, and version and engine must match their fixed forms.

The parser bounds the end directory, central records, local headers and payloads, checks matching
headers and text CRCs, and rejects paths, case aliases/duplicates, overlapping regions, encryption,
compression, ZIP64 and data descriptors. Malformed or unsupported archives and archives without
supported text return HTTP 415; no attachment or webhook request is made. The launcher's current
archives use ZIP_STORED.

Readable text coalesces Windows CR runs, removes terminal controls and escapes backtick fences. It
keeps the newest input bytes, with UTF-8 output caps of 256 KiB for crash text, 512 KiB for the game
log and 128 KiB for the lobby log, applied after the fence escape and cut on a line boundary. The
complete Markdown is limited to 1 MiB, including headings and fences. The quoted text is explicitly
identified as diagnostic data.

Run local validation with `npm test` from this directory, or
`node --test tools/crash_relay/test/crash_markdown.test.js` from the repository root. Tests create
synthetic archives in memory and mock the webhook. When the local
`run-source/crash-1790846146111` fixture exists, it is packaged in memory for a compatibility test;
the fixture is not copied into the tests or uploaded. All 21 Node tests passed on 2026-10-01 with
the scrub and keep policy, including the shared vectors, planted private details in ZIP text,
minidumps and headers, plus a mocked outbound upload that checks both attachments and Discord
metadata. The earlier allowlist policy, its test log and its fixture sample under
`run-source/rel09-crash-relay-20261001/` are obsolete, as is the raw-ZIP forwarding implementation
before it.

Deploy (once):

1. `cd tools/crash_relay`
2. `npx wrangler kv namespace create RATE`, then put the printed id in `wrangler.toml`.
3. `npx wrangler secret put DISCORD_WEBHOOK_URL` and paste the webhook URL.
4. `npx wrangler deploy`. Put the printed `https://melee-crash-relay.<account>.workers.dev/report`
   URL into `kCrashRelayUrl` in `port/app/launcher_crash.inl`.

Limits: zip only, 8 MB, one report per IP per 10 minutes, 50 per day.

Deployed 2026-09-30 as `melee-crash-relay` (KV namespace RATE 36cc7f4af26b4a8abd15d99aa824aae9, preview URLs off):
https://melee-crash-relay.firescribe-share-worker.workers.dev/report. Checked: GET 405, wrong type 415,
a marked test report 200, an immediate second report 429.
