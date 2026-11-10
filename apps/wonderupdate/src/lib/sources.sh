#!/usr/bin/env bash
# Where updates come from: the pacman repositories, the AUR, Flatpak and the
# Wonderland checkout the desktop was installed from. Each query fills its
# array with "name old -> new" lines and never needs root.
# shellcheck disable=SC2034,SC2154

repo_updates=() aur_updates=() flatpak_updates=() wl_updates=() wl_commits=()
source_errors=()
wl_source="" wl_ahead=0

query_repo() {
    repo_updates=()
    have checkupdates || return 0
    local db="$cachedir/checkupdates-db" out rc
    mkdir -p "$db"
    # The caller holds the check lock, so a lock file left here is from a run
    # that was killed and would block checkupdates for good.
    rm -f "$db/db.lck"
    out="$(CHECKUPDATES_DB="$db" timeout "$update_check_timeout" checkupdates --nocolor 2>/dev/null)"
    rc=$?
    # 2 means "nothing to update".
    if [[ $rc -ne 0 && $rc -ne 2 ]]; then
        source_errors+=("repo")
        return 0
    fi
    mapfile -t repo_updates < <(grep -- ' -> ' <<<"$out")
}

query_aur() {
    aur_updates=()
    [[ -n $aur_helper ]] || return 0
    local out rc
    # Helpers print progress on stderr and read the terminal if they can.
    out="$(LC_ALL=C timeout "$update_check_timeout" "$aur_helper" --color never "${devel_flag[@]}" -Qua </dev/null 2>/dev/null)"
    rc=$?
    if [[ $rc -eq 124 ]]; then
        source_errors+=("AUR")
        return 0
    fi
    mapfile -t aur_updates < <(sed 's/^ *//; s/  */ /g' <<<"$out" | grep -- ' -> ' | grep -v '\[ignored\]$')
}

query_flatpak() {
    flatpak_updates=()
    flatpak_enabled || return 0
    local rc
    # --foreground: from a TTY flatpak may ask polkit for a password.
    timeout --foreground "$update_check_timeout" flatpak update --appstream >/dev/null 2>&1
    rc=$?
    if [[ $rc -eq 124 ]]; then
        source_errors+=("Flatpak")
        return 0
    fi
    local -a masks
    mapfile -t masks < <(flatpak mask 2>/dev/null | tr -d ' ' | grep -v '^$')
    local app_name app_id app_version pattern
    while IFS=$'\t' read -r app_name app_id app_version; do
        [[ -n $app_id ]] || continue
        for pattern in "${masks[@]}"; do
            # shellcheck disable=SC2053  # masks are glob patterns
            [[ $app_id == $pattern ]] && continue 2
        done
        flatpak_updates+=("${app_name:-$app_id}"$'\t'"$app_version")
    done < <(flatpak remote-ls --updates --cached --columns=name,application,version 2>/dev/null)
}

# The checkout WonderUpdate (and the rest of the desktop) was installed from:
# the configured one, the one `make install` recorded, or install.sh's default.
find_wonderland_source() {
    wl_source=""
    [[ -z $no_wonderland ]] && have git || return 1
    local candidate recorded=""
    [[ -f $datadir/source ]] && recorded="$(<"$datadir/source")"
    for candidate in "$wonderland_source" "$recorded" "${WONDERLAND_SRC:-$HOME/.local/src/wonderlandwm}"; do
        [[ -n $candidate && -d $candidate ]] || continue
        if git -C "$candidate" rev-parse --git-dir >/dev/null 2>&1; then
            wl_source=$candidate
            return 0
        fi
    done
    return 1
}

query_wonderland() {
    wl_updates=() wl_commits=() wl_ahead=0 wl_managed="" wl_tip=""
    # An install that recorded its version updates from WonderUpdate's own
    # clone (auto.sh); an older one, from the checkout it was installed from.
    if read_stamp; then
        query_wonderland_managed
        return 0
    fi
    find_wonderland_source || return 0
    # Nothing to compare with on a detached HEAD or a branch that tracks nothing.
    git -C "$wl_source" rev-parse --verify -q '@{upstream}' >/dev/null 2>&1 || return 0
    if ! GIT_TERMINAL_PROMPT=0 timeout "$update_check_timeout" git -C "$wl_source" fetch --quiet 2>/dev/null; then
        source_errors+=("Wonderland")
        return 0
    fi
    local behind
    behind="$(git -C "$wl_source" rev-list --count 'HEAD..@{upstream}' 2>/dev/null)" || return 0
    [[ ${behind:-0} -gt 0 ]] || return 0
    wl_ahead="$(git -C "$wl_source" rev-list --count '@{upstream}..HEAD' 2>/dev/null || echo 0)"
    wl_updates=("wonderland $(git -C "$wl_source" rev-parse --short HEAD) -> $(git -C "$wl_source" rev-parse --short '@{upstream}')")
    # First parent only: on a branch that takes pull requests that is one line per change.
    mapfile -t wl_commits < <(git -C "$wl_source" log --first-parent --format=%s 'HEAD..@{upstream}' 2>/dev/null)
}

query_all() {
    source_errors=()
    query_repo
    query_aur
    query_flatpak
    query_wonderland
}

updates_count() {
    echo $((${#repo_updates[@]} + ${#aur_updates[@]} + ${#flatpak_updates[@]} + ${#wl_updates[@]}))
}

# One line per update, as saved between checks to tell whether anything changed.
updates_summary() {
    local line
    for line in "${repo_updates[@]}" "${aur_updates[@]}" "${wl_updates[@]}"; do
        if [[ -n $no_version ]]; then echo "${line%% *}"; else echo "$line"; fi
    done
    for line in "${flatpak_updates[@]}"; do
        if [[ -n $no_version ]]; then echo "${line%%$'\t'*}"; else echo "${line/$'\t'/ }"; fi
    done
}

json_updates() {
    local line name old new first=1
    printf '['
    for line in "$@"; do
        if [[ $line == *$'\t'* ]]; then
            name=${line%%$'\t'*} old="" new=${line#*$'\t'}
        else
            read -r name old _ new <<<"$line"
        fi
        [[ $first -eq 1 ]] || printf ','
        first=0
        printf '{"name":%s,"from":%s,"to":%s}' "$(json_string "$name")" "$(json_string "$old")" "$(json_string "$new")"
    done
    printf ']'
}

json_strings() {
    local s first=1
    printf '['
    for s in "$@"; do
        [[ $first -eq 1 ]] || printf ','
        first=0
        json_string "$s"
    done
    printf ']'
}

status_json() {
    printf '{"checked":%s,"count":%s,"helper":%s,' "$(date +%s)" "$(updates_count)" "$(json_string "$aur_helper")"
    printf '"repo":%s,' "$(json_updates "${repo_updates[@]}")"
    printf '"aur":%s,' "$(json_updates "${aur_updates[@]}")"
    printf '"flatpak":%s,' "$(json_updates "${flatpak_updates[@]}")"
    printf '"wonderland":%s,' "$(json_updates "${wl_updates[@]}")"
    printf '"commits":%s,' "$(json_strings "${wl_commits[@]}")"
    printf '"errors":%s}\n' "$(json_strings "${source_errors[@]}")"
}
