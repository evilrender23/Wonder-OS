#!/usr/bin/env bash
# Applying the updates, one source after the other. A failed pacman upgrade
# stops everything; the other sources are independent of each other.
# shellcheck disable=SC2034,SC2154

failed_sources=()
compositor_bin="$(command -v driftwm 2>/dev/null || true)"

compositor_stamp() {
    [[ -n $compositor_bin && -e $compositor_bin ]] && stat -Lc '%Y %s' "$compositor_bin" 2>/dev/null
}

upgrade_repo() {
    main_msg "$(t "Updating Packages...")"
    echo
    if ! "$su_cmd" pacman --color "$pacman_color" -Syu; then
        echo
        error_msg "$(t "An error has occurred during the update; it has been aborted")"
        return 1
    fi
    echo
}

upgrade_aur() {
    main_msg "$(t "Updating AUR Packages...")"
    echo
    # -a: the repositories were upgraded just above.
    if ! "$aur_helper" --color "$pacman_color" "${devel_flag[@]}" -Sua; then
        failed_sources+=("AUR")
        echo
        error_msg "$(t "The %s update failed" "AUR")"
    fi
    echo
}

upgrade_flatpak() {
    main_msg "$(t "Updating Flatpak Packages...")"
    echo
    if ! flatpak update; then
        failed_sources+=("Flatpak")
        echo
        error_msg "$(t "The %s update failed" "Flatpak")"
    fi
    echo
}

upgrade_wonderland() {
    main_msg "$(t "Updating Wonderland...")"
    echo
    # Fast-forward only: local work in the checkout is never rewritten or merged.
    if [[ $wl_ahead -gt 0 ]]; then
        warning_msg "$(t "%s has local commits its upstream doesn't have; update it by hand (git pull)" "$wl_source")"
        echo
        return 0
    fi
    if [[ ! -x $wl_source/install.sh ]]; then
        warning_msg "$(t "%s has no install.sh; update it by hand" "$wl_source")"
        echo
        return 0
    fi
    if ! git -C "$wl_source" pull --ff-only || ! (cd "$wl_source" && ./install.sh --yes); then
        failed_sources+=("Wonderland")
        echo
        error_msg "$(t "The %s update failed" "Wonderland")"
    fi
    echo
}

# Runs the upgrade for every source that has something pending.
apply_updates() {
    failed_sources=()
    local stamp_before
    stamp_before="$(compositor_stamp)"

    if [[ ${#repo_updates[@]} -gt 0 ]]; then
        upgrade_repo || return 1
    fi
    [[ ${#aur_updates[@]} -gt 0 ]] && upgrade_aur
    [[ ${#flatpak_updates[@]} -gt 0 ]] && upgrade_flatpak
    [[ ${#wl_updates[@]} -gt 0 ]] && upgrade_wonderland

    compositor_updated=""
    [[ $(compositor_stamp) != "$stamp_before" ]] && compositor_updated=1

    if [[ ${#failed_sources[@]} -gt 0 ]]; then
        warning_msg "$(t "Some updates failed; see the messages above")"
    else
        info_msg "$(t "The update has been applied")"
    fi
    echo
    return 0
}

# What is left after the upgrade, for the bar; nothing new to announce.
refresh_status() {
    take_lock check || return 0
    query_all >/dev/null 2>&1
    write_status
    updates_summary >"$last_updates_file"
}
