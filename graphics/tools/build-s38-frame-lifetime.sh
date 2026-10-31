#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
exec python3 "$ROOT/graphics/tools/relink-s38-frame-lifetime.py"
