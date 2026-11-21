#!/bin/sh
# Run inside Xvfb with the T1 executable and an output directory.
set -eu
app=$1
evidence=$2
mkdir -p "$evidence"
home=$(mktemp -d)
SHELL=$(getent passwd "$(id -u)" | cut -d: -f7)
export SHELL
HOME=$home XDG_CONFIG_HOME=$home/config SDL_VIDEO_DRIVER=x11 TERM=xterm-256color "$app" &
app_pid=$!
cleanup() {
    kill "$app_pid" 2>/dev/null || :
    wait "$app_pid" 2>/dev/null || :
    rm -rf "$home"
}
trap cleanup EXIT
fail() {
    printf '%s (evidence: %s)\n' "$1" "$evidence" >&2
    exit 1
}
window=$(timeout 15 xdotool search --sync --onlyvisible --all --pid "$app_pid" --name '^Termole$') ||
    fail "Termole window did not open"
xdotool windowfocus --sync "$window"
for attempt in $(seq 1 50); do
    import -silent -window "$window" "$evidence/prompt.png"
    ink_width=$(convert "$evidence/prompt.png" -fuzz 10% -trim -format '%w' info:)
    [ "$ink_width" -gt 22 ] && break
    sleep 0.1
done
[ "$ink_width" -gt 22 ] || fail "Default shell prompt did not appear"
command_in_shell() {
    xdotool type --clearmodifiers --delay 10 "$1"
    xdotool key Return
}
# Seed input via a file. The shell command itself contains no candidate text.
printf '%s\n' 'Pear Apple' 'Pear Lemon' 'Plum' > "$home/items"
command_in_shell 'echo $$ > "$HOME/start"; fzf --layout=reverse --no-sort --info=hidden --border --pointer=X --prompt="Query " --color=fg+:15,bg+:0,hl+:15,pointer:15 < "$HOME/items" > "$HOME/pick"'
# Restrict OCR to the top left. The bottom-right border is checked separately.
capture() {
    import -silent -window "$window" "$evidence/$1.png"
    convert "$evidence/$1.png" -crop 280x150+0+0 +repage -resize 200% "$home/text.png"
    tesseract "$home/text.png" stdout --psm 6 > "$evidence/$1.txt" 2>/dev/null
}
wait_text() {
    stage=$1
    pattern=$2
    for attempt in $(seq 1 35); do
        capture "$stage"
        grep -Eiq "$pattern" "$evidence/$stage.txt" && return 0
        sleep 0.1
    done
    cat "$evidence/$stage.txt" >&2
    fail "Missing '$pattern' in $stage"
}
wait_text initial 'Plum'
grep -Eiq 'Pear[[:space:]]+Apple' "$evidence/initial.txt" || fail 'First candidate missing initially'
actual_shell=$(readlink "/proc/$(cat "$home/start")/exe")
[ "$actual_shell" = "$(readlink -f "$SHELL")" ] || fail "Unexpected shell: $actual_shell"
xdotool type --clearmodifiers pea
xdotool key Down
check_selection() {
    wait_text "$1" 'X[[:space:]]*Pear[[:space:]]+Lemon'
    if grep -iq Plum "$evidence/$1.txt"; then fail "Filtered candidate remained after $1"; fi
    grep -Eiq 'Query[[:space:]]+pea' "$evidence/$1.txt" || fail "Filter missing after $1"
    grep -Eiq 'Pear[[:space:]]+Apple' "$evidence/$1.txt" || fail "First match missing after $1"
}
check_selection selected
for stage in small large; do
    case $stage in
        small) width=600; height=360 ;;
        large) width=1000; height=650 ;;
    esac
    xdotool windowsize --sync "$window" "$width" "$height"
    # Window geometry updates before fzf handles its terminal resize.
    # Poll the bottom edge until the border reaches the new drawable area.
    for attempt in $(seq 1 50); do
        import -silent -window "$window" "$evidence/$stage.png"
        colors=$(convert "$evidence/$stage.png" -gravity SouthEast -crop 80x45+40+0 +repage -format '%k' info:)
        [ "$colors" -gt 1 ] && break
        sleep 0.1
    done
    check_selection "$stage"
    size=$(identify -format '%wx%h' "$evidence/$stage.png")
    [ "$size" = "${width}x${height}" ] || fail "Unexpected $stage capture size: $size"
    # fzf draws a full-height border; find its horizontal stroke near the
    # bottom edge, away from the vertical border and window decorations.
    colors=$(convert "$evidence/$stage.png" -gravity SouthEast -crop 80x45+40+0 +repage -format '%k' info:)
    [ "$colors" -gt 1 ] || fail "Missing bottom fzf border at $size"
done
xdotool key Return
command_in_shell 'echo $$ > "$HOME/end"'
for attempt in $(seq 1 50); do
    [ -s "$home/end" ] && break
    sleep 0.1
done
[ -s "$home/end" ] || fail "Shell did not execute follow-up"
cmp "$home/start" "$home/end" || fail "Shell PID changed"
printf '%s\n' 'Pear Lemon' > "$home/expected"
cmp "$home/expected" "$home/pick" || fail "Enter returned the wrong candidate"
uname -sr
printf 'Shell: %s, PID: %s\n' "$SHELL" "$(cat "$home/end")"
"$SHELL" --version | head -n 1
fzf --version
