#!/usr/bin/env bash
# Runs WonderUpdate against fake pacman/AUR/Flatpak tools and a throwaway git
# checkout, in a private HOME: nothing on the real system is read or changed.
# shellcheck disable=SC2016  # the fake tools are written with single quotes on purpose

set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
wonderupdate="$here/../src/wonderupdate"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

fakes="$work/bin"
log="$work/calls.log"
mkdir -p "$fakes" "$work/home" "$work/run"

export HOME="$work/home"
export XDG_STATE_HOME="$HOME/.local/state" XDG_CACHE_HOME="$HOME/.cache" XDG_CONFIG_HOME="$HOME/.config"
export XDG_RUNTIME_DIR="$work/run"
export PATH="$fakes:/usr/bin:/bin"
export LANG=C LC_ALL=C NO_COLOR=1 WONDERLAND_SRC="$work/none" FAKE_LOG="$log" FAKE_DIR="$work"
unset LANGUAGE TERMINAL INVOCATION_ID EDITOR
git_env=(-c user.name=test -c user.email=test@example.org -c init.defaultBranch=main -c commit.gpgsign=false)

fake() {
    printf '#!/usr/bin/env bash\necho "%s $*" >>"$FAKE_LOG"\n%s\n' "$1" "$2" >"$fakes/$1"
    chmod +x "$fakes/$1"
}

reset_fakes() {
    rm -rf "$fakes" "$HOME" "$log" "$work"/state-* "$work/feed.xml"
    mkdir -p "$fakes" "$HOME"
    : >"$log"
    fake checkupdates 'printf "linux 6.1-1 -> 6.2-1\nbash 5.2-1 -> 5.3-1\n"'
    fake paru 'case "$*" in *-Qua*) echo "wonder-git r1 -> r2"; echo "held 1 -> 2 [ignored]" ;; esac'
    fake flatpak 'case "$1" in
        list) echo org.example.App ;;
        mask) echo "    org.masked.*" ;;
        remote-ls) printf "Example App\torg.example.App\t2.0\nMasked\torg.masked.Thing\t1.0\n" ;;
    esac'
    fake sudo '"$@"'
    fake pacman 'case "$*" in *-Qtdq*) [[ -f $FAKE_DIR/state-orphans ]] && cat "$FAKE_DIR/state-orphans" ;; esac; exit 0'
    fake paccache 'exit 0'
    fake pacdiff 'exit 0'
    fake notify-send 'echo close'
    fake curl 'cat "$FAKE_DIR/feed.xml" 2>/dev/null || exit 22'
    fake uname 'echo 6.1-1-test'
    fake systemctl 'exit 0'
    fake checkservices 'cat "$FAKE_DIR/state-services" 2>/dev/null; exit 0'
}

failures=0
total=0

ok() {
    total=$((total + 1))
    echo "ok   $1"
}

fail() {
    total=$((total + 1))
    failures=$((failures + 1))
    echo "FAIL $1"
    [[ -n ${2:-} ]] && printf '%s\n' "$2" | sed 's/^/       /'
}

expect_in() {
    if [[ $2 == *"$3"* ]]; then ok "$1"; else fail "$1" "missing «$3» in:"$'\n'"$2"; fi
}

expect_not_in() {
    if [[ $2 != *"$3"* ]]; then ok "$1"; else fail "$1" "unexpected «$3» in:"$'\n'"$2"; fi
}

expect_eq() {
    if [[ $2 == "$3" ]]; then ok "$1"; else fail "$1" "got «$2», expected «$3»"; fi
}

# Notifications are sent from the background when a shell runs the check.
logged() {
    for _ in {1..30}; do
        grep -q -- "$1" "$log" && return 0
        sleep 0.1
    done
    return 1
}

expect_logged() {
    if logged "$2"; then ok "$1"; else fail "$1" "«$2» was never called:"$'\n'"$(<"$log")"; fi
}

