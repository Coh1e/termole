"""Run one native Cocoa acceptance scenario after the ordinary default-shell test."""

import argparse
from contextlib import ExitStack
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
import uuid


def output(*args):
    return subprocess.check_output(args, text=True).strip()


def wait_for(description, check, seconds=12):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(0.15)
    raise RuntimeError(f"Timed out waiting for {description}")


def geometry(lines, label):
    for y, text in lines:
        if text == label:
            return y
    return None


class Window:
    def __init__(self, stack, args, name, shell):
        self.driver = args.driver
        self.evidence = args.evidence
        self.home = Path(stack.enter_context(tempfile.TemporaryDirectory()))
        (self.home / ".zshrc").touch()
        env = {**os.environ, "HOME": str(self.home), "ZDOTDIR": str(self.home),
               "XDG_CONFIG_HOME": str(self.home / "config"), "SDL_VIDEO_DRIVER": "cocoa",
               "SHELL": shell, "FZF_DEFAULT_OPTS": "", "FZF_DEFAULT_OPTS_FILE": "/dev/null"}
        self.name = name
        self.app = subprocess.Popen([args.app], cwd=self.home, env=env)
        stack.callback(self.cleanup)
        self.wid = wait_for("visible " + name, self.find_window)
        self.command('echo $$ > "$HOME/shellpid"')
        try:
            self.shellpid = int(wait_for("shell PID", lambda: self.read("shellpid")))
        except RuntimeError:
            print(self.capture("shell-pid-timeout"), flush=True)
            raise
        if output("ps", "-p", str(self.shellpid), "-o", "ppid=") != str(self.app.pid):
            raise RuntimeError("Shell process does not belong to the launched window")
        actual = output("ps", "-p", str(self.shellpid), "-o", "comm=")
        metadata = {"window_id": self.wid, "app_pid": self.app.pid, "shell_pid": self.shellpid,
                    "shell_executable": actual}
        (self.evidence / f"{name}-session.json").write_text(json.dumps(metadata, indent=2) + "\n")
        if Path(actual).name != Path(shell).name:
            raise RuntimeError(f"Unexpected shell {actual}")

    def cleanup(self):
        if self.app.poll() is None:
            self.app.terminate()
            try:
                self.app.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.app.kill()
                self.app.wait()

    def find_window(self):
        if self.app.poll() is not None:
            raise RuntimeError(f"Application exited with {self.app.returncode}")
        match = subprocess.run([self.driver, "window", str(self.app.pid)],
                               capture_output=True, text=True)
        return match.stdout.strip() if match.returncode == 0 else None

    def action(self, name, *args):
        return output(self.driver, name, str(self.app.pid), *(str(arg) for arg in args))

    def command(self, line):
        self.action("type", line)

    def read(self, name):
        path = self.home / name
        return path.read_text().strip() if path.exists() else None

    def capture(self, stage):
        base = self.evidence / f"{self.name}-{stage}"
        png = base.with_suffix(".png")
        subprocess.run(["screencapture", "-x", "-o", "-l", self.wid, png], check=True)
        raw = output(self.driver, "lines", png)
        base.with_suffix(".txt").write_text(raw + "\n")
        lines = []
        for line in raw.splitlines():
            y, text = line.split("\t", 1)
            lines.append((float(y), text.strip()))
        return lines

    def see(self, stage, condition):
        return wait_for(stage, lambda: condition(self.capture(stage)))

    def continued(self, label):
        self.command(f'echo $$ > "$HOME/again"; echo {label[:-1]}"{label[-1]}"')
        self.see("continued", lambda lines: geometry(lines, label))
        assert int(self.read("again")) == self.shellpid


def clipboard(window):
    text = "Termole 中文 test"
    fixture = text.encode("utf-8")
    # Separate fixture input from the user's selection and the foreground receiver.
    (window.home / "source").write_bytes(fixture + b"\n")
    receiver = window.home / "received"
    window.command('stty size > size; printf "\\033[2J\\033[H"; cat source; head -n 1 > received')
    y = window.see("source", lambda lines: next((y for y, s in lines
                   if "Termole" in s and "test" in s), None))
    _, cols = map(int, window.read("size").split())
    subprocess.run(["pbcopy"], input=uuid.uuid4().hex.encode(), check=True)
    # Two CJK characters occupy four cells; select through the final t.
    window.action("drag", 0.5 / cols, 16.5 / cols, y)
    window.capture("selection")
    window.action("key", "copy")
    wait_for("exact selection on pasteboard", lambda: subprocess.check_output(["pbpaste"]) == fixture)
    window.action("key", "paste")
    window.action("key", "enter")
    wait_for("foreground program input", lambda: receiver.exists() and receiver.stat().st_size)
    actual = receiver.read_bytes()
    (window.evidence / "received.bin").write_bytes(actual)
    assert actual == fixture + b"\n", f"Received {actual!r}"
    window.capture("pasted")


