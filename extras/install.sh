#!/bin/sh
# Install the driftwm rice into your config dir.
#
# Idempotent and non-destructive: existing config/scripts/shell are backed
# up (timestamped, never clobbered) before the rice's versions are copied into
# ~/.config/driftwm. See README.md for the tools this setup expects.
#
# DRIFTWM_KEEP_CONFIG=1 leaves an existing config.toml alone and updates only
# the rest (the top-level install.sh does that on a re-run).
set -eu

src="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
cfg="${XDG_CONFIG_HOME:-$HOME/.config}/driftwm"
ts="$(date +%Y%m%d-%H%M%S)"

mkdir -p "$cfg"

# Move an existing target aside (timestamped) so a customized one is never lost.
backup() {
    if [ -e "$cfg/$1" ]; then
        mv -- "$cfg/$1" "$cfg/$1.$ts.bak"
        echo "backed up existing $1 -> $1.$ts.bak"
    fi
}

keep_config=0
if [ "${DRIFTWM_KEEP_CONFIG:-0}" = 1 ] && [ -f "$cfg/config.toml" ]; then
    keep_config=1
    echo "kept your config.toml (WonderlandWM's is $src/config.toml)"
else
    backup config.toml
    cp -- "$src/config.toml" "$cfg/config.toml"
fi

# Scripts: config.toml references them by absolute path (~/.config/driftwm/...)
# rather than bare name, because a systemd user service launched by a display
# manager gets a minimal PATH that often excludes ~/.local/bin.
backup scripts
cp -r -- "$src/scripts" "$cfg/scripts"
chmod +x "$cfg"/scripts/*

# Quickshell shell: top bar, panels, notifications.
backup quickshell
cp -r -- "$src/quickshell" "$cfg/quickshell"

# Shader wallpapers, so [background] can point at a stable path.
backup wallpapers
cp -r -- "$src/wallpapers" "$cfg/wallpapers"

# Hermes compositor theme (shaders + TOML) for wonderland-theme / the bar's
# theme picker.
sh "$src/themes/hermes/install.sh" >/dev/null

# macOS-style app icons (WhiteSur), downloaded into ~/.local/share/icons. With
# no network (or no git) the rice keeps Breeze rather than a theme that isn't
# there, which would leave every app icon blank.
if ! sh "$src/scripts/wonderland-icons" install; then
    if [ "$keep_config" = 0 ]; then
        sed -i 's/^QS_ICON_THEME = "WhiteSur"/QS_ICON_THEME = "breeze"/' "$cfg/config.toml"
        echo "note: WhiteSur icons not installed, using Breeze; retry with: sh $cfg/scripts/wonderland-icons install"
        echo "      then set QS_ICON_THEME = \"WhiteSur\" under [env] in $cfg/config.toml"
    else
        echo "note: WhiteSur icons not installed; retry with: sh $cfg/scripts/wonderland-icons install"
    fi
fi

# Fuzzel + swaync themes matching the shell. These live in their own config
# dirs, so they're copied (and backed up) separately from the driftwm dir.
install_themed() {
    [ -f "$src/$1" ] || return 0
    dir="${XDG_CONFIG_HOME:-$HOME/.config}/$2"
    mkdir -p "$dir"
    if [ -e "$dir/$3" ]; then
        mv -- "$dir/$3" "$dir/$3.$ts.bak"
        echo "backed up existing $2/$3 -> $3.$ts.bak"
    fi
    cp -- "$src/$1" "$dir/$3"
}
install_themed fuzzel/fuzzel.ini fuzzel fuzzel.ini
install_themed swaync/style.css swaync style.css
# The terminal greeting: the Wonderland logo beside a short system summary.
install_themed fastfetch/config.jsonc fastfetch config.jsonc
install_themed fastfetch/wonderland.txt fastfetch wonderland.txt

echo "installed driftwm rice -> $cfg"
echo "AI quota widget (optional): sh $cfg/scripts/ai-usage/install.sh claude codex opencode"

# The rice still runs without these — missing pieces just no-op — but warn so the
# experience isn't silently incomplete.
missing=""
for tool in wonderterm qs nmcli swayosd-server fuzzel jq swaylock swayidle curl \
            checkupdates brightnessctl playerctl wl-copy notify-send; do
    command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
if [ -n "$missing" ]; then
    echo
    echo "note: these tools the rice uses are not on PATH (install for the full experience):"
    echo "  $missing"
fi
