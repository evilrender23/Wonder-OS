#!/usr/bin/env bash
# The unattended check (what the timer and the bar run), the status file the
# bar reads, and the list printed in a terminal.
# shellcheck disable=SC2154

status_file="$statedir/status.json"
last_updates_file="$statedir/last_updates"

# Written in place rather than renamed over: the bar watches this very file.
write_status() {
    local json
    json="$(status_json)"
    printf '%s\n' "$json" >"$status_file"
}

notify_updates() {
    local count=$1 summary body
    [[ -z $no_notification ]] && have notify-send || return 0
    if [[ $count -eq 1 ]]; then
        summary="$(t "1 update available")"
    else
        summary="$(t "%s updates available" "$count")"
    fi
    body="$(head -n 12 "$last_updates_file")"
    if [[ $count -gt 12 ]]; then
        body+=$'\n'"$(t "... and %s more" "$((count - 12))")"
    fi
    local -a cmd=(notify-send -a WonderUpdate -i wonderupdate -t 20000
        -A "update=$(t "Update now")" -A "close=$(t "Close")" "$summary" "$body")
    # With actions notify-send waits for the answer. Under systemd the wait has
    # to happen here (the unit's processes die with it); from a shell it must not
    # hold the prompt.
    if [[ -n ${INVOCATION_ID:-} ]]; then
        if [[ $(timeout 600 "${cmd[@]}" 2>/dev/null) == update ]]; then
            open_in_terminal
        fi
    else
        (
            if [[ $(timeout 600 "${cmd[@]}" 2>/dev/null) == update ]]; then
                open_in_terminal
            fi
        ) >/dev/null 2>&1 </dev/null &
        disown
    fi
}

run_check() {
    # A check already under way will write the same answer; wait for it instead
    # of querying everything twice.
    if ! take_lock check; then
        wait_lock check $((update_check_timeout * 4)) || true
        [[ -n $json_output && -f $status_file ]] && cat "$status_file"
        return 0
    fi

    check_aur_helper 2>/dev/null
    query_all
    write_status

    local count previous=""
    count="$(updates_count)"
    [[ -f $last_updates_file ]] && previous="$(<"$last_updates_file")"
    updates_summary >"$last_updates_file"

    [[ -n $json_output ]] && cat "$status_file"

    maybe_prepare

    # Only what wasn't already announced is worth a notification.
    if [[ $count -gt 0 && $(<"$last_updates_file") != "$previous" ]]; then
        notify_updates "$count"
    fi
    return 0
}

print_updates() {
    local title=$1 line name
    shift
    [[ $# -gt 0 ]] || return 0
    main_msg "$title"
    for line in "$@"; do
        if [[ $line == *$'\t'* ]]; then
            name=${line%%$'\t'*}
            line="$name ${line#*$'\t'}"
        fi
        if [[ -n $no_version ]]; then
            echo "${line%% *}"
        else
            echo "$line"
        fi
    done
    echo
}

print_wonderland() {
    [[ ${#wl_updates[@]} -gt 0 ]] || return 0
    main_msg "$(t "Wonderland:")"
    echo "${wl_updates[0]}  $dim$(t "%s new change(s) in %s" "${#wl_commits[@]}" "$wl_source")$off"
    is_prepared && detail "$(t "already built: installing it is quick")"
    local i
    for ((i = 0; i < ${#wl_commits[@]} && i < 10; i++)); do
        detail "${wl_commits[i]}"
    done
    if [[ ${#wl_commits[@]} -gt 10 ]]; then
        detail "$(t "... and %s more" "$((${#wl_commits[@]} - 10))")"
    fi
    echo
}

# Queries every source and prints what it finds. Returns 1 with nothing to update.
list_updates() {
    main_msg "$(t "Looking for updates...")"
    echo
    query_all
    write_status
    updates_summary >"$last_updates_file"

    local failed what
    for failed in "${source_errors[@]}"; do
        what=$failed
        [[ $failed == repo ]] && what="$(t "the repositories")"
        warning_msg "$(t "Could not check %s (timeout or network error)" "$what")"
    done
    [[ ${#source_errors[@]} -eq 0 ]] || echo

    print_updates "$(t "Packages:")" "${repo_updates[@]}"
    print_updates "$(t "AUR Packages:")" "${aur_updates[@]}"
    print_updates "$(t "Flatpak Packages:")" "${flatpak_updates[@]}"
    print_wonderland

    [[ $(updates_count) -gt 0 ]]
}
