#!/usr/bin/env bash
# Shared by every WonderUpdate stage: paths, configuration, translated
# messages, prompts and the small helpers (privilege command, AUR helper,
# terminal, JSON).
# shellcheck disable=SC2034,SC2154  # set here and read by the other libraries, or the other way round

statedir="${XDG_STATE_HOME:-$HOME/.local/state}/$name"
cachedir="${XDG_CACHE_HOME:-$HOME/.cache}/$name"
tmpdir="${XDG_RUNTIME_DIR:-/tmp}/$name"
config_file="${XDG_CONFIG_HOME:-$HOME/.config}/$name/$name.conf"


no_color="" no_version="" no_aur="" no_flatpak="" no_wonderland="" no_notification="" no_news=""
news_num=5 news_timeout=10 update_check_timeout=30
old_packages_num=3 uninstalled_packages_num=0
aur_helper="" su_cmd="" diff_prog="" wonderland_source="" terminal=""
news_feed="https://archlinux.org/feeds/news/"
config_warnings=()

trim() {
    local s=$1
    s="${s#"${s%%[![:space:]]*}"}"
    printf '%s' "${s%"${s##*[![:space:]]}"}"
}

load_config() {
    [[ -f $config_file ]] || return 0
    local line key val
    while IFS= read -r line || [[ -n $line ]]; do
        line="$(trim "${line%%#*}")"
        [[ -n $line ]] || continue
        if [[ $line == *=* ]]; then
            key="$(trim "${line%%=*}")"
            val="$(trim "${line#*=}")"
        else
            key="$line"
            val=""
        fi
        case $key in
        NoColor) no_color=1 ;;
        NoVersion) no_version=1 ;;
        NoAUR) no_aur=1 ;;
        NoFlatpak) no_flatpak=1 ;;
        NoWonderland) no_wonderland=1 ;;
        NoNotification) no_notification=1 ;;
        NoNews) no_news=1 ;;
        NewsNum) config_number "$key" "$val" 1 && news_num=$val ;;
        NewsTimeout) config_number "$key" "$val" 1 && news_timeout=$val ;;
        UpdateCheckTimeout) config_number "$key" "$val" 1 && update_check_timeout=$val ;;
        KeepOldPackages) config_number "$key" "$val" 0 && old_packages_num=$val ;;
        KeepUninstalledPackages) config_number "$key" "$val" 0 && uninstalled_packages_num=$val ;;
        AURHelper)
            if [[ $val == paru || $val == yay ]]; then aur_helper=$val; else config_warnings+=("$key=$val"); fi
            ;;
        PrivilegeElevationCommand)
            if [[ $val == sudo || $val == sudo-rs || $val == doas || $val == run0 ]]; then
                su_cmd=$val
            else
                config_warnings+=("$key=$val")
            fi
            ;;
        DiffProg) diff_prog=$val ;;
        WonderlandSource) wonderland_source="${val/#\~/$HOME}" ;;
        Terminal) terminal=$val ;;
        NewsFeed) news_feed=$val ;;
        *) config_warnings+=("$line") ;;
        esac
    done <"$config_file"
}

