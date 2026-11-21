#!/bin/sh
# Run with a logged-in desktop user. Capture only the window belonging to this launch.
set -eu
app=$1
driver=$2
evidence=$3
mkdir -p "$evidence"
"$driver" permissions
test_home=$(mktemp -d)
login_shell=$(dscl . -read "/Users/$(id -un)" UserShell | awk '{print $2}')
test -x "$login_shell"
sw_vers > "$evidence/platform.txt"
printf 'Default shell: %s\n' "$login_shell" >> "$evidence/platform.txt"
# Prevent zsh-newuser-install from opening in the empty test home.
touch "$test_home/.zshrc"
HOME="$test_home" XDG_CONFIG_HOME="$test_home/config" ZDOTDIR="$test_home" \
    SHELL="$login_shell" SDL_VIDEO_DRIVER=cocoa "$app" &
app_pid=$!
cleanup() {
    kill "$app_pid" 2>/dev/null || :
    wait "$app_pid" 2>/dev/null || :
    rm -rf "$test_home"
}
trap cleanup EXIT
for attempt in $(seq 1 60); do
    window=$("$driver" window "$app_pid") && break
    kill -0 "$app_pid"
    sleep 0.25
done
test -n "$window"

capture() {
    screencapture -x -o -l "$window" "$evidence/$1.png"
    "$driver" read "$evidence/$1.png" > "$evidence/$1.txt"
}

for attempt in $(seq 1 30); do
    capture prompt
    grep -Ev '^[[:space:]]*(Termole)?[[:space:]]*$' "$evidence/prompt.txt" && break
    sleep 0.1
done
grep -Eqv '^[[:space:]]*(Termole)?[[:space:]]*$' "$evidence/prompt.txt"
"$driver" type "$app_pid" 'echo $$ > "$HOME/first"; echo Termole rea"dy"'
for attempt in $(seq 1 30); do
    capture ready
    grep -Fxq 'Termole ready' "$evidence/ready.txt" && break
    sleep 0.1
done
cat "$evidence/ready.txt"
grep -Fxq 'Termole ready' "$evidence/ready.txt"
shell_pid=$(cat "$test_home/first")
test "$(ps -p "$shell_pid" -o ppid= | tr -d ' ')" = "$app_pid"
shell_name=$(ps -p "$shell_pid" -o comm=)
test "${shell_name##*/}" = "${login_shell##*/}"
printf 'Actual shell: %s, PID: %s\n' "$shell_name" "$shell_pid" >> "$evidence/platform.txt"
"$driver" type "$app_pid" 'echo $$ > "$HOME/second"; exit 37'
for attempt in $(seq 1 60); do
    kill -0 "$app_pid" 2>/dev/null || break
    sleep 0.25
done
if kill -0 "$app_pid" 2>/dev/null; then
    echo 'The application did not exit after the second command.' >&2
    exit 1
fi
status=0
wait "$app_pid" || status=$?
test "$status" -eq 37
cmp "$test_home/first" "$test_home/second"
cat "$evidence/platform.txt"
