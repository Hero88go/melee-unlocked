# Crash report relay

Forwards crash report zips from the launcher to a private Discord channel. The webhook is never in
the launcher.

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
