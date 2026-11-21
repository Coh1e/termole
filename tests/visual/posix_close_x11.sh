#!/bin/sh
# Run in Xvfb with xdotool, ImageMagick, Tesseract, fzf and X11 development headers.
set -eu
app=$(realpath "$1")
evidence=$(realpath -m "$2")
mkdir -p "$evidence"
work=$(mktemp -d)
first=
second=
cleanup() {
    for pid in "$first" "$second"; do
        [ -z "$pid" ] || kill "$pid" 2>/dev/null || :
    done
    wait || :
    rm -rf "$work"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
export SHELL
SHELL=$(getent passwd "$(id -u)" | cut -d: -f7)
mkdir "$work/first" "$work/second"
printf '%s\n' alpha bravo charlie > "$work/first/candidates"
# Bare Xvfb has no window manager. Deliver the same WM_DELETE_WINDOW request
# a window manager sends when the user clicks the window's close button.
cat > "$work/close.c" <<'EOF'
#include <stdlib.h>
#include <X11/Xlib.h>
int main(int argc, char **argv) {
    Display *display = XOpenDisplay(NULL);
    if (!display || argc != 2) return 1;
    Window window = strtoul(argv[1], NULL, 10);
    XEvent event = {0};
    event.xclient.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = XInternAtom(display, "WM_PROTOCOLS", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = XInternAtom(display, "WM_DELETE_WINDOW", False);
    event.xclient.data.l[1] = CurrentTime;
    int sent = XSendEvent(display, window, False, NoEventMask, &event);
    XSync(display, False);
    XCloseDisplay(display);
    return !sent;
}
EOF
cc "$work/close.c" -lX11 -o "$work/close"

window_for() {
    timeout 15 xdotool search --all --sync --onlyvisible --pid "$1" --name '^Termole$'
}
enter() {
    xdotool windowraise "$1"
    xdotool windowfocus --sync "$1"
    xdotool type --clearmodifiers --delay 12 "$2"
    xdotool key Return
}
wait_file() {
    for attempt in $(seq 1 100); do
        [ ! -s "$1" ] || return 0
        sleep 0.1
    done
    echo "Missing shell result: $1" >&2
    return 1
}
capture_text() {
    for attempt in $(seq 1 25); do
        import -silent -window "$1" "$evidence/$2.png"
        tesseract "$evidence/$2.png" stdout --psm 6 > "$evidence/$2.txt" 2>/dev/null
        grep -Eiq "$3" "$evidence/$2.txt" && return 0
        sleep 0.1
    done
    cat "$evidence/$2.txt" >&2
    return 1
}

HOME="$work/first" SDL_VIDEO_DRIVER=x11 "$app" &
first=$!
first_window=$(window_for "$first")
HOME="$work/second" SDL_VIDEO_DRIVER=x11 "$app" &
second=$!
second_window=$(window_for "$second")
[ "$first_window" != "$second_window" ]

enter "$second_window" 'echo $$ > "$HOME/before"'
wait_file "$work/second/before"
enter "$first_window" 'echo $$ > "$HOME/shell"; sh -c '\''echo $$ > "$HOME/fzf"; exec fzf < "$HOME/candidates"'\'''
wait_file "$work/first/fzf"
shell_pid=$(cat "$work/first/shell")
fzf_pid=$(cat "$work/first/fzf")
survivor=$(cat "$work/second/before")
[ "$shell_pid" != "$survivor" ]
[ "$(readlink "/proc/$shell_pid/exe")" = "$(realpath "$SHELL")" ]
[ "$(readlink "/proc/$survivor/exe")" = "$(realpath "$SHELL")" ]
capture_text "$first_window" selector 'bravo'
[ "$(readlink "/proc/$fzf_pid/exe")" = "$(realpath "$(command -v fzf)")" ]
{
    uname -sr
    printf 'Shell=%s fzf=%s\n' "$SHELL" "$(fzf --version)"
    printf 'window1=%s shell1=%s fzf_pid=%s window2=%s shell2=%s\n' \
        "$first_window" "$shell_pid" "$fzf_pid" "$second_window" "$survivor"
} > "$evidence/processes.txt"
"$work/close" "$first_window"
wait "$first"
first=
for attempt in $(seq 1 100); do
    if [ ! -e "/proc/$shell_pid" ] && [ ! -e "/proc/$fzf_pid" ]; then break; fi
    sleep 0.1
done
[ ! -e "/proc/$shell_pid" ] && [ ! -e "/proc/$fzf_pid" ] || {
    echo 'Closing the first window left a shell or fzf process behind' >&2
    exit 1
}
enter "$second_window" 'echo $$ > "$HOME/after"; echo Second session rea"dy"'
wait_file "$work/second/after"
cmp "$work/second/before" "$work/second/after"
capture_text "$second_window" survivor '^Second session ready$'
printf 'First shell and fzf exited; surviving shell PID=%s\n' "$survivor" >> "$evidence/processes.txt"
enter "$second_window" 'exit'
wait "$second"
second=