json_get() {
    python3 -c 'import json,sys; d=json.load(sys.stdin); print(eval(sys.argv[1], {"d": d}))' "$1"
}

reset_fakes
out="$("$wonderupdate" --check --json)"
expect_eq "check: valid JSON with every source counted" "$(json_get 'd["count"]' <<<"$out")" 4
expect_eq "check: repo packages parsed" "$(json_get 'd["repo"][0]' <<<"$out")" "{'name': 'linux', 'from': '6.1-1', 'to': '6.2-1'}"
expect_eq "check: ignored AUR packages left out" "$(json_get '[p["name"] for p in d["aur"]]' <<<"$out")" "['wonder-git']"
expect_eq "check: masked Flatpaks left out" "$(json_get '[p["name"] for p in d["flatpak"]]' <<<"$out")" "['Example App']"
expect_eq "check: helper reported" "$(json_get 'd["helper"]' <<<"$out")" paru
expect_logged "check: notification sent" "notify-send -a WonderUpdate"
expect_in "check: notification lists the packages" "$(<"$log")" "4 updates available"
expect_eq "status: last check kept for the bar" "$("$wonderupdate" --status --json)" "$out"
expect_in "status: plain list" "$("$wonderupdate" --status)" "bash 5.2-1 -> 5.3-1"

: >"$log"
"$wonderupdate" --check
expect_not_in "check: same updates aren't announced twice" "$(<"$log")" notify-send

fake checkupdates 'printf "linux 6.1-1 -> 6.3-1\n"'
: >"$log"
"$wonderupdate" --check
expect_logged "check: a changed list is announced again" notify-send

reset_fakes
fake checkupdates 'exit 2'
fake paru 'exit 0'
fake flatpak 'exit 0'
out="$("$wonderupdate" --check --json)"
expect_eq "check: nothing pending" "$(json_get 'd["count"]' <<<"$out")" 0
expect_not_in "check: no notification without updates" "$(<"$log")" notify-send
"$wonderupdate" --list >/dev/null
expect_eq "list: exit code 7 without updates" $? 7

reset_fakes
fake checkupdates 'exit 1'
out="$("$wonderupdate" --check --json)"
expect_eq "check: a failing source is reported, not counted" "$(json_get '(d["errors"], len(d["repo"]))' <<<"$out")" "(['repo'], 0)"

reset_fakes
fake checkupdates 'printf "weird\\\\pkg \"1\" -> 2\n"'
out="$("$wonderupdate" --check --json)"
expect_eq "check: odd characters still give valid JSON" "$(json_get 'd["repo"][0]["name"]' <<<"$out")" 'weird\pkg'

reset_fakes
mkdir -p "$XDG_CONFIG_HOME/wonderupdate"
printf 'NoAUR\nNoFlatpak # not here\nNoNotification\nNoVersion\nKeepOldPackages = x\nBogus = 1\n' >"$XDG_CONFIG_HOME/wonderupdate/wonderupdate.conf"
out="$("$wonderupdate" --check --json)"
expect_eq "config: NoAUR and NoFlatpak" "$(json_get '(d["count"], d["helper"])' <<<"$out")" "(2, '')"
expect_not_in "config: NoNotification" "$(<"$log")" notify-send
out="$("$wonderupdate" --list 2>&1)"
expect_in "config: NoVersion hides versions" "$out" $'\nlinux\nbash'
expect_in "config: bad values are reported" "$out" "KeepOldPackages=x"
expect_in "config: unknown keys are reported" "$out" "Bogus = 1"

reset_fakes
"$wonderupdate" --gen-config >/dev/null
expect_eq "gen-config: file created" "$(ls "$XDG_CONFIG_HOME/wonderupdate")" wonderupdate.conf
"$wonderupdate" --gen-config >/dev/null 2>&1
expect_eq "gen-config: doesn't overwrite" $? 10
out="$("$wonderupdate" --list 2>&1)"
expect_not_in "gen-config: the example has no active or unknown setting" "$out" "WARNING"

