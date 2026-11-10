#!/usr/bin/env bash
# WonderlandWM installer — the compositor, its login session, the desktop apps
# and the desktop (bar, theme, scripts, wallpapers) in one go.
#
# From a clone:
#
#     ./install.sh
#
# Or straight from GitHub (clones into ~/.local/src/wonderlandwm first):
#
#     bash <(curl -fsSL https://raw.githubusercontent.com/evilrender23/wonderlandwm/main/install.sh)
#
# Run it as your normal user: it asks for sudo for the system-wide steps.
# Arch Linux and derivatives (CachyOS, EndeavourOS, Manjaro…) get their
# dependencies installed with pacman; elsewhere install them yourself first
# (see README.md) and the rest works the same. `./install.sh --help` lists
# the options.
set -euo pipefail

REPO_SLUG="evilrender23/wonderlandwm"
REPO_URL="https://github.com/$REPO_SLUG.git"
PREFIX="/usr/local"
MIN_RUST="1.88"

# Arch packages, by step. Every one of them is in the official repos.
PKGS_BUILD=(base-devel git pkgconf libdisplay-info libinput seatd mesa libxkbcommon wayland)
PKGS_SESSION=(seatd xwayland-satellite xdg-desktop-portal xdg-desktop-portal-wlr
    fuzzel swaylock swayidle swayosd brightnessctl playerctl wl-clipboard wlr-randr)
PKGS_APPS=(cmake ninja qt6-base qt6-declarative qt6-svg qt6-multimedia qt6-multimedia-ffmpeg
    taglib xdg-utils)
# Fotos: video playback, EXIF, more image formats, and ffmpeg for video thumbnails.
PKGS_APPS+=(qt6-imageformats exiv2 ffmpeg)
# Bluetooth: the daemon and the Qt binding the manager talks to it through.
PKGS_APPS+=(bluez bluez-qt)
PKGS_DESKTOP=(quickshell jq curl libnotify networkmanager pacman-contrib
    polkit-kde-agent plasma-integration breeze-icons grim slurp wf-recorder adwaita-fonts
    python-gobject)
# The password store behind org.freedesktop.secrets (resources/wonderland-secrets).
PKGS_DESKTOP+=(kwallet)

usage() {
    cat <<EOF
Usage: ./install.sh [options]

Installs WonderlandWM: dependencies (pacman), the compositor and its login
session (/usr/local), the desktop apps in apps/ (/usr/local) and the desktop
config (~/.config/driftwm). Re-running it updates an existing install.

Steps (all on by default):
  --no-deps        don't install packages with pacman
  --no-session     don't build/install the compositor and the login session
  --no-apps        don't build/install the apps in apps/
  --no-desktop     don't install the bar, theme, scripts and wallpapers
  --no-greeter     don't install Wonderland's login screen (extras/greeter)
                   and make it the login manager; the one you have stays

Options:
  --fresh-config   replace an existing ~/.config/driftwm/config.toml with the
                   WonderlandWM one (the old file is kept as a timestamped .bak).
                   By default an existing config is left as it is.
  --update         git pull the repository first, then install
  -y, --yes        don't ask for confirmation
  -n, --dry-run    print what would be done, change nothing
  --uninstall      remove what the installer put in /usr/local
                   (your config in ~/.config/driftwm is kept)
  -h, --help       this help

Environment:
  WONDERLAND_SRC   where the GitHub one-liner clones the repository
                   (default: ~/.local/src/wonderlandwm)
EOF
}

if [[ -t 1 ]]; then
    B=$'\e[1m' DIM=$'\e[2m' RED=$'\e[31m' GRN=$'\e[32m' YLW=$'\e[33m' BLU=$'\e[34m' R=$'\e[0m'
else
    B="" DIM="" RED="" GRN="" YLW="" BLU="" R=""
fi
step() { printf '\n%s==>%s %s%s%s\n' "$BLU$B" "$R" "$B" "$*" "$R"; }
info() { printf '    %s\n' "$*"; }
ok() { printf '    %s✓%s %s\n' "$GRN" "$R" "$*"; }
warn() { printf '    %s!%s %s\n' "$YLW$B" "$R" "$*" >&2; }
die() {
    printf '%serror:%s %s\n' "$RED$B" "$R" "$*" >&2
    exit 1
}