config_number() {
    if [[ $2 =~ ^[0-9]+$ ]] && ((10#$2 >= $3)); then
        return 0
    fi
    config_warnings+=("$1=$2")
    return 1
}


lang=en
case "${LANGUAGE:-${LC_ALL:-${LC_MESSAGES:-${LANG:-}}}}" in
es*) lang=es ;;
esac

# Translated printf: the English format string is the key.
t() {
    local fmt=$1
    shift
    if [[ $lang == es && -n ${ES[$fmt]+x} ]]; then
        fmt=${ES[$fmt]}
    fi
    # shellcheck disable=SC2059
    printf -- "$fmt" "$@"
}

setup_colors() {
    if [[ -z $no_color && -z ${NO_COLOR:-} && -t 1 ]]; then
        bold=$'\e[1m' dim=$'\e[2m' blue=$'\e[1;34m' green=$'\e[1;32m' yellow=$'\e[1;33m' red=$'\e[1;31m' off=$'\e[0m'
        pacman_color=always
    else
        bold="" dim="" blue="" green="" yellow="" red="" off=""
        pacman_color=never
    fi
}

main_msg() { printf '%s==>%s %s%s%s\n' "$blue" "$off" "$bold" "$*" "$off"; }
info_msg() { printf '%s==>%s %s%s%s\n' "$green" "$off" "$bold" "$*" "$off"; }
warning_msg() { printf '%s==> %s:%s %s%s%s\n' "$yellow" "$(t "WARNING")" "$off" "$bold" "$*" "$off"; }
error_msg() { printf '%s==> %s:%s %s%s%s\n' "$red" "$(t "ERROR")" "$off" "$bold" "$*" "$off" >&2; }
detail() { printf '    %s\n' "$*"; }

# The answer is left in $answer; end of input counts as an empty answer.
ask() {
    answer=""
    read -rp "$blue->$off $bold$*$off " answer || echo
}

# "s" is accepted next to "y" so a Spanish «sí» works whatever the locale.
confirm_default_yes() {
    ask "$* $(t "[Y/n]")"
    [[ -z $answer || $answer == [YySs]* ]]
}

confirm_default_no() {
    ask "$* $(t "[y/N]")"
    [[ $answer == [YySs]* ]]
}

quit_msg() {
    # Keeps the terminal window open when it was opened just for this run.
    [[ -t 0 ]] || return 0
    read -n 1 -r -s -p "$green==>$off $bold$(t "Press \"enter\" to quit")$off " _ || true
    echo
}


have() { command -v "$1" >/dev/null 2>&1; }

check_su_cmd() {
    if [[ -n $su_cmd ]]; then
        have "$su_cmd" && return 0
        error_msg "$(t "The %s command set in %s is not installed" "$su_cmd" "$config_file")"
        exit 2
    fi
    local c
    for c in sudo sudo-rs doas run0; do
        if have "$c"; then
            su_cmd=$c
            return 0
        fi
    done
    error_msg "$(t "A privilege elevation command is required (sudo, sudo-rs, doas or run0)")"
    exit 2
}

check_aur_helper() {
    if [[ -n $no_aur ]]; then
        aur_helper=""
        return 0
    fi
    if [[ -n $aur_helper ]]; then
        have "$aur_helper" && return 0
        warning_msg "$(t "The %s AUR helper set in %s is not installed" "$aur_helper" "$config_file")"
        aur_helper=""
    fi
    local h
    for h in paru yay; do
        if have "$h"; then
            aur_helper=$h
            return 0
        fi
    done
}

flatpak_enabled() {
    [[ -z $no_flatpak ]] && have flatpak && [[ -n $(flatpak list --columns=application 2>/dev/null) ]]
}

# Prints the terminal to open and the flag that precedes the command to run.
find_terminal() {
    local candidate
    for candidate in "$terminal" "${TERMINAL:-}" wonderterm foot alacritty kitty wezterm ptyxis konsole gnome-terminal xterm; do
        if [[ -z $candidate ]] || ! have "${candidate%% *}"; then
            continue
        fi
        case ${candidate%% *} in
        gnome-terminal | ptyxis) echo "$candidate --" ;;
        wezterm) echo "$candidate start --" ;;
        *) echo "$candidate -e" ;;
        esac
        return 0
    done
    return 1
}

open_in_terminal() {
    local term
    if ! term="$(find_terminal)"; then
        error_msg "$(t "No terminal emulator found (set Terminal in %s)" "$config_file")"
        exit 3
    fi
    # shellcheck disable=SC2086  # the terminal and its flag are separate words
    setsid -f $term "$self" "$@" >/dev/null 2>&1 </dev/null
}