reset_fakes
upstream="$work/upstream.git"
checkout="$work/checkout"
rm -rf "$upstream" "$checkout" "$work/seed"
git "${git_env[@]}" init -q --bare "$upstream"
git "${git_env[@]}" clone -q "$upstream" "$work/seed" 2>/dev/null
(
    cd "$work/seed" || exit 1
    printf '#!/usr/bin/env bash\necho "install.sh $*" >>"$FAKE_LOG"\n' >install.sh
    chmod +x install.sh
    git add install.sh
    git "${git_env[@]}" commit -qm "First version"
    git push -q origin HEAD
)
git "${git_env[@]}" clone -q "$upstream" "$checkout"
(
    cd "$work/seed" || exit 1
    echo more >notes
    git add notes
    git "${git_env[@]}" commit -qm "Add a new widget"
    git push -q origin HEAD
)
export WONDERLAND_SRC="$checkout"
fake checkupdates 'exit 2'
fake paru 'exit 0'
fake flatpak 'exit 0'
out="$("$wonderupdate" --check --json)"
expect_eq "wonderland: pending commits found" "$(json_get '(len(d["wonderland"]), d["commits"])' <<<"$out")" "(1, ['Add a new widget'])"
out="$(printf 'y\n\n\n\n' | "$wonderupdate" 2>&1)"
expect_in "wonderland: listed before updating" "$out" "Add a new widget"
expect_eq "wonderland: checkout fast-forwarded" "$(git -C "$checkout" log -1 --format=%s)" "Add a new widget"
expect_in "wonderland: install.sh re-run" "$(<"$log")" "install.sh --yes"
expect_eq "wonderland: nothing pending afterwards" "$("$wonderupdate" --status --json | json_get 'd["count"]')" 0

(
    cd "$work/seed" || exit 1
    echo again >>notes
    git "${git_env[@]}" commit -qam "Upstream change"
    git push -q origin HEAD
)
(
    cd "$checkout" || exit 1
    echo mine >local
    git add local
    git "${git_env[@]}" commit -qm "Local work"
)
: >"$log"
out="$(printf 'y\n\n\n\n' | "$wonderupdate" 2>&1)"
expect_in "wonderland: local commits aren't touched" "$out" "has local commits"
expect_eq "wonderland: checkout left as it was" "$(git -C "$checkout" log -1 --format=%s)" "Local work"
expect_not_in "wonderland: install.sh not run then" "$(<"$log")" "install.sh"
export WONDERLAND_SRC="$work/none"

reset_fakes
out="$(printf 'n\n' | "$wonderupdate" 2>&1)"
expect_eq "update: declining exits with 4" $? 4
expect_not_in "update: declining upgrades nothing" "$(<"$log")" "pacman --color never -Syu"

reset_fakes
cat >"$work/feed.xml" <<'EOF'
<?xml version="1.0"?><rss><channel><title>Arch Linux: Recent news updates</title>
<item><title>Manual intervention &amp; care needed</title><link>https://archlinux.org/news/one/</link><description>&lt;p&gt;Run &lt;code&gt;pacman -Syu&lt;/code&gt; twice.&lt;/p&gt;
&lt;p&gt;Second paragraph.&lt;/p&gt;</description><pubDate>Wed, 21 Oct 2026 19:29:36 +0000</pubDate></item>
<item><title>Older news</title><link>https://archlinux.org/news/two/</link><description>&lt;p&gt;Old.&lt;/p&gt;</description><pubDate>Mon, 01 Jun 2026 10:00:00 +0000</pubDate></item>
</channel></rss>
EOF
echo unused-dep >"$work/state-orphans"
out="$(printf 'y\n1\n\ny\n' | "$wonderupdate" 2>&1)"
rc=$?
calls="$(<"$log")"
expect_eq "update: succeeds" "$rc" 0
expect_in "update: news offered first" "$out" "Manual intervention & care needed (21 Oct 2026) [NEW]"
expect_in "update: selected news printed without markup" "$out" "Run pacman -Syu twice."
expect_in "update: repositories upgraded" "$calls" "sudo pacman --color never -Syu"
expect_in "update: AUR upgraded" "$calls" "paru --color never -Sua"
expect_in "update: Flatpak upgraded" "$calls" "flatpak update"$'\n'
expect_in "update: orphans removed after asking" "$calls" "pacman --color never -Rns unused-dep"
expect_in "update: reboot offered when the running kernel is gone" "$out" "Reboot required"
expect_not_in "update: no reboot without a yes" "$calls" "systemctl reboot"
expect_eq "update: order is repo, AUR, Flatpak" \
    "$(grep -n -m1 -e 'pacman --color never -Syu' "$log" | cut -d: -f1 | head -1)" \
    "$(grep -n -e '-Syu' -e '-Sua' -e '^flatpak update$' "$log" | head -1 | cut -d: -f1)"

