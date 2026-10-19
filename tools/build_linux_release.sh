#!/bin/bash
# Builds the native Linux game, Source Port and launcher from this tree and packages
# release/MeleeUnlocked-<version>-linux-x86_64.tar.gz. Run under Linux or WSL (Ubuntu 24.04):
#   bash tools/build_linux_release.sh
# publish_release.ps1 runs it before every publish, so each release carries the Linux package.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tools/build_linux.py
python3 tools/package_linux_release.py
