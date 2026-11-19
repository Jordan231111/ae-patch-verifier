#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
wheels="$root/.github/play-client"
python3 -m pip install --disable-pip-version-check --quiet --no-compile --no-index --no-deps \
  --require-hashes --find-links "$wheels" -r "$wheels/requirements.txt"
