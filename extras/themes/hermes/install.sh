#!/bin/sh
# Copy the Hermes theme shaders where the theme files expect them.
# Does not touch config.toml: merge hermes-light.toml or hermes-dark.toml
# into it yourself (the compositor hot-reloads on save).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
dest="${XDG_CONFIG_HOME:-$HOME/.config}/driftwm/themes/hermes"
mkdir -p "$dest"
install -m644 "$here"/*.glsl "$here"/*.toml "$dest/"
echo "Theme copied to $dest"
echo "Apply it with: ~/.config/driftwm/scripts/wonderland-theme hermes-light   (or hermes-dark)"
echo "or from the bar: right click the launcher → Settings → Theme."