: >"$log"
rm -f "$work/state-orphans"
fake checkupdates 'printf "linux 6.1-1 -> 6.2-1\n"'
out="$(printf 'y\n' | "$wonderupdate" 2>&1)"
expect_not_in "update: news already seen aren't shown again" "$out" "Arch News"

reset_fakes
fake pacman 'case "$*" in *-Syu*) exit 1 ;; esac; exit 0'
out="$(printf 'y\n' | "$wonderupdate" 2>&1)"
expect_eq "update: a failed pacman upgrade exits with 6" $? 6
expect_not_in "update: nothing else is upgraded after it" "$(<"$log")" "-Sua"

reset_fakes
fake paru 'case "$*" in *-Qua*) echo "wonder-git r1 -> r2" ;; *) exit 1 ;; esac'
out="$(printf 'y\n\n\n\n' | "$wonderupdate" 2>&1)"
expect_eq "update: a failed AUR upgrade exits with 6" $? 6
expect_in "update: Flatpak still upgraded after an AUR failure" "$(<"$log")" "flatpak update"$'\n'

# --- Automatic updates: an install that recorded its version ----------------------
# A published repository whose install.sh is a fake: it logs how it was called
# and, on a real install, records the version the way the real one does.
stamp="$XDG_STATE_HOME/wonderlandwm/version.json"
managed="$HOME/.local/share/wonderos/source"
auto_state="$XDG_STATE_HOME/wonderupdate"

write_stamp() { # commit branch repo options
    mkdir -p "$(dirname "$stamp")"
    printf '{\n  "commit": "%s",\n  "date": "",\n  "branch": "%s",\n  "repo": "%s",\n  "options": "%s",\n  "dirty": false\n}\n' "$1" "$2" "$3" "$4" >"$stamp"
}

auto_setup() {
    reset_fakes
    fake checkupdates 'exit 2'
    fake paru 'exit 0'
    fake flatpak 'exit 0'
    rm -rf "$work/pub" "$work/pub.git" "$work/mine"
    git "${git_env[@]}" init -q "$work/pub"
    cat >"$work/pub/install.sh" <<'EOF'
#!/usr/bin/env bash
echo "install.sh $*" >>"$FAKE_LOG"
[[ -f $FAKE_DIR/state-build-fails ]] && exit 1
case " $* " in *" --build-only "*) exit 0 ;; esac
mkdir -p "$XDG_STATE_HOME/wonderlandwm"
printf '{\n  "commit": "%s",\n  "branch": "main",\n  "repo": "%s",\n  "options": "--no-greeter",\n  "dirty": false\n}\n' \
    "$(git rev-parse HEAD)" "$(git remote get-url origin)" >"$XDG_STATE_HOME/wonderlandwm/version.json"
