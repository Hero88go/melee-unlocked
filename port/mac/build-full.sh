#!/bin/bash
# M3 exception: Apple clang only, at most two compile jobs; no dependency/game builds.
set -euo pipefail
cd "$(dirname "$0")/../.."
exec python3 -B port/mac/build_full.py "$@"
