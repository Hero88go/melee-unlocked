"""Post release notes to a Discord channel through a webhook.

The webhook URL is a secret: anyone holding it can post to that channel as the app. It is read from
the environment or from a file that git ignores, never from a committed source file.

Set it up once:
  1. In Discord: right click the #updates channel, Edit Channel, Integrations, Webhooks,
     New Webhook, Copy Webhook URL.
  2. Save it as `discord-webhook.txt` beside this checkout (gitignored), or set MELEE_DISCORD_WEBHOOK.

Usage:
  python tools/post_discord.py --version 0.1.15                 # show what would be posted
  python tools/post_discord.py --version 0.1.15 --send          # actually post it
  python tools/post_discord.py --notes release/NOTES.md --send  # post an arbitrary file

Nothing is posted without --send, so the message can always be read first.
"""
import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DISCORD_LIMIT = 2000   # characters per message; longer notes are split on blank lines


def read_webhook(explicit=None):
    if explicit:
        return explicit.strip()
    env = os.environ.get("MELEE_DISCORD_WEBHOOK")
    if env:
        return env.strip()
    for name in ("discord-webhook.txt", ".discord-webhook"):
        path = ROOT / name
        if path.exists():
            return path.read_text(encoding="utf-8").strip()
    raise SystemExit(
        "no webhook configured. Put the URL in discord-webhook.txt at the root of the checkout,\n"
        "set MELEE_DISCORD_WEBHOOK, or pass --webhook. Create one in Discord under\n"
        "Edit Channel > Integrations > Webhooks > New Webhook > Copy Webhook URL."
    )


def notes_for_version(version):
    """Release notes for a version: the release/ file if there is one, else the GitHub release body."""
    for candidate in (ROOT / "release" / ("RELEASE_NOTES_%s.md" % version),
                      ROOT / "release" / ("RELEASE_NOTES_%s-beta.md" % version)):
        if candidate.exists():
            return candidate.read_text(encoding="utf-8")
    return None


def to_discord(text, version, repo="Hero88go/melee-unlocked"):
    """Markdown that reads well in Discord: drop the H1, keep headings bold, keep bullets."""
    lines = []
    for raw in text.splitlines():
        line = raw.rstrip()
        if line.startswith("# "):
            continue                                   # the title is carried by the release link
        if line.startswith("## "):
            lines.append("")
            lines.append("**%s**" % line[3:].strip())
            continue
        if line.startswith("### "):
            lines.append("**%s**" % line[4:].strip())
            continue
        lines.append(line)
    body = "\n".join(lines).strip()
    body = re.sub(r"\n{3,}", "\n\n", body)
    header = "**Melee Unlocked %s is out**\nhttps://github.com/%s/releases/tag/v%s\n" % (version, repo, version)
    return header + "\n" + body


def split_message(text, limit=DISCORD_LIMIT):
    """Split on blank lines so a message never breaks mid sentence."""
    chunks, current = [], ""
    for block in text.split("\n\n"):
        piece = block if not current else current + "\n\n" + block
        if len(piece) <= limit:
            current = piece
            continue
        if current:
            chunks.append(current)
        while len(block) > limit:                      # a single huge block: split on lines
            cut = block.rfind("\n", 0, limit)
            if cut <= 0:
                cut = limit
            chunks.append(block[:cut])
            block = block[cut:].lstrip("\n")
        current = block
    if current:
        chunks.append(current)
    return chunks


def post(webhook, content):
    data = json.dumps({"content": content, "allowed_mentions": {"parse": []}}).encode("utf-8")
    request = urllib.request.Request(webhook, data=data,
                                     headers={"Content-Type": "application/json",
                                              "User-Agent": "melee-unlocked-release-notes"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.status


def main():
    ap = argparse.ArgumentParser(description="Post release notes to Discord through a webhook.")
    ap.add_argument("--version", help="release version, e.g. 0.1.15 (reads release/RELEASE_NOTES_<v>.md)")
    ap.add_argument("--notes", type=Path, help="post this file instead of a version's notes")
    ap.add_argument("--webhook", help="webhook URL (default: discord-webhook.txt or MELEE_DISCORD_WEBHOOK)")
    ap.add_argument("--send", action="store_true", help="actually post; without it the message is only printed")
    args = ap.parse_args()

    if args.notes:
        text = args.notes.read_text(encoding="utf-8")
        version = args.version or (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    else:
        version = args.version or (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        text = notes_for_version(version)
        if text is None:
            raise SystemExit("no notes found for %s. Write release/RELEASE_NOTES_%s.md or pass --notes."
                             % (version, version))

    message = to_discord(text, version)
    chunks = split_message(message)

    print("=" * 72)
    for i, chunk in enumerate(chunks, 1):
        print(chunk)
        if i != len(chunks):
            print("-" * 24 + " message %d/%d ends here " % (i, len(chunks)) + "-" * 24)
    print("=" * 72)
    print("%d message(s), %d characters total." % (len(chunks), len(message)))

    if not args.send:
        print("\nNothing was posted. Re-run with --send to post it.")
        return 0

    webhook = read_webhook(args.webhook)
    for i, chunk in enumerate(chunks, 1):
        try:
            status = post(webhook, chunk)
        except urllib.error.HTTPError as e:
            print("failed on message %d/%d: HTTP %s %s" % (i, len(chunks), e.code, e.read()[:200]), file=sys.stderr)
            return 1
        except urllib.error.URLError as e:
            print("failed on message %d/%d: %s" % (i, len(chunks), e.reason), file=sys.stderr)
            return 1
        print("posted message %d/%d (HTTP %s)" % (i, len(chunks), status))
    return 0


if __name__ == "__main__":
    sys.exit(main())