DRY_RUN=0
# Print a command, then run it (unless --dry-run).
run() {
    printf '    %s$ %s%s\n' "$DIM" "$*" "$R"
    [[ $DRY_RUN -eq 1 ]] || "$@"
}
as_root() { run sudo "$@"; }

YES=0
confirm() {
    [[ $YES -eq 1 ]] && return 0
    local answer
    if [[ -r /dev/tty ]] && { exec 3</dev/tty; } 2>/dev/null; then
        printf '%s [Y/n] ' "$1"
        read -r answer <&3 || answer=n
        exec 3<&-
    else
        die "no terminal to ask on; re-run with --yes"
    fi
    [[ -z $answer || $answer == [YySs]* ]]
}

# --- Run from GitHub without a clone: fetch the repository, then hand over. ---

here=""
if [[ -n ${BASH_SOURCE[0]:-} && -f ${BASH_SOURCE[0]} ]]; then
    here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
fi
if [[ -z $here || ! -f $here/Cargo.toml || ! -f $here/dev/scripts/install-wonderlandwm ]]; then
    for a in "$@"; do
        case $a in
        -h | --help) usage; exit 0 ;;
        -n | --dry-run) DRY_RUN=1 ;;
        --no-deps | --no-session | --no-apps | --no-desktop | --no-greeter | --fresh-config | \
            --update | \
            -y | --yes | --uninstall) ;;
        *) usage >&2; die "unknown option: $a" ;;
        esac
    done
    [[ $EUID -ne 0 ]] || die "run it as your normal user (it asks for sudo when it needs it)"
    src="${WONDERLAND_SRC:-$HOME/.local/src/wonderlandwm}"
    step "Getting WonderlandWM into $src"
    if ! command -v git >/dev/null 2>&1; then
        command -v pacman >/dev/null 2>&1 || die "git is needed: install it and run this again"
        run sudo pacman -S --needed --noconfirm git
    fi
    if [[ -d $src/.git ]]; then
        run git -C "$src" pull --ff-only
    elif [[ -e $src ]]; then
        die "$src exists and is not a clone of the repository; set WONDERLAND_SRC to another folder"
    else
        mkdir -p "$(dirname -- "$src")"
        # The repository may be private: fall back to the GitHub CLI's login.
        if ! run env GIT_TERMINAL_PROMPT=0 git clone "$REPO_URL" "$src"; then
            if ! command -v gh >/dev/null 2>&1 || ! gh auth status >/dev/null 2>&1; then
                die "could not clone $REPO_URL (private repository? log in with 'gh auth login' and retry)"
            fi
            run gh repo clone "$REPO_SLUG" "$src"
        fi
    fi
    [[ $DRY_RUN -eq 0 ]] || { info "(dry run: would now run $src/install.sh $*)"; exit 0; }
    exec bash "$src/install.sh" "$@"
fi

ROOT="$here"
cd "$ROOT"

DO_DEPS=1 DO_SESSION=1 DO_APPS=1 DO_DESKTOP=1 DO_GREETER=1 FRESH_CONFIG=0 UPDATE=0 UNINSTALL=0
passthrough=()
for a in "$@"; do
    case $a in
    --no-deps) DO_DEPS=0 ;;
    --no-session) DO_SESSION=0 ;;
    --no-apps) DO_APPS=0 ;;
    --no-desktop) DO_DESKTOP=0 ;;
    --no-greeter) DO_GREETER=0 ;;
    --fresh-config) FRESH_CONFIG=1 ;;
    --update) UPDATE=1; continue ;;
    -y | --yes) YES=1 ;;
    -n | --dry-run) DRY_RUN=1 ;;
    --uninstall) UNINSTALL=1 ;;
    -h | --help) usage; exit 0 ;;
    *) usage >&2; die "unknown option: $a" ;;
    esac
    passthrough+=("$a")
done

[[ $(uname -s) == Linux ]] || die "WonderlandWM is a Linux compositor"
[[ $EUID -ne 0 ]] || die "run it as your normal user (it asks for sudo when it needs it)"

if [[ $UPDATE -eq 1 ]]; then
    step "Updating the repository"
    [[ -d .git || -f .git ]] || die "--update needs a git clone"
    run git pull --ff-only
    # The pull may have changed this very script: run the new one.
    [[ $DRY_RUN -eq 1 ]] || exec bash "$ROOT/install.sh" "${passthrough[@]}"
