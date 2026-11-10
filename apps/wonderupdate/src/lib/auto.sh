#!/usr/bin/env bash
# Automatic updates of the desktop itself, without releases: every machine
# builds the tip of the branch it was installed from.
#
# - install.sh records what it installed in version.json (commit, branch,
#   repository and the steps it left out). Without that stamp the install is
#   older than this, or a development one: the checkout it came from is updated
#   the way it always was (sources.sh), and nothing here runs.
# - A check asks the repository for the tip of the branch. When it is not the
#   installed commit, the new version is built in the background, in a clone
#   that belongs to WonderUpdate: the checkout the user works in is never
#   touched. The build is `install.sh --build-only` of that tip, without root.
# - Installing needs root, so that part waits for the user: a notification says
#   the update is ready, and its button opens a terminal that installs what is
#   already built. auto.log follows the whole thing (STEP n/total, READY,
#   ERROR, APPLIED), one line per event.
# shellcheck disable=SC2034,SC2154

stamp_file="${XDG_STATE_HOME:-$HOME/.local/state}/wonderlandwm/version.json"
managed_dir="${XDG_DATA_HOME:-$HOME/.local/share}/wonderos"
managed_src="$managed_dir/source"
auto_log="$statedir/auto.log"
build_log="$statedir/build.log"
prepared_file="$statedir/prepared"
failed_build_file="$statedir/build-failed"
development_file="$statedir/development"
last_build_file="$statedir/last-build"

st_commit="" st_branch="" st_repo="" st_dirty="" st_opts=()
wl_managed="" wl_tip=""

stamp_field() {
    sed -n "s/^[[:space:]]*\"$1\":[[:space:]]*\"\\{0,1\\}\\([^\"]*\\)\"\\{0,1\\},\\{0,1\\}[[:space:]]*\$/\\1/p" "$stamp_file" 2>/dev/null | head -n 1
}

# Reads the stamp into st_*. Returns 1 without one, or with one that can't be
# trusted: its values end up as arguments of git and install.sh.
read_stamp() {
    st_commit="" st_branch="" st_repo="" st_dirty="" st_opts=()
    [[ -f $stamp_file ]] || return 1
    st_commit="$(stamp_field commit)"
    st_branch="$(stamp_field branch)"
    st_repo="$(stamp_field repo)"
    [[ $st_commit =~ ^[0-9a-f]{7,64}$ ]] || return 1
    [[ $st_branch =~ ^[A-Za-z0-9._/-]+$ && $st_branch != -* ]] || return 1
    [[ -n $st_repo && $st_repo != -* ]] || return 1
    [[ $(stamp_field dirty) == true ]] && st_dirty=1
    local opt
    for opt in $(stamp_field options); do
        case $opt in
        --no-deps | --no-greeter) st_opts+=("$opt") ;;
        esac
    done
    return 0
}

