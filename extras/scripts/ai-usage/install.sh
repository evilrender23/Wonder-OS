#!/bin/sh
# Install the AI quota collectors (from Quickshell Rise, MIT) for the bar's
# `ai` widget: scripts to ~/.local/bin, user timers every 5 minutes. Each
# script talks only to its own tool's service with that tool's stored login
# (Claude Code's OAuth token, Codex's app-server, OpenCode's local data).
# Pass the tools you use, e.g.: ./install.sh claude codex
set -eu
here=$(cd "$(dirname "$0")" && pwd)
[ $# -gt 0 ] || { echo "usage: $0 claude|codex|opencode ..." >&2; exit 2; }
mkdir -p "$HOME/.local/bin" "${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
for tool in "$@"; do
    case $tool in claude|codex|opencode) ;; *) echo "unknown tool: $tool" >&2; exit 2;; esac
    install -m 755 "$here/$tool-usage" "$HOME/.local/bin/$tool-usage"
    install -m 644 "$here/systemd/$tool-usage.service" "$here/systemd/$tool-usage.timer" \
        "${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/"
    systemctl --user daemon-reload
    systemctl --user enable --now "$tool-usage.timer"
    echo "installed $tool-usage (cache: ~/.cache/$tool-usage.json)"
done