fi

cfg_dir="${XDG_CONFIG_HOME:-$HOME/.config}/driftwm"
state_dir="${XDG_STATE_HOME:-$HOME/.local/state}/wonderlandwm"
has_pacman=0
command -v pacman >/dev/null 2>&1 && has_pacman=1

# apps/<name>/ with a CMakeLists.txt (Qt apps) or a Makefile with an install
# target (Rust apps that ship their own .desktop file).
app_kind() {
    if [[ -f $1/CMakeLists.txt ]]; then
        echo cmake
    elif [[ -f $1/Makefile ]] && grep -q '^install:' "$1/Makefile"; then
        echo make
    fi
}
apps=()
for d in apps/*/; do
    d="${d%/}"
    [[ -n $(app_kind "$d") ]] && apps+=("$d")
done

if [[ $UNINSTALL -eq 1 ]]; then
    printf '%sUninstall WonderlandWM%s from %s.\n' "$B" "$R" "$PREFIX"
    info "Your config in $cfg_dir and the pacman packages stay."
    confirm "Continue?" || exit 0
    if [[ -e /etc/wonderland-greeter || -x $PREFIX/bin/wonderland-greeter ]]; then
        # First, while the compositor it runs is still there: the login manager
        # you had before comes back.
        greeter_args=(--uninstall)
        [[ $DRY_RUN -eq 1 ]] && greeter_args+=(-n)
        extras/greeter/install.sh "${greeter_args[@]}"
    fi
    step "Login session and compositor"
    as_root sh dev/scripts/install-wonderlandwm --uninstall
    step "Apps"
    for d in "${apps[@]}"; do
        name="$(basename "$d")"
        case $(app_kind "$d") in
        cmake)
            manifest="target/apps/$name/install_manifest.txt"
            if [[ -f $manifest ]]; then
                as_root xargs -d '\n' rm -fv <"$manifest"
            else
                warn "$name: no install manifest (was it installed by this script?)"
            fi
            ;;
        make) as_root make -C "$d" uninstall PREFIX="$PREFIX" ;;
        esac
    done
    [[ $DRY_RUN -eq 1 ]] && { printf '\n%sDry run finished:%s nothing was changed.\n' "$GRN$B" "$R"; exit 0; }
    systemctl --user daemon-reload 2>/dev/null || true
    printf '\n%sDone.%s Pick another session on the login screen; %s is still there.\n' "$GRN$B" "$R" "$cfg_dir"
    exit 0
fi

# --- Plan ---------------------------------------------------------------------

keep_config=0
[[ -f $cfg_dir/config.toml && $FRESH_CONFIG -eq 0 ]] && keep_config=1

printf '%sWonderlandWM installer%s  %s(%s @ %s)%s\n' "$B" "$R" "$DIM" "$ROOT" \
    "$(git rev-parse --short HEAD 2>/dev/null || echo '?')" "$R"
if [[ $DO_DEPS -eq 1 ]]; then
    if [[ $has_pacman -eq 1 ]]; then
        info "• packages: the build, session and desktop dependencies (pacman --needed)"
    else
        warn "no pacman: install the dependencies listed in README.md yourself"
        DO_DEPS=0
    fi
fi
[[ $DO_SESSION -eq 1 ]] && info "• compositor + login session \"WonderlandWM\" -> $PREFIX (sudo)"
[[ $DO_APPS -eq 1 && ${#apps[@]} -gt 0 ]] && info "• apps: ${apps[*]#apps/} -> $PREFIX (sudo)"
if [[ $DO_DESKTOP -eq 1 ]]; then
    info "• desktop: bar, theme, scripts, wallpapers, icons -> $cfg_dir"
    if [[ $keep_config -eq 1 ]]; then
        info "  your config.toml stays as it is (--fresh-config replaces it)"
    elif [[ -f $cfg_dir/config.toml ]]; then
        info "  config.toml is replaced (the current one is kept as a .bak)"
    fi
    info "  scripts/, quickshell/ and wallpapers/ there are replaced (old ones kept as .bak)"
fi
[[ $DO_GREETER -eq 1 ]] &&
    info "• login screen: Wonderland's greeter (greetd) becomes the login manager, from the next boot (sudo)"
[[ $DRY_RUN -eq 1 ]] && info "${YLW}dry run: nothing is changed${R}"
echo
confirm "Continue?" || exit 0

if [[ $DRY_RUN -eq 0 && ($DO_DEPS -eq 1 || $DO_SESSION -eq 1 || $DO_APPS -eq 1 || $DO_GREETER -eq 1) ]]; then
    # Ask for the password once, up front, instead of halfway through a build.
    sudo -v || die "sudo is needed for the system-wide steps"
    # Keep it fresh through a long build so it doesn't ask again halfway.
    (while sleep 50; do sudo -n true 2>/dev/null || exit; done) &
    sudo_keepalive=$!
    trap 'kill "$sudo_keepalive" 2>/dev/null || true' EXIT
fi

failed=()

# --- Dependencies ---------------------------------------------------------------

version_ge() { [[ $(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1) == "$2" ]]; }

rust_ok() {
    command -v cargo >/dev/null 2>&1 && command -v rustc >/dev/null 2>&1 || return 1
    local v
    v="$(rustc --version 2>/dev/null | awk '{print $2}')"
    [[ -n $v ]] && version_ge "${v%%-*}" "$MIN_RUST"
}

if [[ $DO_DEPS -eq 1 ]]; then
    step "Installing dependencies"
    want=("${PKGS_BUILD[@]}")
    [[ $DO_SESSION -eq 1 ]] && want+=("${PKGS_SESSION[@]}")
    [[ $DO_APPS -eq 1 ]] && want+=("${PKGS_APPS[@]}")
    [[ $DO_DESKTOP -eq 1 ]] && want+=("${PKGS_DESKTOP[@]}")
    if ! rust_ok; then
        # rustup and the rust package conflict: use whichever the system has.
        if command -v rustup >/dev/null 2>&1; then
            run rustup toolchain install stable --profile minimal
            run rustup default stable
        else
            want+=(rust)
        fi
    fi
    # A fresh system (or a container) has no sync databases yet; with them,
    # -S --needed leaves everything that's already installed alone.
    have_db=1
    if ! compgen -G '/var/lib/pacman/sync/*.db' >/dev/null; then
        have_db=0
        as_root pacman -Syu --noconfirm
    fi
    available=()
    for p in $(printf '%s\n' "${want[@]}" | sort -u); do
        if [[ $DRY_RUN -eq 1 && $have_db -eq 0 ]] ||
            pacman -Si "$p" >/dev/null 2>&1 || pacman -Qq "$p" >/dev/null 2>&1; then
            available+=("$p")
        else
            warn "$p is not in your repositories; skipping it"
        fi
    done
    as_root pacman -S --needed --noconfirm "${available[@]}"
fi

if [[ $DO_SESSION -eq 1 && $DRY_RUN -eq 0 ]] && ! rust_ok; then
    die "Rust $MIN_RUST or newer is needed to build the compositor (https://rustup.rs)"
fi

# --- Compositor + login session -------------------------------------------------

if [[ $DO_SESSION -eq 1 ]]; then
    step "Building the compositor (cargo build --release; the first build takes a few minutes)"
    # dev/scripts/install-wonderlandwm installs from ./target, whatever the
    # user's own CARGO_TARGET_DIR says.
    run env CARGO_TARGET_DIR="$ROOT/target" cargo build --release --locked
    [[ $DRY_RUN -eq 1 || -x target/release/driftwm ]] || die "the build left no target/release/driftwm"

    step "Installing the compositor and the login session"
    installed="$PREFIX/bin/driftwm"
    if [[ -f $installed ]] && ! cmp -s "$installed" target/release/driftwm; then
        # One step back, should the new build misbehave.
        run mkdir -p "$state_dir"
        run cp "$installed" "$state_dir/driftwm.prev"
        info "previous compositor kept as $state_dir/driftwm.prev"
    fi
    # The packages were installed above (or the user opted out with --no-deps).
    as_root env WONDERLAND_SKIP_PACKAGES=1 sh dev/scripts/install-wonderlandwm
    [[ $DRY_RUN -eq 1 ]] || systemctl --user daemon-reload 2>/dev/null || true
    if pgrep -x driftwm >/dev/null 2>&1; then
        info "a WonderlandWM session is running: the new compositor starts at the next login"
    fi
fi

# --- Apps -----------------------------------------------------------------------

if [[ $DO_APPS -eq 1 && ${#apps[@]} -gt 0 ]]; then
    # Rabbit was called Finder: what an install of it left behind goes, so the
    # menu doesn't list both and "Show in folder" starts the one that is there.
    finder_files=(
        "$PREFIX/bin/wonderland-finder"
        "$PREFIX/share/applications/wonderland-finder.desktop"
        "$PREFIX/share/dbus-1/services/org.wonderland.Finder.FileManager1.service"
        "$PREFIX/share/icons/hicolor/scalable/apps/wonderland-finder.svg"
    )
    for f in "${finder_files[@]}"; do
        if [[ -e $f ]]; then
            as_root rm -f "${finder_files[@]}"
            run rm -rf target/apps/finder
            break
        fi
    done
    for d in "${apps[@]}"; do
        name="$(basename "$d")"
        step "App: $name"
        # One app failing to build doesn't stop the others or the desktop. The
        # subshell runs outside an `if` so that its own `set -e` still applies.
        set +e
        (
            set -e
            case $(app_kind "$d") in
            cmake)
                gen=()
                command -v ninja >/dev/null 2>&1 && gen=(-G Ninja)
                run cmake -S "$d" -B "target/apps/$name" "${gen[@]}" \
                    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX"
                run cmake --build "target/apps/$name" --parallel
                as_root cmake --install "target/apps/$name"
                ;;
            make)
                # The Makefile installs from the app's own target/.
                grep -q '^build:' "$d/Makefile" && run env -u CARGO_TARGET_DIR make -C "$d" build
                as_root make -C "$d" install PREFIX="$PREFIX"
                ;;
            esac
        )
        rc=$?
        set -e
        if [[ $rc -ne 0 ]]; then
            warn "$name failed to build or install; see the output above"
            failed+=("app $name")
        fi
    done
    if [[ $DRY_RUN -eq 0 ]]; then
        if command -v update-desktop-database >/dev/null 2>&1; then
            sudo update-desktop-database -q "$PREFIX/share/applications" 2>/dev/null || true
        fi
        if command -v gtk-update-icon-cache >/dev/null 2>&1; then
            sudo gtk-update-icon-cache -q -t "$PREFIX/share/icons/hicolor" 2>/dev/null || true
        fi
        # Rabbit is the desktop's file manager: folders open in it,
        # unless the user already picked an app for them.
        mimeapps="${XDG_CONFIG_HOME:-$HOME/.config}/mimeapps.list"
        if grep -qs 'wonderland-finder\.desktop' "$mimeapps"; then
            sed -i 's/wonderland-finder\.desktop/wonderland-rabbit.desktop/g' "$mimeapps"
        fi
        if [[ -f $PREFIX/share/applications/wonderland-rabbit.desktop ]] &&
            command -v xdg-mime >/dev/null 2>&1 &&
            ! grep -qs '^inode/directory=' "$mimeapps"; then
            # xdg-mime neither creates the directory nor reports a failed write.
            mkdir -p "${mimeapps%/*}"
            xdg-mime default wonderland-rabbit.desktop inode/directory 2>/dev/null || true
            if grep -qs '^inode/directory=wonderland-rabbit.desktop' "$mimeapps"; then
                ok "Rabbit opens folders"
            else
                warn "could not make Rabbit the app for folders; run: xdg-mime default wonderland-rabbit.desktop inode/directory"
            fi
        fi
        # Música is the desktop's music player: audio files open in it, each
        # type unless the user already picked an app for it.
        musica="$PREFIX/share/applications/wonderland-musica.desktop"
        if [[ -f $musica ]] && command -v xdg-mime >/dev/null 2>&1; then
            mkdir -p "${mimeapps%/*}"
            audio_types=()
            while IFS= read -r -d ';' type; do
                [[ -n $type ]] && ! grep -qs "^${type//+/[+]}=" "$mimeapps" && audio_types+=("$type")
            done < <(sed -n 's/^MimeType=//p' "$musica")
            if [[ ${#audio_types[@]} -gt 0 ]]; then
                xdg-mime default wonderland-musica.desktop "${audio_types[@]}" 2>/dev/null || true
            fi
            if grep -qs '^audio/mpeg=wonderland-musica.desktop' "$mimeapps"; then
                ok "Música plays audio files"
            elif ! grep -qs '^audio/mpeg=' "$mimeapps"; then
                warn "could not make Música the app for audio; run: xdg-mime default wonderland-musica.desktop audio/mpeg"
            fi
        fi
        # Calendario is the desktop's calendar: .ics files open in it, unless
        # the user already picked an app for them.
        if [[ -f $PREFIX/share/applications/wonderland-calendario.desktop ]] &&
            command -v xdg-mime >/dev/null 2>&1 &&
            ! grep -qs '^text/calendar=' "$mimeapps"; then
            mkdir -p "${mimeapps%/*}"
            xdg-mime default wonderland-calendario.desktop text/calendar 2>/dev/null || true
            if grep -qs '^text/calendar=wonderland-calendario.desktop' "$mimeapps"; then
                ok "Calendario opens .ics files"
            else
                warn "could not make Calendario the app for calendars; run: xdg-mime default wonderland-calendario.desktop text/calendar"
            fi
        fi
        # Películas is the desktop's video player: videos open in it, each
        # type unless the user already picked an app for it.
        peliculas="$PREFIX/share/applications/wonderland-peliculas.desktop"
        if [[ -f $peliculas ]] && command -v xdg-mime >/dev/null 2>&1; then
            mkdir -p "${mimeapps%/*}"
            video_types=()
            while IFS= read -r -d ';' type; do
                [[ -n $type ]] && ! grep -qs "^${type//+/[+]}=" "$mimeapps" && video_types+=("$type")
            done < <(sed -n 's/^MimeType=//p' "$peliculas")
            if [[ ${#video_types[@]} -gt 0 ]]; then
                xdg-mime default wonderland-peliculas.desktop "${video_types[@]}" 2>/dev/null || true
            fi
            if grep -qs '^video/mp4=wonderland-peliculas.desktop' "$mimeapps"; then
                ok "Películas opens videos"
            elif ! grep -qs '^video/mp4=' "$mimeapps"; then
                warn "could not make Películas the app for videos; run: xdg-mime default wonderland-peliculas.desktop video/mp4"
            fi
        fi
        # Fotos is the desktop's picture library: images and videos open in it,
        # each type unless the user already picked an app for it.
        fotos="$PREFIX/share/applications/wonderland-fotos.desktop"
        if [[ -f $fotos ]] && command -v xdg-mime >/dev/null 2>&1; then
            mkdir -p "${mimeapps%/*}"
            media_types=()
            while IFS= read -r -d ';' type; do
                [[ -n $type ]] && ! grep -qs "^${type//+/[+]}=" "$mimeapps" && media_types+=("$type")
            done < <(sed -n 's/^MimeType=//p' "$fotos")
            if [[ ${#media_types[@]} -gt 0 ]]; then
                xdg-mime default wonderland-fotos.desktop "${media_types[@]}" 2>/dev/null || true
            fi
            if grep -qs '^image/jpeg=wonderland-fotos.desktop' "$mimeapps"; then
                ok "Fotos opens pictures and videos"
            elif ! grep -qs '^image/jpeg=' "$mimeapps"; then
                warn "could not make Fotos the app for pictures; run: xdg-mime default wonderland-fotos.desktop image/jpeg"
            fi
        fi
    fi
fi

# --- Desktop ----------------------------------------------------------------------

if [[ $DO_DESKTOP -eq 1 ]]; then
    step "Installing the desktop into $cfg_dir"
    if ! run env DRIFTWM_KEEP_CONFIG="$keep_config" sh extras/install.sh; then
        warn "extras/install.sh failed; see the output above"
        failed+=("desktop")
    fi
    # A config.toml that stays may hold window rules for Rabbit under the name
    # it had, Finder: they would stop matching its windows.
    if [[ $keep_config -eq 1 ]] && grep -qs '"wonderland-finder"' "$cfg_dir/config.toml"; then
        run sed -i 's/"wonderland-finder"/"wonderland-rabbit"/g' "$cfg_dir/config.toml"
    fi
fi

# --- Login screen -------------------------------------------------------------------

# Last: it runs the installed compositor and reads the desktop's config.toml
# for the monitors, the keyboard layout and the wallpaper.
if [[ $DO_GREETER -eq 1 ]]; then
    step "Installing the login screen and making it the login manager"
    if [[ $DRY_RUN -eq 0 && ! -x $PREFIX/bin/driftwm ]]; then
        warn "$PREFIX/bin/driftwm is missing (--no-session?); the login screen needs it"
        failed+=("login screen")
    else
        # Its own dry run prints what it would do, so it runs either way.
        greeter_args=(--enable)
        [[ $DRY_RUN -eq 1 ]] && greeter_args+=(-n)
        if ! env WONDERLAND_SKIP_PACKAGES="$((1 - DO_DEPS))" extras/greeter/install.sh "${greeter_args[@]}"; then
            warn "extras/greeter/install.sh failed; your login manager is the one you had"
            failed+=("login screen")
        fi
    fi
fi

# --- Check ------------------------------------------------------------------------

step "Checking"
driftwm_bin=""
if [[ -x $PREFIX/bin/driftwm ]]; then
    driftwm_bin="$PREFIX/bin/driftwm"
elif [[ -x target/release/driftwm ]]; then
    driftwm_bin="target/release/driftwm"
fi
if [[ $DRY_RUN -eq 1 ]]; then
    info "(dry run)"
elif [[ -n $driftwm_bin && -f $cfg_dir/config.toml ]]; then
    # Older builds print the error but still exit 0: trust the text.
    out="$("$driftwm_bin" --config "$cfg_dir/config.toml" --check-config 2>&1 || true)"
    if grep -q 'Config OK' <<<"$out"; then
        ok "$cfg_dir/config.toml is valid"
    else
        warn "the compositor rejects $cfg_dir/config.toml:"
        printf '%s\n' "$out" | sed 's/^/      /' >&2
        warn "fix it, or start over from WonderlandWM's with: ./install.sh --no-deps --no-session --no-apps --fresh-config"
        failed+=("config check")
    fi
fi
if [[ $DO_SESSION -eq 1 && $DRY_RUN -eq 0 ]]; then
    if cmp -s "$PREFIX/bin/driftwm" target/release/driftwm; then
        ok "$PREFIX/bin/driftwm is this build"
    else
        warn "$PREFIX/bin/driftwm is not this build"
        failed+=("compositor install")
    fi
    if ldd "$PREFIX/bin/driftwm" 2>/dev/null | grep -q 'not found'; then
        warn "missing libraries: $(ldd "$PREFIX/bin/driftwm" | grep 'not found' | tr -s ' \t' ' ')"
        failed+=("compositor libraries")
    fi
    [[ -f /usr/share/wayland-sessions/wonderlandwm.desktop ]] && ok "login session \"WonderlandWM\" installed"
fi
if [[ $DO_GREETER -eq 1 && $DRY_RUN -eq 0 ]] && systemctl is-enabled --quiet greetd.service 2>/dev/null; then
    ok "Wonderland's login screen is the login manager, from the next boot"
fi

echo
if [[ ${#failed[@]} -gt 0 ]]; then
    printf '%sFinished with problems:%s %s\n' "$YLW$B" "$R" "${failed[*]}"
    exit 1
fi
if [[ $DRY_RUN -eq 1 ]]; then
    printf '%sDry run finished:%s nothing was changed.\n' "$GRN$B" "$R"
    exit 0
fi
printf '%sWonderlandWM is installed.%s\n' "$GRN$B" "$R"
if [[ $DO_SESSION -eq 1 ]]; then
    info "Log out and pick \"WonderlandWM\" on the login screen."
    if systemctl is-active --quiet 'ly@*' 2>/dev/null || pgrep -x ly-dm >/dev/null 2>&1; then
        info "ly reads its session list only when it starts: reboot to see the new session."
    fi
    info "Way out if something goes wrong: Super+Ctrl+Shift+Q (or, from a TTY: systemctl --user stop driftwm)."
    [[ -f $state_dir/driftwm.prev ]] &&
        info "Back to the previous compositor: sudo install -m755 $state_dir/driftwm.prev $PREFIX/bin/driftwm"
fi
if [[ $DO_GREETER -eq 1 ]]; then
    info "The login screen is Wonderland's own from the next boot. Back to the one you had: extras/greeter/install.sh --disable"
fi
info "Update later with: $ROOT/install.sh --update"