# A short and a full hash are the same commit if one starts with the other.
same_commit() {
    [[ ${#1} -ge 7 && ${#2} -ge 7 ]] && [[ $1 == "$2"* || $2 == "$1"* ]]
}

auto_note() { printf '%s\n' "$*" >>"$auto_log"; }

lock_is_held() {
    local fd rc
    exec {fd}>"$tmpdir/$1.lock" || return 1
    if flock -n "$fd"; then rc=1; else rc=0; fi
    exec {fd}>&-
    return "$rc"
}

managed_git() { GIT_TERMINAL_PROMPT=0 git -C "$managed_src" "$@"; }

# The installed commit has to be part of the published branch; otherwise this
# is a development install, which is left for its owner to update.
installed_is_published() {
    managed_git merge-base --is-ancestor "$st_commit" "origin/$st_branch" 2>/dev/null
}

mark_development() { printf '%s\n' "$st_commit" >"$development_file"; }

is_marked_development() {
    [[ -f $development_file && $(<"$development_file") == "$st_commit" ]]
}

# What the stamped install can be updated to, without root and without
# needing the clone: the repository is asked for the tip of the branch.
query_wonderland_managed() {
    wl_managed=1 wl_tip="" wl_source=$st_repo
    [[ -z $no_wonderland ]] && have git || return 0
    local tip
    if ! tip="$(GIT_TERMINAL_PROMPT=0 timeout "$update_check_timeout" git ls-remote -- "$st_repo" "refs/heads/$st_branch" 2>/dev/null)"; then
        source_errors+=("Wonderland")
        return 0
    fi
    tip=${tip%%[[:space:]]*}
    if [[ ! $tip =~ ^[0-9a-f]{40,64}$ ]]; then
        source_errors+=("Wonderland")
        return 0
    fi
    same_commit "$st_commit" "$tip" && return 0
    is_marked_development && return 0
    wl_tip=$tip
    wl_updates=("wonderland ${st_commit:0:7} -> ${tip:0:7}")
    [[ -d $managed_src/.git ]] || return 0
    # With the clone at hand: what the new commits are, and whether the
    # installed one is among the published ones at all.
    GIT_TERMINAL_PROMPT=0 timeout "$update_check_timeout" git -C "$managed_src" fetch --quiet origin "$st_branch" 2>/dev/null || return 0
    managed_git cat-file -e "$tip^{commit}" 2>/dev/null || return 0
    if ! installed_is_published; then
        mark_development
        wl_updates=() wl_tip=""
        return 0
    fi
    mapfile -t wl_commits < <(managed_git log --first-parent --format=%s "$st_commit..$tip" 2>/dev/null)
}

# Brings the clone to the tip of the branch, creating it the first time. Only
# ever this clone: whatever is in it that the branch doesn't have is discarded,
# except what git ignores (target/, the build cache).
sync_managed() {
    [[ $managed_src == "$managed_dir"/* ]] || return 1
    if [[ -d $managed_src/.git ]]; then
        managed_git remote set-url origin "$st_repo" &&
            managed_git fetch --quiet --prune origin "$st_branch" &&
            managed_git checkout --quiet --force -B "$st_branch" "origin/$st_branch" &&
            managed_git clean -fdq
    elif [[ -e $managed_src ]]; then
        return 1
    else
        mkdir -p "$managed_dir" &&
            GIT_TERMINAL_PROMPT=0 git clone --quiet --branch "$st_branch" -- "$st_repo" "$managed_src"
    fi
}

is_prepared() {
    [[ -n $wl_tip && -f $prepared_file && $(<"$prepared_file") == "$wl_tip" ]]
}

# After a check: starts the background build if there is a new version that
# hasn't been built (or failed to build) yet. Returns at once.
maybe_prepare() {
    [[ -n $wl_managed && -n $wl_tip && -z $no_auto_update ]] || return 0
    is_prepared && return 0
    [[ -f $failed_build_file && $(<"$failed_build_file") == "$wl_tip" ]] && return 0
    if [[ -f $last_build_file && $auto_build_interval -gt 0 ]]; then
        local last
        last="$(stat -c %Y "$last_build_file" 2>/dev/null || echo 0)"
        (($(date +%s) - last >= auto_build_interval * 3600)) || return 0
    fi
    lock_is_held prepare && return 0
    lock_is_held update && return 0
    local -a quiet=()
    have nice && quiet+=(nice -n 19)
    have ionice && quiet+=(ionice -c 3)
    if [[ -n ${INVOCATION_ID:-} ]] && have systemd-run; then
        # Under the timer's unit a child would die with it.
        systemd-run --user --quiet --collect --unit="$name-prepare" -p KillMode=process \
            "${quiet[@]}" "$self" --prepare >/dev/null 2>&1
    else
        setsid -f "${quiet[@]}" "$self" --prepare >/dev/null 2>&1 </dev/null
    fi
    return 0
}

notify_ready() {
    [[ -z $no_notification ]] && have notify-send || return 0
    local tip=$1 body answer
    body="$(managed_git log --first-parent --format='• %s' -n 8 "$st_commit..$tip" 2>/dev/null)"
    answer="$(timeout 600 notify-send -a WonderUpdate -i wonderupdate -t 30000 \
        -A "install=$(t "Install now")" -A "close=$(t "Later")" \
        "$(t "Wonder OS %s is ready to install" "${tip:0:7}")" "$body" 2>/dev/null)"
    if [[ $answer == install ]]; then
        open_in_terminal --apply
    fi
    return 0
}

prepare_step() {
    auto_note "STEP $1/3 $2"
    main_msg "[$1/3] $2"
}

prepare_fail() {
    auto_note "ERROR $*"
    error_msg "$*"
}

# `wonderupdate --prepare`: downloads and builds the new version, installing
# nothing. What maybe_prepare runs in the background.
run_prepare() {
    if ! read_stamp; then
        error_msg "$(t "This install has no version stamp; run install.sh once to get automatic updates")"
        exit 11
    fi
    # Not take_lock: this one is let go of before the notification, which
    # waits for an answer while the install it offers needs the lock.
    local lock_fd
    if ! exec {lock_fd}>"$tmpdir/prepare.lock" || ! flock -n "$lock_fd" || lock_is_held update; then
        error_msg "$(t "There's already a running instance of %s" "WonderUpdate")"
        exit 5
    fi
    export PATH="$HOME/.cargo/bin:$PATH"
    : >"$auto_log"
    : >"$last_build_file"

    prepare_step 1 "$(t "Downloading the code (%s)" "$st_branch")"
    if ! sync_managed >"$build_log" 2>&1; then
        prepare_fail "$(t "Could not download %s (details in %s)" "$st_repo" "$build_log")"
        exit 6
    fi
    local tip
    tip="$(managed_git rev-parse HEAD)"
    if same_commit "$st_commit" "$tip"; then
        info_msg "$(t "No update available")"
        return 0
    fi
    if ! installed_is_published; then
        mark_development
        prepare_fail "$(t "The installed version has commits that are not published; it is not updated automatically")"
        exit 6
    fi

    prepare_step 2 "$(t "Building %s (this takes a few minutes)" "${tip:0:7}")"
    if ! (cd "$managed_src" && bash ./install.sh --build-only "${st_opts[@]}") >>"$build_log" 2>&1; then
        printf '%s\n' "$tip" >"$failed_build_file"
        prepare_fail "$(t "The build failed (details in %s); the update can still be installed from a terminal" "$build_log")"
        exit 6
    fi

    printf '%s\n' "$tip" >"$prepared_file"
    rm -f "$failed_build_file"
    auto_note "READY $tip"
    prepare_step 3 "$(t "Ready: install it with %s" "$name --apply")"
    exec {lock_fd}>&-
    notify_ready "$tip"
}

# Installs the tip of the branch from WonderUpdate's clone, with the options of
# the install it replaces. Quick when the background build already ran.
upgrade_wonderland_managed() {
    if lock_is_held prepare; then
        info_msg "$(t "Waiting for the background build to finish...")"
        wait_lock prepare 3600 || true
    else
        take_lock prepare || true
    fi
    export PATH="$HOME/.cargo/bin:$PATH"
    if ! sync_managed; then
        failed_sources+=("Wonderland")
        echo
        error_msg "$(t "The %s update failed" "Wonderland")"
        echo
        return 0
    fi
    if ! installed_is_published; then
        mark_development
        warning_msg "$(t "The installed version has commits that are not published; it is not updated automatically")"
        echo
        return 0
    fi
    if [[ -n $st_dirty ]]; then
        warning_msg "$(t "The installed version was built with uncommitted changes; the published version replaces it")"
    fi
    local tip
    tip="$(managed_git rev-parse HEAD)"
    if (cd "$managed_src" && bash ./install.sh --yes "${st_opts[@]}"); then
        auto_note "APPLIED $tip"
        rm -f "$prepared_file" "$failed_build_file"
    else
        failed_sources+=("Wonderland")
        echo
        error_msg "$(t "The %s update failed" "Wonderland")"
    fi
    echo
}

# `wonderupdate --apply`: the desktop's own update and nothing else; what the
# "Install now" button runs in a terminal.
run_apply() {
    if ! read_stamp; then
        error_msg "$(t "This install has no version stamp; run install.sh once to get automatic updates")"
        quit_msg
        exit 11
    fi
    if ! take_lock update; then
        error_msg "$(t "There's already a running instance of %s" "WonderUpdate")"
        quit_msg
        exit 5
    fi
    main_msg "$(t "Looking for updates...")"
    echo
    source_errors=() repo_updates=() aur_updates=() flatpak_updates=()
    query_wonderland
    if [[ ${#source_errors[@]} -gt 0 ]]; then
        warning_msg "$(t "Could not check %s (timeout or network error)" "Wonderland")"
        quit_msg
        exit 6
    fi
    if [[ ${#wl_updates[@]} -eq 0 ]]; then
        info_msg "$(t "No update available")"
        quit_msg
        return 0
    fi
    print_wonderland
    apply_updates
    refresh_status
    session_notice
    quit_msg
    [[ ${#failed_sources[@]} -eq 0 ]] || exit 6
}
