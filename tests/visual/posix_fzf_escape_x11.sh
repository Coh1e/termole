#!/bin/sh
# Run under Xvfb with xdotool, ImageMagick, tesseract, and fzf installed.
set -eu
app=$(realpath "$1")
evidence=$(realpath -m "$2")
mkdir -p "$evidence"
home=$(mktemp -d)
SHELL=$(getent passwd "$(id -u)" | cut -d: -f7)
export SHELL
cd "$home"
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

command_line() {
    xdotool type --clearmodifiers --delay 12 "$1"
    xdotool key Return
}

wait_for_text() {
    stage=$1
    pattern=$2
    for attempt in $(seq 1 40); do
        import -silent -window "$window" "$evidence/$stage.png"
        tesseract "$evidence/$stage.png" stdout --psm 6 > "$evidence/$stage.txt" 2>/dev/null
        grep -Eiq "$pattern" "$evidence/$stage.txt" && return 0
        sleep 0.1
    done
    cat "$evidence/$stage.txt" >&2
    return 1
}

# Split the marker in the command so only the output line satisfies the OCR.
command_line 'PS1="Ready "; stty size > grid; echo $$ > first; printf "\033[2J\033[H"; echo Garden mar"ker"'
wait_for_text marker '^Garden marker[[:space:]]*$'
shell_pid=$(cat first)
[ "$(readlink "/proc/$shell_pid/exe")" = "$(readlink -f "$SHELL")" ]
# Use a short command so its echo cannot scroll the marker out of view.
printf '%s\n' alpha bravo charlie > choices
command_line 'fzf < choices'
wait_for_text selector 'charlie'
xdotool type --clearmodifiers --delay 100 'bra'
wait_for_text filtered '1[[:space:]]*/[[:space:]]*3'
grep -iq 'bravo' "$evidence/filtered.txt"
grep -Eiq '>[[:space:]]*bra' "$evidence/filtered.txt"
xdotool key Escape
wait_for_text restored '^Ready([[:space:]]|$)'
grep -Eiq '^Garden marker[[:space:]]*$' "$evidence/restored.txt"
no_selector() {
    if grep -Eiq 'alpha|bravo|charlie|>[[:space:]]*bra|1[[:space:]]*/[[:space:]]*3' "$evidence/$1.txt"; then
        cat "$evidence/$1.txt" >&2
        exit 1
    fi
}
no_selector restored
read -r rows columns < grid
width=$(identify -format '%w' "$evidence/restored.png")
height=$(identify -format '%h' "$evidence/restored.png")
cw=$((width / columns))
ch=$((height / rows))
# With the screen cleared, the marker is row zero, the launch command row
# one, and the next prompt row two. The SDL View paints an inverse block.
cursor_at() {
    x=$(($2 * cw + cw / 2 - 1))
    y=$((2 * ch + ch / 2 - 1))
    convert "$evidence/$1.png" -crop "3x3+$x+$y" +repage -colorspace Gray \
        -format '%[fx:mean]' info: | awk '{ exit !($1 > 0.6) }'
}
cursor_at restored 6
xdotool type --clearmodifiers --delay 40 'echo Willow'
wait_for_text typing 'Ready[[:space:]]+echo[[:space:]]+Willow'
no_selector typing
cursor_at typing 17
xdotool key Return
wait_for_text submitted '^Willow[[:space:]]*$'
command_line 'echo $$ > second; echo Shell rest"ored"'
wait_for_text followup '^Shell restored[[:space:]]*$'
cmp first second
command_line 'exit 37'
status=0
wait "$app_pid" || status=$?
[ "$status" -eq 37 ]
uname -sr
printf 'Default shell: %s, PID: %s\n' "$SHELL" "$shell_pid"
"$SHELL" --version | head -1
fzf --version
