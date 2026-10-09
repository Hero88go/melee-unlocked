"""Download new crash and log reports from the relay (tools/crash_relay) to a local folder.

Usage: py -3.12 tools/pull_reports.py [--out C:/Games/reports] [--all] [--limit N]

Each report lands in <out>/<id>/report.md with its relay metadata in meta.json, plus session.trace
when the player sent one. Messages from #bug-reports, #crash-reports and #help-me land in
<out>/discord-<channel>/<time>-<id>/ (never more than --days back, default 1) as
message.md, message.json (author, reply target, webhook) and their attachments. The newest key
pulled is remembered in <out>/.last_key, so the next run only fetches what came in since.
The admin token is read from MELEE_REPORTS_TOKEN or ~/.melee_reports_token (never from the repo).
"""
from pathlib import Path
import argparse
import json
import os
import sys
import time
import urllib.parse
import urllib.request

RELAY = 'https://melee-crash-relay.firescribe-share-worker.workers.dev'


def token():
    value = os.environ.get('MELEE_REPORTS_TOKEN')
    if value:
        return value.strip()
    path = Path.home() / '.melee_reports_token'
    if path.exists():
        return path.read_text().strip()
    sys.exit('no admin token: set MELEE_REPORTS_TOKEN or write ~/.melee_reports_token')


def fetch(path, tok):
    request = urllib.request.Request(RELAY + path, headers={'Authorization': 'Bearer ' + tok,
                                                            'User-Agent': 'pull_reports'})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()


# Where players report problems. Nothing older than --days (default 1) is pulled, even on a first
# pull or after a long break: the loop runs several times a day, and old reports are backlog.
CHANNELS = [('1548156602945773589', 'bug-reports'), ('1555380854132310046', 'crash-reports'),
            ('1548170633295167599', 'help-me')]
LOOKBACK_DAYS = 1
DISCORD_EPOCH_MS = 1420070400000
BOT_TOKEN_FILE = Path(__file__).resolve().parents[2] / 'melee-nightwatch/secrets/bot-token.txt'


def bot_token():
    value = os.environ.get('MELEE_DISCORD_BOT_TOKEN')
    if value:
        return value.strip()
    return BOT_TOKEN_FILE.read_text().strip() if BOT_TOKEN_FILE.exists() else None


def discord(path, tok):
    request = urllib.request.Request('https://discord.com/api/v10' + path,
                                     headers={'Authorization': 'Bot ' + tok, 'User-Agent': 'DiscordBot (pull_reports, 1)'})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.loads(response.read())


