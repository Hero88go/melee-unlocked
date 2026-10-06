#!/usr/bin/env python3
"""Extract the production major-mode runner for the isolated native boundary fixture."""
import argparse
import re
from pathlib import Path


def extract(text, name="runGameMode"):
    start = re.search(r"^(?:static\s+)?(?:inline\s+)?\w+\s+" + re.escape(name) +
                      r"\([^\n]*\)\s*\{", text, re.MULTILINE)
    if not start:
        raise ValueError("production " + name + " definition not found")
    # Remove comments and literals only for balancing braces; preserve the original bytes for C.
    tokens = re.compile(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]')
    depth = 0
    for token in tokens.finditer(text, start.end() - 1):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return text[start.start():token.end()] + "\n"
    raise ValueError("production " + name + " has unbalanced braces")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--function", default="runGameMode")
    args = parser.parse_args()
    body = extract(args.source.read_text(encoding="utf-8"), args.function)
    args.output.write_text("/* Generated from production " + args.source.name + "; do not edit. */\n" + body,
                           encoding="utf-8")


if __name__ == "__main__":
    main()
