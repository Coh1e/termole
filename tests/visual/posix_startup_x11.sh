#!/bin/sh
# Run inside Xvfb. The unmodified public executable opens its own X11 window.
set -eu
app=$1
evidence=$2
mkdir -p "$evidence"
home=$(mktemp -d)
login_shell=$(getent passwd "$(id -u)" | cut -d: -f7)
unset SHELL
HOME=$home XDG_CONFIG_HOME=$home/config SDL_VIDEO_DRIVER=x11 "$app" &
app_pid=$!
cleanup() {
    kill "$app_pid" 2>/dev/null || :
    wait "$app_pid" 2>/dev/null || :
    rm -rf "$home"
}
trap cleanup EXIT
window=$(timeout 15 xdotool search --all --sync --onlyvisible --pid "$app_pid" --name '^Termole$')
xdotool windowfocus --sync "$window"

capture() {
    import -silent -window "$window" "$evidence/$1.png"
}

# Wait for the actual default shell and check that the prompt contains glyphs.
# The narrow cursor block alone fits on a single cell.
for attempt in $(seq 1 50); do
    capture prompt
    width=$(convert "$evidence/prompt.png" -fuzz 10% -trim -format '%w' info:)
    [ "$width" -gt 22 ] && break
    sleep 0.1
done
[ "$width" -gt 22 ]

type_command() {
    xdotool type --clearmodifiers --delay 12 "$1"
    xdotool key Return
}

type_command 'echo $$ > "$HOME/first"; echo Termole rea"dy"'
for attempt in $(seq 1 50); do
    capture ready
    tesseract "$evidence/ready.png" stdout --psm 6 > "$evidence/ready.txt" 2>/dev/null
    grep -Fxq 'Termole ready' "$evidence/ready.txt" && break
    sleep 0.1
done
grep -Fxq 'Termole ready' "$evidence/ready.txt"
shell_pid=$(cat "$home/first")
actual_shell=$(readlink "/proc/$shell_pid/exe")
[ "$actual_shell" = "$(readlink -f "$login_shell")" ]
type_command 'echo $$ > "$HOME/second"; exit 37'
status=0
wait "$app_pid" || status=$?
[ "$status" -eq 37 ]
cmp "$home/first" "$home/second"
uname -sr
printf 'Default shell: %s, PID: %s\n' "$login_shell" "$(cat "$home/first")"