def fzf(window):
    (window.home / "options").write_text("Amber alpha\nAmber bravo\nViolet cobalt\n")
    window.command("fzf --layout=default --no-sort --pointer=Z --prompt='Find ' < options > picked")
    window.see("initial-fzf", lambda lines: any("Violet cobalt" in s for _, s in lines))
    kids = output("pgrep", "-P", str(window.shellpid)).splitlines()
    matching = [int(pid) for pid in kids if Path(output("ps", "-p", pid, "-o", "comm=")).name == "fzf"]
    if len(matching) != 1:
        raise RuntimeError(f"Expected one foreground fzf child, found {matching}")
    return matching[0]


def fzf_frame(lines, selected):
    texts = [s for _, s in lines]
    query = geometry(lines, "Find Amber")
    # Default fzf layout anchors the query at the bottom. A stale old-size frame
    # in a larger window puts it far from the new bottom edge.
    return (query is not None and query < 0.12
            and any("Amber alpha" in s for s in texts) and any("Amber bravo" in s for s in texts)
            and all("Violet cobalt" not in s for s in texts)
            and any(re.fullmatch(r"Z\s*" + selected, s) for s in texts))


def resize(window):
    fzf(window)
    window.action("text", "Amber")
    window.see("filtered", lambda lines: fzf_frame(lines, "Amber alpha"))
    window.action("key", "up")
    window.see("selected", lambda lines: fzf_frame(lines, "Amber bravo"))
    for stage, width, height in (("small", 580, 340), ("large", 940, 610)):
        window.action("resize", width, height)
        wait_for(stage + " dimensions", lambda: window.action("size") == f"{width} {height}")
        window.see(stage, lambda lines: fzf_frame(lines, "Amber bravo"))
    window.action("key", "enter")
    wait_for("fzf result", lambda: window.read("picked"))
    result = (window.home / "picked").read_bytes()
    (window.evidence / "fzf-result.bin").write_bytes(result)
    assert result == b"Amber bravo\n"
    window.continued("Termole resized")


def cancel(window):
    window.command("echo Termole base'line'")
    window.see("baseline", lambda lines: geometry(lines, "Termole baseline"))
    fzf(window)
    window.action("text", "Amber")
    window.see("filter", lambda lines: fzf_frame(lines, "Amber alpha")
               and geometry(lines, "Termole baseline") is None)
    window.action("key", "esc")

    def restored(lines):
        return (geometry(lines, "Termole baseline") is not None
                and all("Amber alpha" not in s and "Amber bravo" not in s
                        and "Find Amber" not in s for _, s in lines))

    window.see("restored", restored)
    window.action("text", "echo Termole recovered")
    command_row = window.see("editing", lambda lines: restored(lines)
                             and next((y for y, s in lines if "echo Termole recovered" in s), None))
    cursor = [int(output(window.driver, "cursor", window.evidence / f"{window.name}-{stage}.png",
                         str(command_row))) for stage in ("restored", "editing")]
    if cursor[1] <= cursor[0] + 50:
        raise RuntimeError(f"Expected cursor to advance with the command on the prompt row: {cursor}")
    window.action("key", "enter")
    window.see("executed", lambda lines: geometry(lines, "Termole recovered"))
    assert (window.home / "picked").read_bytes() == b""
    window.continued("Termole continued")


def isolate(first, second):
    child = fzf(first)
    assert first.wid != second.wid and first.shellpid != second.shellpid
    pids = {"closed_shell": first.shellpid, "closed_fzf": child, "surviving_shell": second.shellpid}
    (first.evidence / "processes.json").write_text(json.dumps(pids, indent=2) + "\n")
    first.action("close")
    first.app.wait(timeout=12)

    def gone(pid):
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return True
        return False

    for pid in (first.shellpid, child):
        wait_for(f"process {pid} to exit", lambda: gone(pid))
    assert second.find_window() == second.wid
    second.continued("Termole survived")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scenario", choices=("t2", "t3", "t4", "t5"))
    parser.add_argument("app", type=lambda p: str(Path(p).resolve()))
    parser.add_argument("driver", type=lambda p: str(Path(p).resolve()))
    parser.add_argument("evidence", type=Path)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    subprocess.run([args.driver, "permissions"], check=True)
    shell = output("dscl", ".", "-read", "/Users/" + output("id", "-un"), "UserShell").split(": ", 1)[1]
    meta = {"platform": output("sw_vers"), "shell": shell}
    if args.scenario != "t2":
        meta["fzf"] = output("fzf", "--version")
    (args.evidence / "platform.json").write_text(json.dumps(meta, indent=2) + "\n")
    with ExitStack() as stack:
        first = Window(stack, args, "first", shell)
        if args.scenario == "t5":
            isolate(first, Window(stack, args, "second", shell))
        else:
            {"t2": clipboard, "t3": resize, "t4": cancel}[args.scenario](first)


if __name__ == "__main__":
    main()