EOF
    chmod +x "$work/pub/install.sh"
    git -C "$work/pub" add -A
    git -C "$work/pub" "${git_env[@]}" commit -qm "First version"
    installed="$(git -C "$work/pub" rev-parse HEAD)"
    git clone -q --bare "$work/pub" "$work/pub.git"
    git -C "$work/pub" remote add origin "$work/pub.git"
    write_stamp "$installed" main "$work/pub.git" "--no-greeter --fresh-config; rm -rf /"
}

publish() {
    echo "$1" >>"$work/pub/file"
    git -C "$work/pub" add -A
    git -C "$work/pub" "${git_env[@]}" commit -qm "$1"
    git -C "$work/pub" push -q origin main
}

wait_prepare() {
    for _ in {1..100}; do
        grep -q '^\(READY\|ERROR\)' "$auto_state/auto.log" 2>/dev/null && return 0
        sleep 0.1
    done
    return 1
}

auto_setup
out="$("$wonderupdate" --check --json)"
expect_eq "auto: up to date with the published branch" "$(json_get 'd["count"]' <<<"$out")" 0
sleep 0.3
expect_not_in "auto: nothing is built without a new version" "$(<"$log")" "install.sh"

publish "Add a new widget"
tip="$(git -C "$work/pub" rev-parse HEAD)"
out="$("$wonderupdate" --check --json)"
expect_eq "auto: the new version is found without a clone" "$(json_get 'd["wonderland"][0]["to"]' <<<"$out")" "${tip:0:7}"
wait_prepare
expect_in "auto: built in the background" "$(<"$log")" "install.sh --build-only --no-greeter"
expect_not_in "auto: only known options are repeated" "$(<"$log")" "fresh-config"
expect_not_in "auto: nothing is installed in the background" "$(<"$log")" "install.sh --yes"
expect_eq "auto: the build is recorded as ready" "$(grep '^READY' "$auto_state/auto.log")" "READY $tip"
expect_eq "auto: the clone is at the new version" "$(git -C "$managed" rev-parse HEAD)" "$tip"
expect_eq "auto: the installed version is still the old one" "$(sed -n 's/.*"commit": "\(.*\)",/\1/p' "$stamp")" "$installed"
expect_logged "auto: ready notification" "is ready to install"
out="$("$wonderupdate" --list 2>&1)"
expect_in "auto: the list says it is built" "$out" "already built"
expect_in "auto: the list shows the new commits" "$out" "Add a new widget"

: >"$log"
"$wonderupdate" --check
sleep 0.5
expect_not_in "auto: a built version isn't built again" "$(<"$log")" "install.sh"

out="$("$wonderupdate" --apply </dev/null 2>&1)"
expect_eq "auto: --apply succeeds" $? 0
expect_in "auto: --apply installs with the recorded options" "$(<"$log")" "install.sh --yes --no-greeter"
expect_eq "auto: the new version is the installed one" "$(sed -n 's/.*"commit": "\(.*\)",/\1/p' "$stamp")" "$tip"
expect_in "auto: the install is recorded" "$(<"$auto_state/auto.log")" "APPLIED $tip"
expect_eq "auto: nothing pending afterwards" "$("$wonderupdate" --status --json | json_get 'd["count"]')" 0
expect_not_in "auto: only the desktop is touched by --apply" "$(<"$log")" "pacman"

# The full update goes the same way, and a later version waits its turn.
publish "Second change"
tip="$(git -C "$work/pub" rev-parse HEAD)"
: >"$log"
"$wonderupdate" --check
sleep 0.5
expect_not_in "auto: builds are spaced out" "$(<"$log")" "install.sh"
out="$(printf 'y\n' | "$wonderupdate" 2>&1)"
expect_in "auto: the full update installs it" "$(<"$log")" "install.sh --yes --no-greeter"
expect_eq "auto: installed by the full update" "$(sed -n 's/.*"commit": "\(.*\)",/\1/p' "$stamp")" "$tip"

