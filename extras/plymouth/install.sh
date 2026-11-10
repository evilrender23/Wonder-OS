#!/usr/bin/env bash
# Wonder OS's boot screen (a Plymouth theme: the logo on black, a spinner below).
#
#   extras/plymouth/install.sh             install it and make it the boot screen
#                                          (rebuilds the initramfs; from the next boot)
#   extras/plymouth/install.sh --uninstall back to the boot screen you had, and
#                                          remove the files
#
# Run it as yourself: it asks for sudo. Plymouth has to be installed and already
# in use (the `plymouth` hook in the initramfs, `splash` on the kernel command
# line): this only changes which theme it shows.
set -euo pipefail

THEME=wonder-os
THEMES_DIR=/usr/share/plymouth/themes
DEST="$THEMES_DIR/$THEME"
STATE=/etc/wonder-os
# The spinner and the password prompt come from Plymouth's own spinner theme,
# copied at install time rather than shipped here.
STOCK="$THEMES_DIR/spinner"

usage() { sed -n '2,11s/^# \{0,1\}//p' "$0"; }
step() { printf '\n==> %s\n' "$*"; }
info() { printf '    %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

DRY_RUN=0 ACTION=install
for a in "$@"; do
    case $a in
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

if [[ $DRY_RUN -eq 0 ]]; then
    command -v plymouth-set-default-theme >/dev/null 2>&1 ||
        die "Plymouth is not installed (plymouth-set-default-theme is missing)"
fi
current="$(plymouth-set-default-theme 2>/dev/null || true)"

if [[ $ACTION == uninstall ]]; then
    step "Back to the previous boot screen"
    prev=""
    [[ -r $STATE/previous-plymouth-theme ]] && prev="$(cat "$STATE/previous-plymouth-theme")"
    if [[ $current == "$THEME" ]]; then
        [[ -n $prev && -d $THEMES_DIR/$prev ]] || prev=spinner
        as_root plymouth-set-default-theme -R "$prev"
        info "$prev is the boot screen again, from the next boot"
    else
        info "the boot screen is ${current:-not set}: left as it is"
    fi
    as_root rm -rf "$DEST" "$STATE/previous-plymouth-theme"
    exit 0
fi

step "Installing the boot screen"
if [[ $DRY_RUN -eq 0 ]]; then
    [[ -f $STOCK/throbber-0001.png && -f $STOCK/lock.png ]] ||
        die "$STOCK is incomplete: reinstall the plymouth package"
fi
as_root rm -rf "$DEST"
as_root install -d "$DEST" "$STATE"
as_root install -m644 "$here/$THEME/$THEME.plymouth" "$here/$THEME/watermark.png" "$DEST/"
# shellcheck disable=SC2016  # $1 and $2 belong to the inner shell
as_root sh -c 'for f in "$1"/throbber-*.png "$1"/bullet.png "$1"/capslock.png "$1"/entry.png \
    "$1"/keyboard.png "$1"/keymap-render.png "$1"/lock.png; do
    [ -f "$f" ] && install -m644 "$f" "$2/"
done; true' sh "$STOCK" "$DEST"

if [[ -n $current && $current != "$THEME" ]]; then
    printf '%s\n' "$current" | as_root tee "$STATE/previous-plymouth-theme" >/dev/null
fi

step "Making it the boot screen"
as_root plymouth-set-default-theme -R "$THEME"

step "Done"
info "The new boot screen shows at the next boot."
info "Way back:  $0 --uninstall"