def pull_channel(out, fresh, channel_id, label, days):
    """New messages of one channel, oldest first, each in its own folder with its attachments."""
    tok = bot_token()
    if not tok:
        print(f'#{label}: skipped (no bot token)')
        return
    folder = out / f'discord-{label}'
    folder.mkdir(parents=True, exist_ok=True)
    state = folder / '.last_id'
    floor = (int((time.time() - days * 86400) * 1000) - DISCORD_EPOCH_MS) << 22
    bookmark = int(state.read_text().strip() or 0) if state.exists() and not fresh else 0
    after = str(max(floor, bookmark))
    messages = []
    while True:
        query = '?limit=100' + ('&after=' + after if after else '')
        page = discord(f'/channels/{channel_id}/messages' + query, tok)
        if not page:
            break
        page.sort(key=lambda m: int(m['id']))
        messages += page
        after = page[-1]['id']
        if len(page) < 100 or len(messages) >= 1000:
            break
    if not messages:
        print(f'#{label}: no new messages')
        return
    for m in messages:
        target = folder / f"{m['timestamp'][:19].replace(':', '-')}-{m['id']}"
        target.mkdir(exist_ok=True)
        name = m['author'].get('global_name') or m['author']['username']
        text = f"# #{label} message from {name}\n\nSent {m['timestamp']}\n\n{m.get('content', '')}\n"
        for a in m.get('attachments', []):
            safe = ''.join(c for c in a['filename'] if c.isalnum() or c in '._-')[:120] or 'file'
            try:
                with urllib.request.urlopen(urllib.request.Request(a['url'], headers={'User-Agent': 'pull_reports'}), timeout=120) as r:
                    (target / safe).write_bytes(r.read())
                text += f'\nAttachment: {safe}\n'
            except Exception as error:   # an expired or removed attachment does not stop the pull
                text += f'\nAttachment {safe} could not be downloaded: {error}\n'
        (target / 'message.md').write_text(text, encoding='utf-8')
        # Who sent it and what it answers, for tools that sort reports from replies and webhook copies.
        (target / 'message.json').write_text(json.dumps({
            'id': m['id'], 'timestamp': m['timestamp'], 'author_id': m['author']['id'], 'author': name,
            'bot': bool(m['author'].get('bot')), 'webhook_id': m.get('webhook_id'),
            'reply_to': (m.get('message_reference') or {}).get('message_id'),
            'attachments': [a['filename'] for a in m.get('attachments', [])]}, indent=1), encoding='utf-8')
        print(f"#{label}  {m['timestamp'][:16]}  {name[:20]:20}  {(m.get('content') or '').replace(chr(10), ' ')[:90]}"
              f"{'  [' + str(len(m['attachments'])) + ' file(s)]' if m.get('attachments') else ''}")
        state.write_text(m['id'])
    print(f'{len(messages)} #{label} message(s) in {folder}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', type=Path, default=Path('C:/Games/reports'))
    ap.add_argument('--all', action='store_true', help='ignore the remembered position')
    ap.add_argument('--limit', type=int, default=200)
    ap.add_argument('--days', type=float, default=LOOKBACK_DAYS, help='never pull anything older (default 1)')
    ap.add_argument('--no-discord', action='store_true', help='skip the Discord channels')
    args = ap.parse_args()
    if not args.no_discord:
        for channel_id, label in CHANNELS:
            pull_channel(args.out, args.all, channel_id, label, args.days)
    tok = token()
    args.out.mkdir(parents=True, exist_ok=True)
    state = args.out / '.last_key'
    # Relay keys start with the upload time, so the lookback floor is a key too.
    floor = 'r:' + time.strftime('%Y-%m-%dT%H-%M-%S', time.gmtime(time.time() - args.days * 86400))
    after = max(floor, '' if args.all or not state.exists() else state.read_text().strip())
    items = json.loads(fetch('/admin/list?after=' + urllib.parse.quote(after), tok))['items'][:args.limit]
    if not items:
        print('no new reports')
        return 0
    for item in items:
        key, meta = item['key'], item.get('meta', {})
        folder = args.out / key[2:]
        folder.mkdir(exist_ok=True)
        q = urllib.parse.quote(key)
        (folder / 'report.md').write_bytes(fetch('/admin/get?key=' + q, tok))
        # meta.version is the launcher's version; the crashed game's version is in the crash line.
        (folder / 'meta.json').write_text(json.dumps({'key': key, **meta}, indent=1), encoding='utf-8')
        if meta.get('trace'):
            trace = fetch('/admin/trace?key=' + q, tok)
            (folder / 'session.trace').write_bytes(trace)
            # Send logs puts every marked match into one trace, each after a "# match <name>" line.
            for part in trace.decode('utf-8', 'replace').split('# match ')[1:]:
                name, _, rows = part.partition('\n')
                safe = ''.join(c for c in name.strip() if c.isalnum() or c in '._-')[:80] or 'match.trace'
                (folder / ('match-' + safe)).write_text(rows, encoding='utf-8')
        note = (meta.get('note') or '').replace('\n', ' ')[:120]
        print(f"{key[2:]}  {meta.get('kind', '?'):5}  {meta.get('version', '?'):8}  {meta.get('engine', '?'):13}  "
              f"{(meta.get('where') or '')[:70]}{'  | ' + note if note else ''}")
        state.write_text(key)
    print(f'{len(items)} report(s) in {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
