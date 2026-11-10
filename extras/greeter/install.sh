#!/usr/bin/env bash
# Wonderland's login screen (greetd + the compositor + Quickshell).
#
#   extras/greeter/install.sh             install it; the login manager you have stays
#   extras/greeter/install.sh --enable    install it and make it the login manager
#                                         (from the next boot)
#   extras/greeter/install.sh --disable   back to the login manager you had
#   extras/greeter/install.sh --uninstall --disable, and remove the files
#
# Run it as yourself, from your desktop: it reads your compositor config so the
# login screen gets your monitors, keyboard layout and wallpaper settings, and
# asks for sudo for the rest. Run it again after changing any of those.
set -euo pipefail

PREFIX="${PREFIX:-/usr/local}"
ETC=/etc/wonderland-greeter
VAR=/var/lib/wonderland-greeter
SHARE="$PREFIX/share/wonderland-greeter"
GREETD_CONF=/etc/greetd/config.toml
# Login managers --enable takes over from (ly runs as ly@ttyN).
KNOWN_DMS=('ly@*.service' ly.service sddm.service gdm.service lightdm.service
    plasmalogin.service lemurs.service lxdm.service)

usage() { sed -n '2,12s/^# \{0,1\}//p' "$0"; }
step() { printf '\n==> %s\n' "$*"; }
info() { printf '    %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

DRY_RUN=0 ACTION=install ENABLE=0
for a in "$@"; do
    case $a in
    --enable) ENABLE=1 ;;
    --disable) ACTION=disable ;;
    --uninstall) ACTION=uninstall ;;
    -n | --dry-run) DRY_RUN=1 ;;
    -h | --help) usage; exit 0 ;;
    *) usage >&2; die "unknown option: $a" ;;
    esac
done
[[ $EUID -ne 0 ]] || die "run it as your normal user (it asks for sudo when it needs it)"

run() {
    printf '    $ %s\n' "$*"
    [[ $DRY_RUN -eq 1 ]] || "$@"
}
as_root() { run sudo "$@"; }

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# Enabled login managers, greetd aside: the symlinks systemd made for them.
enabled_dms() {
    local args=() pat f
    for pat in "${KNOWN_DMS[@]}" display-manager.service; do
        args+=(-o -name "$pat")
    done
    find /etc/systemd/system -maxdepth 2 -type l \( "${args[@]:1}" \) 2>/dev/null |
        while read -r f; do
            if [[ $(basename "$f") == display-manager.service ]]; then
                basename "$(readlink -f "$f")"
            else
                basename "$f"
            fi
        done | grep -v '^greetd\.service$' | sort -u
}

disable_greeter() {
    step "Back to the previous login manager"
    local prev=""
    [[ -r $ETC/previous-dm ]] && prev="$(cat "$ETC/previous-dm")"
    if systemctl is-enabled --quiet greetd.service 2>/dev/null; then
        as_root systemctl disable greetd.service
    fi
    if [[ -n $prev ]]; then
        # shellcheck disable=SC2086  # one unit per word
        as_root systemctl enable $prev
        info "$prev is the login manager again, from the next boot"
    else
        info "no previous login manager on record: enable yours with systemctl enable <unit>"
    fi
}

if [[ $ACTION != install ]]; then
    disable_greeter
    if [[ $ACTION == uninstall ]]; then
        step "Removing the login screen"
        if [[ -f $GREETD_CONF.pre-wonderland ]]; then
            as_root mv "$GREETD_CONF.pre-wonderland" "$GREETD_CONF"
        elif grep -qs wonderland-greeter "$GREETD_CONF"; then
            as_root rm -f "$GREETD_CONF"
        fi
        as_root rm -rf "$PREFIX/bin/wonderland-greeter" "$PREFIX/bin/wonderland-greeter-ui" \
            "$SHARE" "$ETC" "$VAR"
        info "the greetd package is left installed"
    fi
    exit 0
fi

# --- Install -------------------------------------------------------------------

step "Packages"
if command -v pacman >/dev/null 2>&1; then
    as_root pacman -S --needed --noconfirm greetd quickshell
else
    info "no pacman here: install greetd and quickshell with your package manager"
fi
if [[ $DRY_RUN -eq 0 ]]; then
    command -v qs >/dev/null 2>&1 || die "Quickshell (qs) is not installed"
    [[ -x $PREFIX/bin/driftwm ]] || die "$PREFIX/bin/driftwm is missing: run ./install.sh first"
    if ! getent passwd greeter >/dev/null; then
        as_root useradd --system --no-create-home --home-dir "$VAR" \
            --shell /usr/bin/nologin greeter
    fi
fi

step "Generating the login screen for this machine"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
run "$here/scripts/greeter-setup" "$stage" --prefix "$PREFIX"
if [[ $DRY_RUN -eq 0 ]]; then
    # The compositor reads the staged config with the shader still next to it.
    sed "s|$SHARE/wallpaper.glsl|$stage/wallpaper.glsl|" "$stage/driftwm.toml" >"$stage/check.toml"
    out="$("$PREFIX/bin/driftwm" --config "$stage/check.toml" --check-config 2>&1 || true)"
    grep -q 'Config OK' <<<"$out" || die "the compositor rejects the greeter's config:
$out"
    rm -f "$stage/check.toml"
fi

step "Installing"
as_root install -Dm755 "$here/scripts/wonderland-greeter" "$PREFIX/bin/wonderland-greeter"
as_root install -Dm755 "$here/scripts/wonderland-greeter-ui" "$PREFIX/bin/wonderland-greeter-ui"
as_root rm -rf "$SHARE/ui"
as_root install -d "$SHARE/ui/scripts" "$SHARE/ui/assets/icons" "$ETC"
as_root install -m644 "$here"/*.qml "$SHARE/ui/"
as_root install -m644 "$here"/assets/icons/* "$SHARE/ui/assets/icons/"
as_root install -m755 "$here/scripts/greeter-info" "$SHARE/ui/scripts/greeter-info"
as_root install -m644 "$stage/wallpaper.glsl" "$SHARE/wallpaper.glsl"
as_root install -m644 "$stage/driftwm.toml" "$ETC/driftwm.toml"
as_root install -m644 "$stage/greeter.json" "$ETC/greeter.json"
as_root install -d -o greeter -g greeter -m750 "$VAR"

if [[ -f $GREETD_CONF && ! -f $GREETD_CONF.pre-wonderland ]] &&
    ! grep -qs wonderland-greeter "$GREETD_CONF"; then
    as_root cp -p "$GREETD_CONF" "$GREETD_CONF.pre-wonderland"
fi
as_root install -Dm644 "$stage/greetd.toml" "$GREETD_CONF"

if [[ $ENABLE -eq 0 ]]; then
    step "Installed"
    info "Your login manager is unchanged. Make this one the login screen with:"
    info "  $0 --enable"
    exit 0
fi

step "Making it the login manager"
dms="$(enabled_dms | tr '\n' ' ')"
dms="${dms% }"
if [[ -n $dms ]]; then
    printf '%s\n' "$dms" | as_root tee "$ETC/previous-dm" >/dev/null
    # shellcheck disable=SC2086  # one unit per word
    as_root systemctl disable $dms
fi
as_root systemctl enable greetd.service

step "Done"
info "The new login screen shows at the next boot (nothing running was stopped)."
info "Way back, also from a TTY (Ctrl+Alt+F3):  $0 --disable"
[[ -z $dms ]] || info "  or by hand:  sudo systemctl disable greetd && sudo systemctl enable $dms"
info "The greeter's log: $VAR/greeter.log"