json_string() {
    local s=$1
    s=${s//\\/\\\\}
    s=${s//\"/\\\"}
    s=${s//$'\n'/\\n}
    s=${s//$'\t'/\\t}
    s=${s//$'\r'/}
    # Whatever control characters are left would make the document invalid.
    s=${s//[$'\001'-$'\037']/}
    printf '"%s"' "$s"
}

# Holds the lock for the rest of the process. Returns 1 if someone else has it.
take_lock() {
    local file="$tmpdir/$1.lock" fd
    exec {fd}>"$file" || return 1
    flock -n "$fd"
}

wait_lock() {
    local file="$tmpdir/$1.lock" fd
    exec {fd}>"$file" || return 1
    flock -w "$2" "$fd"
}


declare -A ES=(
    ["WARNING"]="AVISO"
    ["ERROR"]="ERROR"
    ["[Y/n]"]="[S/n]"
    ["[y/N]"]="[s/N]"
    ["Press \"enter\" to quit"]="Pulsa «Intro» para salir"
    ["Press \"enter\" to continue"]="Pulsa «Intro» para continuar"
    ["The %s command set in %s is not installed"]="La orden %s indicada en %s no está instalada"
    ["A privilege elevation command is required (sudo, sudo-rs, doas or run0)"]="Hace falta una orden para elevar privilegios (sudo, sudo-rs, doas o run0)"
    ["The %s AUR helper set in %s is not installed"]="El ayudante de AUR %s indicado en %s no está instalado"
    ["No terminal emulator found (set Terminal in %s)"]="No se encontró ningún emulador de terminal (define Terminal en %s)"
    ["Ignored in %s: %s"]="Ignorado en %s: %s"
    ["Unknown option: %s"]="Opción desconocida: %s"
    ["Try '%s --help'"]="Prueba «%s --help»"
    ["Looking for updates..."]="Buscando actualizaciones..."
    ["Could not check %s (timeout or network error)"]="No se pudo comprobar %s (tiempo agotado o error de red)"
    ["the repositories"]="los repositorios"
    ["Packages:"]="Paquetes:"
    ["AUR Packages:"]="Paquetes de AUR:"
    ["Flatpak Packages:"]="Paquetes Flatpak:"
    ["Wonderland:"]="Wonderland:"
    ["%s new change(s) in %s"]="%s cambio(s) nuevo(s) en %s"
    ["... and %s more"]="... y %s más"
    ["No update available"]="No hay actualizaciones"
    ["Proceed with update?"]="¿Continuar con la actualización?"
    ["The update has been aborted"]="Se ha cancelado la actualización"
    ["There's already a running instance of %s"]="Ya hay otra instancia de %s en marcha"
    ["Proceed anyway?"]="¿Continuar de todos modos?"
    ["Looking for recent Arch News..."]="Buscando noticias recientes de Arch..."
    ["Unable to retrieve recent Arch News; check %s before updating"]="No se pudieron obtener las noticias de Arch; consulta %s antes de actualizar"
    ["No recent Arch News found"]="No hay noticias recientes de Arch"
    ["Arch News:"]="Noticias de Arch:"
    ["[NEW]"]="[NUEVA]"
    ["Select the news to read (e.g. 1 3 5), 0 to read them all, or press \"enter\" to continue:"]="Elige las noticias que leer (p. ej. 1 3 5), 0 para leerlas todas, o pulsa «Intro» para continuar:"
    ["Invalid input"]="Entrada no válida"
    ["Updating Packages..."]="Actualizando paquetes..."
    ["Updating AUR Packages..."]="Actualizando paquetes de AUR..."
    ["Updating Flatpak Packages..."]="Actualizando paquetes Flatpak..."
    ["Updating Wonderland..."]="Actualizando Wonderland..."
    ["An error has occurred during the update; it has been aborted"]="Ha ocurrido un error durante la actualización; se ha cancelado"
    ["The %s update failed"]="La actualización de %s ha fallado"
    ["The update has been applied"]="La actualización se ha aplicado"
    ["Some updates failed; see the messages above"]="Algunas actualizaciones han fallado; revisa los mensajes anteriores"
    ["%s has local commits its upstream doesn't have; update it by hand (git pull)"]="%s tiene commits locales que no están en su origen; actualízalo a mano (git pull)"
    ["%s has no install.sh; update it by hand"]="%s no tiene install.sh; actualízalo a mano"
    ["Orphan Packages:"]="Paquetes huérfanos:"
    ["Remove these orphan packages (and the dependencies only they need) now?"]="¿Eliminar ahora estos paquetes huérfanos (y las dependencias que solo ellos necesitan)?"
    ["Removing Orphan Packages..."]="Eliminando paquetes huérfanos..."
    ["The removal failed"]="La eliminación ha fallado"
    ["The removal has been applied"]="La eliminación se ha aplicado"
    ["The removal hasn't been applied"]="No se ha eliminado nada"
    ["No orphan package found"]="No hay paquetes huérfanos"
    ["Unused Flatpak runtimes:"]="Runtimes de Flatpak sin usar:"
    ["Cached Packages:"]="Paquetes en caché:"
    ["%s old or uninstalled package(s) in the pacman cache"]="%s paquete(s) antiguos o desinstalados en la caché de pacman"
    ["Remove them from the cache now?"]="¿Eliminarlos ahora de la caché?"
    ["Cleaning the pacman cache..."]="Limpiando la caché de pacman..."
    ["No old or uninstalled cached package found"]="No hay paquetes antiguos ni desinstalados en la caché"
    ["Pacnew Files:"]="Archivos pacnew:"
    ["Process these files now?"]="¿Procesar ahora estos archivos?"
    ["Processing Pacnew Files..."]="Procesando archivos pacnew..."
    ["The pacnew files have been processed"]="Los archivos pacnew se han procesado"
    ["An error occurred while processing the pacnew files"]="Ha ocurrido un error al procesar los archivos pacnew"
    ["The pacnew files haven't been processed; consider doing it soon"]="Los archivos pacnew no se han procesado; conviene hacerlo pronto"
    ["No pacnew file found"]="No hay archivos pacnew"
    ["Reboot required:"]="Hace falta reiniciar:"
    ["The running kernel has been updated; a reboot is needed to use the new one"]="El kernel en uso se ha actualizado; hay que reiniciar para usar el nuevo"
    ["Reboot now?"]="¿Reiniciar ahora?"
    ["Rebooting in %s..."]="Reiniciando en %s..."
    ["The reboot failed"]="El reinicio ha fallado"
    ["Remember to reboot to finish the kernel update"]="Recuerda reiniciar para completar la actualización del kernel"
    ["No pending kernel update found"]="No hay actualizaciones de kernel pendientes"
    ["Session:"]="Sesión:"
    ["The compositor has been updated; log out and back in to run the new one"]="El compositor se ha actualizado; cierra la sesión y vuelve a entrar para usar el nuevo"
    ["Services:"]="Servicios:"
    ["These services need a restart to use the updated files:"]="Estos servicios necesitan reiniciarse para usar los archivos actualizados:"
    ["Select the services to restart (e.g. 1 3 5), 0 to restart them all, or press \"enter\" to skip:"]="Elige los servicios que reiniciar (p. ej. 1 3 5), 0 para reiniciarlos todos, o pulsa «Intro» para omitir:"
    ["%s restarted"]="%s reiniciado"
    ["%s could not be restarted"]="%s no se pudo reiniciar"
    ["No service was restarted; consider restarting them to finish the update"]="No se ha reiniciado ningún servicio; conviene reiniciarlos para completar la actualización"
    ["No service needs a restart"]="Ningún servicio necesita reiniciarse"
    ["1 update available"]="1 actualización disponible"
    ["%s updates available"]="%s actualizaciones disponibles"
    ["Update now"]="Actualizar ahora"
    ["Close"]="Cerrar"
    ["Automatic update checks enabled (at login, then every hour)"]="Comprobación automática activada (al iniciar sesión y luego cada hora)"
    ["Automatic update checks disabled"]="Comprobación automática desactivada"
    ["%s already exists (use --gen-config --force to overwrite it)"]="%s ya existe (usa --gen-config --force para sobrescribirlo)"
    ["%s has been created"]="Se ha creado %s"
    ["No configuration file yet; create one with: %s --gen-config"]="Aún no hay archivo de configuración; créalo con: %s --gen-config"
    ["Example configuration not found"]="No se encontró la configuración de ejemplo"
)
