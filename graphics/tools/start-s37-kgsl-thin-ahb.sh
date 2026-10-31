#!/data/data/com.termux/files/usr/bin/bash
# S37 explicit native Termux entrypoint. S36 is unchanged; only the
# private project XFCE is restarted after --restart, never automatically.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
case "${1:-}" in
    --restart) mode=--s37-thin-ahb ;;
    --restore-s36) mode=--restore-s36 ;;
    --restore-s32) mode=--restore-s32 ;;
    --restore-s30) mode=--restore-s30 ;;
    *) echo 'Usage: bash graphics/tools/start-s37-kgsl-thin-ahb.sh --restart|--restore-s36|--restore-s32|--restore-s30 [--dry-run]' >&2;exit 2 ;;
esac
shift
[[ "$#" -eq 0 || ( "$#" -eq 1 && "$1" == --dry-run ) ]] || exit 2
exec bash "$ROOT/graphics/tools/start-s36-rootful-ahb-output.sh" "$mode" "$@"
