#!/bin/bash
# Explicit app-only write; keeps one ROM connection through verification.
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/env.sh
python tools/flash_app.py flash "$@"
