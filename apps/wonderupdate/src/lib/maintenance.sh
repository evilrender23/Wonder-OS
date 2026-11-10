#!/usr/bin/env bash
# What an upgrade leaves behind: orphans, old cached packages, pacnew files,
# services still running old code and a kernel that needs a reboot. Every
# stage only acts after asking.
# shellcheck disable=SC2154

orphan_packages() {
    local -a orphans
    mapfile -t orphans < <(pacman -Qtdq 2>/dev/null)
    if [[ ${#orphans[@]} -gt 0 ]]; then
        main_msg "$(t "Orphan Packages:")"
        printf '%s\n' "${orphans[@]}"
        echo
        if confirm_default_no "$(t "Remove these orphan packages (and the dependencies only they need) now?")"; then
            echo
            main_msg "$(t "Removing Orphan Packages...")"
            echo
            if "$su_cmd" pacman --color "$pacman_color" -Rns "${orphans[@]}"; then
                echo
                info_msg "$(t "The removal has been applied")"
            else
                echo
                error_msg "$(t "The removal failed")"
            fi
        else
            echo
            info_msg "$(t "The removal hasn't been applied")"
        fi
    else
        info_msg "$(t "No orphan package found")"
    fi
    echo

    if flatpak_enabled; then
        main_msg "$(t "Unused Flatpak runtimes:")"
        # flatpak lists them and asks by itself.
        flatpak uninstall --unused || true
        echo
    fi
}

cache_candidates() {
    paccache -dv "$@" 2>/dev/null | grep -c -E '\.pkg\.tar\.[a-z0-9]+$' || true
}

packages_cache() {
    have paccache || return 0
    local old uninstalled total
    old="$(cache_candidates -k "$old_packages_num")"
    uninstalled="$(cache_candidates -u -k "$uninstalled_packages_num")"
    total=$((old + uninstalled))
    if [[ $total -eq 0 ]]; then
        info_msg "$(t "No old or uninstalled cached package found")"
        echo
        return 0
    fi
    main_msg "$(t "Cached Packages:")"
    t "%s old or uninstalled package(s) in the pacman cache" "$total"
    echo
    echo
    if confirm_default_yes "$(t "Remove them from the cache now?")"; then
        echo
        main_msg "$(t "Cleaning the pacman cache...")"
        echo
        [[ $old -gt 0 ]] && "$su_cmd" paccache -r -k "$old_packages_num"
        [[ $uninstalled -gt 0 ]] && "$su_cmd" paccache -r -u -k "$uninstalled_packages_num"
    fi
    echo
}

pacnew_files() {
    have pacdiff || return 0
    local -a files
    mapfile -t files < <(pacdiff -o 2>/dev/null)
    if [[ ${#files[@]} -eq 0 ]]; then
        info_msg "$(t "No pacnew file found")"
        echo
        return 0
    fi
    main_msg "$(t "Pacnew Files:")"
    printf '%s\n' "${files[@]}"
    echo
    if confirm_default_yes "$(t "Process these files now?")"; then
        echo
        main_msg "$(t "Processing Pacnew Files...")"
        echo
        local -a diff_env=()
        [[ -n $diff_prog ]] && diff_env=("DIFFPROG=$diff_prog")
        # env sits after the privilege command: sudo and doas reset the environment.
        if "$su_cmd" env "${diff_env[@]}" pacdiff; then
            echo
            info_msg "$(t "The pacnew files have been processed")"
        else
            echo
            error_msg "$(t "An error occurred while processing the pacnew files")"
        fi
    else
        echo
        warning_msg "$(t "The pacnew files haven't been processed; consider doing it soon")"
    fi
    echo
}

# checkservices prints the units between two scissor lines, one per line and quoted.
services_to_restart() {
    "$su_cmd" checkservices -F -P -R -S -L 2>/dev/null | sed -n "s/^'\(.*\)'\$/\1/p"
}

restart_services() {
    local standalone=${1:-}
    if ! have checkservices; then
        return 0
    fi
    local -a services
    mapfile -t services < <(services_to_restart)
    if [[ ${#services[@]} -eq 0 ]]; then
        [[ -n $standalone ]] && info_msg "$(t "No service needs a restart")" && echo
        return 0
    fi
    main_msg "$(t "Services:")"
    t "These services need a restart to use the updated files:"
    echo
    local i
    for i in "${!services[@]}"; do
        printf '%2d - %s\n' "$((i + 1))" "${services[i]}"
    done
    echo
    while true; do
        ask "$(t "Select the services to restart (e.g. 1 3 5), 0 to restart them all, or press \"enter\" to skip:")"
        read_selection "${#services[@]}"
        case $? in
        1)
            echo
            warning_msg "$(t "No service was restarted; consider restarting them to finish the update")"
            break
            ;;
        2)
            warning_msg "$(t "Invalid input")"
            continue
            ;;
        esac
        echo
        for i in "${selection[@]}"; do
            if "$su_cmd" systemctl restart "${services[i]}"; then
                info_msg "$(t "%s restarted" "${services[i]}")"
            else
                error_msg "$(t "%s could not be restarted" "${services[i]}")"
            fi
        done
        break
    done
    echo
}

kernel_needs_reboot() {
    # pacman removes the running kernel's modules when it upgrades the package.
    [[ ! -d /usr/lib/modules/$(uname -r) ]]
}

session_notice() {
    [[ -n ${compositor_updated:-} ]] || return 0
    main_msg "$(t "Session:")"
    t "The compositor has been updated; log out and back in to run the new one"
    echo
    echo
}

kernel_reboot() {
    if ! kernel_needs_reboot; then
        return 0
    fi
    main_msg "$(t "Reboot required:")"
    t "The running kernel has been updated; a reboot is needed to use the new one"
    echo
    echo
    if confirm_default_no "$(t "Reboot now?")"; then
        echo
        local i
        for i in 5 4 3 2 1; do
            printf '\r%s' "$(t "Rebooting in %s..." "$i")"
            sleep 1
        done
        echo
        if ! systemctl reboot; then
            error_msg "$(t "The reboot failed")"
        fi
    else
        echo
        warning_msg "$(t "Remember to reboot to finish the kernel update")"
    fi
    echo
}
