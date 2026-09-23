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
  python tools/post_discord.py --last 3                         # preview the latest 3 releases
  python tools/post_discord.py --last 3 --send                  # post the latest 3 releases
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
INSTALL_BLOCK = """**Install**

- Choose one archive from the linked GitHub release: `MeleeUnlocked-{version}-win64.zip` is the standard build; `MeleeUnlocked-{version}-DLSS5-Experimental.zip` is experimental and requires an RTX 50-series GPU or newer plus NVIDIA's DLSS 5 file (not included).
- Close Melee Unlocked, then extract the chosen archive over the existing folder so settings, saves and replays carry over.
- Keep your own Melee NTSC 1.02 ISO beside the files as `melee.iso`, or select it with the included launcher.
- Start `MeleeUnlockedLauncher.exe` after extraction."""


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
    for candidate in (ROOT / ("RELEASE_NOTES_%s.md" % version),
                      ROOT / "release" / ("RELEASE_NOTES_%s.md" % version),
                      ROOT / "release" / ("RELEASE_NOTES_%s-beta.md" % version)):
        if candidate.exists():
            return candidate.read_text(encoding="utf-8")
    return None


def to_discord(text, version, repo="Hero88go/melee-unlocked", release_url=None):
    """Markdown that reads well in Discord.

    The notes are hard wrapped for reading as a file, but Discord treats every newline as a real
    line break, so a wrapped paragraph arrives broken at every wrap point. Paragraphs and list
    items are unwrapped back into single lines and Discord is left to wrap them to the reader's
    window. The release link goes last so its preview card lands at the end instead of splitting
    the post in half.
    """
    blocks, para = [], []
    # Sections the GitHub release keeps but the announcement does not want. Install instructions
    # belong on the release page people land on, not in a chat message.
    skip_sections = {"install"}
    skipping = False
    title = ""

    def flush():
        if para:
            blocks.append(" ".join(para))
            del para[:]

    for raw in text.splitlines():
        line = raw.rstrip()
        stripped = line.strip()
        if not stripped:
            flush()
            blocks.append("")
            continue
        if stripped.startswith("# "):
            flush()
            title = stripped[2:].strip()               # carried by the header below
            continue
        if stripped.startswith("## "):
            flush()
            heading = stripped[3:].strip()
            skipping = heading.lower() in skip_sections
            if not skipping:
                blocks.append("**%s**" % heading)
            continue
        if skipping:
            continue
        if stripped.startswith("### "):
            flush()
            blocks.append("**%s**" % stripped[4:].strip())
            continue
        if stripped.startswith("- ") or stripped.startswith("* "):
            flush()
            blocks.append("- " + stripped[2:].strip())
            continue
        # An indented line under a bullet continues that bullet rather than starting a paragraph.
        if line[:1].isspace() and blocks and blocks[-1].startswith("- ") and not para:
            blocks[-1] += " " + stripped
            continue
        para.append(stripped)
    flush()

    body = re.sub(r"\n{3,}", "\n\n", "\n".join(blocks)).strip()
    # A notes title with more than the version ("0.6.0: DLSS5 update, ...") is the headline.
    header = ("**%s**\n\n" % title) if ":" in title else "**Melee Unlocked %s is out**\n\n" % version
    link = ("\n\n" + release_url) if release_url else ("\n\nhttps://github.com/%s/releases/tag/v%s" % (repo, version))
    return header + INSTALL_BLOCK.format(version=version) + "\n\n" + body + link


def latest_releases(repo, count):
    """Return the newest published GitHub releases, newest first."""
    if count <= 0:
        raise SystemExit("--last must be greater than zero")
    url = "https://api.github.com/repos/%s/releases?per_page=%d" % (repo, max(count, 1))
    headers = {"Accept": "application/vnd.github+json", "User-Agent": "melee-unlocked-release-notes"}
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        headers["Authorization"] = "Bearer " + token
    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            releases = json.loads(response.read().decode("utf-8"))
    except (urllib.error.HTTPError, urllib.error.URLError) as error:
        raise SystemExit("could not read GitHub releases: %s" % error)
    published = [r for r in releases if isinstance(r, dict) and not r.get("draft")]
    if len(published) < count:
        raise SystemExit("GitHub returned only %d published release(s); need %d" % (len(published), count))
    return published[:count]


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
    # A heading alone at the end of a message, with its text in the next one, reads as a mistake.
    # Move a trailing heading down to the message it introduces.
    for i in range(len(chunks) - 1):
        lines = chunks[i].rstrip().split("\n")
        last = lines[-1].strip() if lines else ""
        if len(lines) > 1 and last.startswith("**") and last.endswith("**") and len(last) < 60:
            chunks[i] = "\n".join(lines[:-1]).rstrip()
            chunks[i + 1] = last + "\n\n" + chunks[i + 1]
    return chunks