# A build that fails is not retried for the same version, and doesn't hide it.
auto_setup
mkdir -p "$XDG_CONFIG_HOME/wonderupdate"
printf 'AutoBuildInterval = 0\n' >"$XDG_CONFIG_HOME/wonderupdate/wonderupdate.conf"
publish "Broken build"
: >"$work/state-build-fails"
"$wonderupdate" --check
wait_prepare
expect_in "auto: a failed build is recorded" "$(<"$auto_state/auto.log")" "ERROR"
: >"$log"
out="$("$wonderupdate" --check --json)"
sleep 0.5
expect_not_in "auto: a failed build isn't retried" "$(<"$log")" "install.sh"
expect_eq "auto: the update is still offered" "$(json_get 'd["count"]' <<<"$out")" 1
rm -f "$work/state-build-fails"

# Turned off: found, listed, not built.
auto_setup
mkdir -p "$XDG_CONFIG_HOME/wonderupdate"
printf 'NoAutoUpdate\n' >"$XDG_CONFIG_HOME/wonderupdate/wonderupdate.conf"
publish "Something new"
out="$("$wonderupdate" --check --json)"
sleep 0.5
expect_eq "auto: NoAutoUpdate still finds the update" "$(json_get 'd["count"]' <<<"$out")" 1
expect_not_in "auto: NoAutoUpdate builds nothing" "$(<"$log")" "install.sh"

# A development install (commits nobody published) is left to its owner.
auto_setup
git clone -q "$work/pub.git" "$work/mine"
echo local >"$work/mine/local"
git -C "$work/mine" add -A
git -C "$work/mine" "${git_env[@]}" commit -qm "Local work"
write_stamp "$(git -C "$work/mine" rev-parse HEAD)" main "$work/pub.git" ""
"$wonderupdate" --prepare >/dev/null 2>&1
expect_eq "auto: --prepare refuses an unpublished install" $? 6
expect_not_in "auto: an unpublished install isn't built" "$(<"$log")" "install.sh"
out="$("$wonderupdate" --check --json)"
expect_eq "auto: an unpublished install isn't offered updates" "$(json_get 'd["count"]' <<<"$out")" 0

# A stamp that can't be trusted is as good as none.
auto_setup
write_stamp "$installed" '--upload-pack=touch $HOME/pwned' "$work/pub.git" ""
publish "Something new"
out="$("$wonderupdate" --check --json)"
expect_eq "auto: a malformed stamp is ignored" "$(json_get 'd["count"]' <<<"$out")" 0
"$wonderupdate" --apply </dev/null >/dev/null 2>&1
expect_eq "auto: --apply without a stamp exits with 11" $? 11
rm -f "$stamp"

reset_fakes
printf "%s\n" "-------8<---" "'sshd.service'" "'cups.service'" "-------8<---" >"$work/state-services"
out="$(printf '2\n' | "$wonderupdate" --services 2>&1)"
expect_in "services: the ones running old files are listed" "$out" " 1 - sshd.service"
expect_in "services: only the selected one is restarted" "$(grep 'systemctl restart' "$log")" "sudo systemctl restart cups.service"
expect_not_in "services: the others are left alone" "$(<"$log")" "restart sshd.service"
rm -f "$work/state-services"

reset_fakes
out="$("$wonderupdate" --news 2>&1)"
expect_eq "news: unreachable feed exits with 8" $? 8

reset_fakes
out="$(LANG=es_ES.UTF-8 LC_ALL='' "$wonderupdate" --list 2>&1)"
expect_in "i18n: Spanish messages" "$out" "Buscando actualizaciones..."
expect_in "i18n: Spanish section titles" "$out" "Paquetes de AUR:"

reset_fakes
fake wonderterm 'exit 0'
"$wonderupdate" --terminal
sleep 0.3
expect_in "terminal: opens the updater in a terminal" "$(<"$log")" "wonderterm -e $(realpath "$wonderupdate")"

"$wonderupdate" --nope >/dev/null 2>&1
expect_eq "cli: unknown options are rejected" $? 1

echo
echo "$((total - failures))/$total passed"
[[ $failures -eq 0 ]]
