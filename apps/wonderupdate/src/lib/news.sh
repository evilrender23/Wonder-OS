#!/usr/bin/env bash
# Arch News: updates that need manual intervention are announced there, so the
# recent ones are offered before upgrading.
# shellcheck disable=SC2154

news_titles=() news_dates=() news_links=() news_bodies=()
last_news_file="$statedir/last_news"

# One line per item: title, date, link and body separated by tabs, the body's
# paragraphs separated by \x01.
parse_feed() {
    tr '\n\r\t' '   ' | awk -v max="$1" '
        function field(item, tag,    start, end, s, e) {
            start = "<" tag ">"; end = "</" tag ">"
            s = index(item, start); e = index(item, end)
            if (!s || !e || e < s) return ""
            return substr(item, s + length(start), e - s - length(start))
        }
        function unescape(s) {
            gsub(/&lt;/, "<", s); gsub(/&gt;/, ">", s); gsub(/&quot;/, "\"", s)
            gsub(/&#39;/, "\047", s); gsub(/&apos;/, "\047", s); gsub(/&#x27;/, "\047", s)
            gsub(/&nbsp;/, " ", s); gsub(/&amp;/, "\\&", s)
            return s
        }
        function text(s) {
            s = unescape(s)
            gsub(/<\/(p|pre|li|ul|ol|h[1-6])>|<br *\/?>/, "\001", s)
            gsub(/<li>/, "- ", s)
            gsub(/<[^>]*>/, "", s)
            s = unescape(s)
            gsub(/[\033\002-\010\013-\037]/, "", s)
            gsub(/  +/, " ", s); gsub(/ *\001[ \001]*/, "\001", s)
            sub(/^[ \001]+/, "", s); sub(/[ \001]+$/, "", s)
            return s
        }
        BEGIN { RS = "<item>" }
        NR > 1 && NR - 1 <= max {
            title = text(field($0, "title")); gsub(/\001/, " ", title)
            date = field($0, "pubDate"); sub(/^[A-Za-z]+, */, "", date); sub(/ [0-9:]+ [+-][0-9]+ *$/, "", date)
            printf "%s\t%s\t%s\t%s\n", title, date, field($0, "link"), text(field($0, "description"))
        }'
}

fetch_news() {
    news_titles=() news_dates=() news_links=() news_bodies=()
    local feed title date link body
    feed="$(curl -fsSL --max-time "$news_timeout" "$news_feed" 2>/dev/null)" || return 1
    [[ $feed == *"<item>"* ]] || return 1
    while IFS=$'\t' read -r title date link body; do
        [[ -n $title ]] || continue
        news_titles+=("$title")
        news_dates+=("$date")
        news_links+=("$link")
        news_bodies+=("$body")
    done < <(parse_feed "$1" <<<"$feed")
    [[ ${#news_titles[@]} -gt 0 ]]
}

print_news_item() {
    local i=$1 width paragraph
    width=$(($(tput cols 2>/dev/null || echo 80) - 2))
    [[ $width -gt 100 ]] && width=100
    [[ $width -lt 40 ]] && width=40
    echo
    main_msg "${news_titles[i]}"
    printf '%s%s · %s%s\n\n' "$dim" "${news_dates[i]}" "${news_links[i]}" "$off"
    while IFS= read -r -d $'\001' paragraph || [[ -n $paragraph ]]; do
        [[ -n $paragraph ]] || continue
        fold -s -w "$width" <<<"$paragraph"
        echo
    done <<<"${news_bodies[i]}"
}

# Sets $selection to the zero-based indexes chosen among $1 numbered entries
# ("0" meaning all of them). Returns 1 on an empty answer, 2 on a bad one.
selection=()
read_selection() {
    local total=$1 n i
    selection=()
    [[ -n $answer ]] || return 1
    for n in $answer; do
        if [[ ! $n =~ ^[0-9]+$ ]] || ((10#$n > total)); then
            return 2
        fi
        if ((10#$n == 0)); then
            selection=()
            for ((i = 0; i < total; i++)); do selection+=("$i"); done
            return 0
        fi
        selection+=("$((10#$n - 1))")
    done
}

# $1: how many news to show. $2 = "update": stay quiet unless something was
# published since the last time, and never fail the update over it.
show_news() {
    local num=$1 mode=${2:-} last="" i tag
    if [[ $mode == update ]]; then
        [[ -z $no_news ]] && have curl || return 0
    fi
    [[ $mode == update ]] || main_msg "$(t "Looking for recent Arch News...")"
    if ! fetch_news "$num"; then
        [[ $mode == update ]] || echo
        warning_msg "$(t "Unable to retrieve recent Arch News; check %s before updating" "https://archlinux.org/news/")"
        echo
        [[ $mode == update ]] && return 0
        return 1
    fi

    [[ -f $last_news_file ]] && last="$(<"$last_news_file")"
    if [[ $mode == update && ${news_titles[0]} == "$last" ]]; then
        return 0
    fi

    [[ $mode == update ]] || echo
    main_msg "$(t "Arch News:")"
    tag="$(t "[NEW]")"
    local new=1
    for i in "${!news_titles[@]}"; do
        [[ ${news_titles[i]} == "$last" ]] && new=0
        if [[ $new -eq 1 ]]; then
            printf '%2d - %s %s(%s)%s %s%s%s\n' "$((i + 1))" "${news_titles[i]}" "$dim" "${news_dates[i]}" "$off" "$green" "$tag" "$off"
        else
            printf '%2d - %s %s(%s)%s\n' "$((i + 1))" "${news_titles[i]}" "$dim" "${news_dates[i]}" "$off"
        fi
    done
    echo
    printf '%s\n' "${news_titles[0]}" >"$last_news_file"

    while true; do
        ask "$(t "Select the news to read (e.g. 1 3 5), 0 to read them all, or press \"enter\" to continue:")"
        read_selection "${#news_titles[@]}"
        case $? in
        1) break ;;
        2)
            warning_msg "$(t "Invalid input")"
            continue
            ;;
        esac
        for i in "${selection[@]}"; do
            print_news_item "$i"
        done
        ask "$(t "Press \"enter\" to continue")"
        break
    done
    echo
}