def post(webhook, content):
    """Posts one message and returns (status, message id).

    `wait=true` makes Discord return the created message, which is the only way to learn its id.
    Without the id a posted message cannot later be edited or deleted, which is what made the
    first badly formatted announcement impossible to clean up automatically.
    """
    url = webhook + ("&" if "?" in webhook else "?") + "wait=true"
    data = json.dumps({"content": content, "allowed_mentions": {"parse": []}}).encode("utf-8")
    request = urllib.request.Request(url, data=data,
                                     headers={"Content-Type": "application/json",
                                              "User-Agent": "melee-unlocked-release-notes"})
    with urllib.request.urlopen(request, timeout=30) as response:
        body = response.read()
        message_id = ""
        try:
            message_id = json.loads(body.decode("utf-8")).get("id", "")
        except Exception:
            pass
        return response.status, message_id


def delete(webhook, message_id):
    request = urllib.request.Request(webhook + "/messages/" + message_id, method="DELETE",
                                     headers={"User-Agent": "melee-unlocked-release-notes"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.status


def main():
    ap = argparse.ArgumentParser(description="Post release notes to Discord through a webhook.")
    ap.add_argument("--version", help="release version, e.g. 0.1.15 (reads release/RELEASE_NOTES_<v>.md)")
    ap.add_argument("--last", type=int, metavar="N", help="use the N latest published GitHub releases")
    ap.add_argument("--notes", type=Path, help="post this file instead of a version's notes")
    ap.add_argument("--repo", default="Hero88go/melee-unlocked", help="GitHub repository for --last")
    ap.add_argument("--webhook", help="webhook URL (default: discord-webhook.txt or MELEE_DISCORD_WEBHOOK)")
    ap.add_argument("--send", action="store_true", help="actually post; without it the message is only printed")
    ap.add_argument("--delete", metavar="VERSION", help="delete the messages posted for VERSION and stop")
    ap.add_argument("--delete-ids", help="delete comma-separated webhook message IDs and stop")
    args = ap.parse_args()

    if args.last and (args.version or args.notes):
        ap.error("--last cannot be combined with --version or --notes")

    if args.delete and args.delete_ids:
        ap.error("--delete and --delete-ids cannot be combined")

    if args.delete_ids:
        webhook = read_webhook(args.webhook)
        for message_id in (item.strip() for item in args.delete_ids.split(",")):
            if message_id:
                try:
                    print("deleted %s (HTTP %s)" % (message_id, delete(webhook, message_id)))
                except urllib.error.HTTPError as e:
                    print("could not delete %s: HTTP %s" % (message_id, e.code), file=sys.stderr)
                    return 1
        return 0

    if args.delete:
        record = ROOT / "release" / ("discord-posted-%s.txt" % args.delete)
        if not record.exists():
            raise SystemExit("no record of posted messages for %s (%s)" % (args.delete, record))
        webhook = read_webhook(args.webhook)
        ids = [i.strip() for i in record.read_text(encoding="utf-8").splitlines() if i.strip()]
        for message_id in ids:
            try:
                print("deleted %s (HTTP %s)" % (message_id, delete(webhook, message_id)))
            except urllib.error.HTTPError as e:
                print("could not delete %s: HTTP %s" % (message_id, e.code), file=sys.stderr)
        record.unlink()
        return 0

    if args.last:
        messages = []
        for release in latest_releases(args.repo, args.last):
            tag = release.get("tag_name", "").strip()
            version = tag[1:] if tag.startswith("v") else tag
            if not version:
                raise SystemExit("a GitHub release had no tag_name")
            title = release.get("name") or tag
            body = release.get("body") or "No release notes were provided."
            text = "# %s\n\n%s" % (title, body)
            messages.extend(split_message(to_discord(text, version, args.repo,
                                                      release.get("html_url"))))
        chunks = messages
        version = "last%d" % args.last
        message = "\n\n".join(chunks)
    elif args.notes:
        text = args.notes.read_text(encoding="utf-8")
        version = args.version or (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        message = to_discord(text, version)
        chunks = split_message(message)
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
    posted = []
    for i, chunk in enumerate(chunks, 1):
        try:
            status, message_id = post(webhook, chunk)
        except urllib.error.HTTPError as e:
            print("failed on message %d/%d: HTTP %s %s" % (i, len(chunks), e.code, e.read()[:200]), file=sys.stderr)
            return 1
        except urllib.error.URLError as e:
            print("failed on message %d/%d: %s" % (i, len(chunks), e.reason), file=sys.stderr)
            return 1
        posted.append(message_id)
        print("posted message %d/%d (HTTP %s, id %s)" % (i, len(chunks), status, message_id or "unknown"))
    # Remember the ids so a mis-formatted announcement can be removed without hunting in Discord.
    if posted:
        record = ROOT / "release" / ("discord-posted-%s.txt" % version)
        record.parent.mkdir(parents=True, exist_ok=True)
        record.write_text("\n".join(i for i in posted if i) + "\n", encoding="utf-8")
        print("message ids saved to %s (delete them with --delete %s)" % (record, version))
    return 0


if __name__ == "__main__":
    sys.exit(main())
