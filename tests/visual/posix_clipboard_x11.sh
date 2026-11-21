#!/bin/sh
# Run: xvfb-run -a sh tests/visual/posix_clipboard_x11.sh /path/to/termole evidence
# Requires xdotool, ImageMagick import, Python 3 with Tk, and a UTF-8 locale.
set -eu
app=$(realpath "$1")
mkdir -p "$2"
evidence=$(realpath "$2")
home=$(mktemp -d)
receiver_pid=
app_pid=
cleanup() {
    for pid in "$app_pid" "$receiver_pid"; do
        [ -z "$pid" ] || kill "$pid" 2>/dev/null || :
        [ -z "$pid" ] || wait "$pid" 2>/dev/null || :
    done
    rm -rf "$home"
}
trap cleanup EXIT
export LC_ALL=C.UTF-8
SHELL=$(getent passwd "$(id -u)" | cut -d: -f7)
export SHELL
printf '%s' 'Termole 中文 test' > "$home/expected"
HOME=$home XDG_CONFIG_HOME=$home/config SDL_VIDEO_DRIVER=x11 "$app" &
app_pid=$!
window=$(timeout 15 xdotool search --all --sync --onlyvisible --pid "$app_pid" --name '^Termole$')
xdotool windowfocus --sync "$window"
# Use ASCII keystrokes for the shell command. The specimen itself comes from
# the PTY output, avoiding X keyboard-layout and input-method dependencies.
xdotool type --clearmodifiers 'printf "\033[2J\033[H"; cat "$HOME/expected"; echo; stty size > "$HOME/grid"; echo $$ > "$HOME/shellpid"'
xdotool key Return
for attempt in $(seq 1 100); do
    [ -s "$home/shellpid" ] && break
    sleep 0.1
done
actual_shell=$(readlink "/proc/$(cat "$home/shellpid")/exe")
[ "$actual_shell" = "$(readlink -f "$SHELL")" ]
read -r rows columns < "$home/grid"
width=$(xdotool getwindowgeometry --shell "$window" | sed -n 's/^WIDTH=//p')
height=$(xdotool getwindowgeometry --shell "$window" | sed -n 's/^HEIGHT=//p')
cw=$((width / columns))
ch=$((height / rows))
# Fifteen codepoints occupy seventeen cells, counting each CJK glyph twice.
xdotool mousemove --window "$window" "$((cw / 2))" "$((ch / 2))" mousedown 1 \
    mousemove --window "$window" "$((16 * cw + cw / 2))" "$((ch / 2))" mouseup 1
xdotool key --clearmodifiers ctrl+shift+c
sleep 0.3
import -silent -window "$window" "$evidence/selection.png"

# A separate Tk process receives the desktop paste through its Entry widget.
python3 - "$home/received" <<'PY' &
import pathlib
import sys
import tkinter as tk

root = tk.Tk()
root.title("Termole clipboard recipient")
entry = tk.Entry(root, width=36, font=("sans", 18))
entry.pack(padx=12, pady=12)
entry.focus_set()
entry.bind("<Return>", lambda event: pathlib.Path(sys.argv[1]).write_bytes(entry.get().encode("utf-8")))
root.mainloop()
PY
receiver_pid=$!
recipient=$(timeout 15 xdotool search --sync --onlyvisible --name '^Termole clipboard recipient$')
xdotool windowraise "$recipient" windowfocus --sync "$recipient"
xdotool mousemove --window "$recipient" 30 25 click 1
xdotool key --clearmodifiers ctrl+v
xdotool key Return
for attempt in $(seq 1 50); do
    [ -f "$home/received" ] && break
    sleep 0.1
done
import -silent -window "$recipient" "$evidence/pasted.png"
cp "$home/received" "$evidence/received.txt"
cmp "$home/expected" "$home/received"
# Return to the original View and paste through its key handler. The
# foreground program consumes a line directly from the same PTY.
xdotool windowraise "$window" windowfocus --sync "$window"
cat > "$home/recipient.py" <<'PY'
import pathlib
import sys

home = pathlib.Path.home()
(home / "ready").touch()
(home / "pty-received").write_bytes(sys.stdin.buffer.readline())
PY
xdotool type --clearmodifiers 'python3 "$HOME/recipient.py"'
xdotool key Return
for attempt in $(seq 1 100); do
    [ -e "$home/ready" ] && break
    sleep 0.1
done
test -e "$home/ready"
xdotool key --clearmodifiers ctrl+shift+v
sleep 0.3
import -silent -window "$window" "$evidence/view-paste.png"
xdotool key Return
for attempt in $(seq 1 50); do
    [ -s "$home/pty-received" ] && break
    sleep 0.1
done
cp "$home/pty-received" "$evidence/pty-received.txt"
{ cat "$home/expected"; printf '\n'; } > "$home/expected-line"
cmp "$home/expected-line" "$evidence/pty-received.txt"
uname -sr
printf 'Shell: %s (PID %s)\n' "$actual_shell" "$(cat "$home/shellpid")"
"$actual_shell" --version 2>/dev/null | head -1
od -An -tx1 "$home/received"
