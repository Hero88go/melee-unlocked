#!/usr/bin/env python3
"""Extract the production major-mode runner for the isolated native boundary fixture."""
import argparse
import re
from pathlib import Path


def extract(text):
    start = re.search(r"^u8 runGameMode\(u8 mode_kind\)\s*\{", text, re.MULTILINE)
    if not start:
        raise ValueError("production runGameMode definition not found")
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
    raise ValueError("production runGameMode has unbalanced braces")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    body = extract(args.source.read_text(encoding="utf-8"))
    args.output.write_text("/* Generated from the production gm_1A3F.c; do not edit. */\n" + body,
                           encoding="utf-8")


if __name__ == "__main__":
    main()
