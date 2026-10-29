#!/data/data/com.termux/files/usr/bin/bash
# Non-invasive GPU-development tests. Never touches active :0 or :90.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$ROOT"
bash graphics/tools/test-capabilities.sh
bash graphics/tools/test-host-overlay.sh
bash graphics/tools/test-ahb-protocol.sh
if [[ -s .source/gpu-audit/termux-x11-protected.sha256 ]]; then
    bash graphics/tools/snapshot-termux-x11.sh verify
fi
echo 'PASS: all non-invasive Stage-1 graphics contracts'
