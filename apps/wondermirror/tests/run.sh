#!/usr/bin/env bash
# Checks what can be checked without a Windows machine, in a private HOME:
# nothing of the real machine, its settings or its session is read or changed.

set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

export HOME="$work/home"
export XDG_DATA_HOME="$HOME/.local/share" XDG_STATE_HOME="$HOME/.local/state" XDG_CONFIG_HOME="$HOME/.config"
export PYTHONDONTWRITEBYTECODE=1 LANG=C LC_ALL=C
unset WAYLAND_DISPLAY DISPLAY
mkdir -p "$HOME"

exec /usr/bin/python3 "$here/checks.py" "$here/.."
