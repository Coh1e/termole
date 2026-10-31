# Testing

## Inner loop

Do not run the whole suite for every edit. Build one target and run one test:

```bat
scripts\test-one.cmd termole_wrap_test t17_wrap
```

Equivalent:

```bat
cmake --build --preset windows --target termole_wrap_test
ctest --preset windows -R t17_wrap --output-on-failure
```

Run the full suite only before committing:

```bat
build.cmd
```

`build.cmd` is configure + full build + full CTest. It is the release/commit check, not the edit loop.

## Launcher tests

- End-to-end CLI contracts go through the public entry (`termole`, `-e`, `--stdio`). `--gui` is the internal GUI role, used only by tests about window policy or geometry.
- `t26_launcher` (launcher, waiting, stdio bytes, large binary/slow consumer, cancel and start-failure paths, console hiding, pixel capture) and `t26_cli_pipelines` (real CMD/PowerShell bytes, order, exit codes, early close, fzf `--filter`, NUL file redirection, multi-megabyte input) are the launcher test home; extend them instead of adding a parallel harness. The launcher's `PrintWindow` check is a window-content smoke signal; visual conclusions belong to `tests/visual/`.
- To make a program fail to start, hold a real copy open exclusively; renaming a text file to `.exe` pops the loader's "unsupported application" dialog and the test hangs until someone clicks OK (see `docs/t15-validation.md`).
- A failed pixel check writes `window-capture.bmp` into the test artifact directory.

## UI evidence

For changes involving View, renderer, window placement or selector UI:

- **Screenshots/pixels are the pass criterion.** `screen_text()`, `viewport_text()`, `MainWindowHandle`, `paint_count` and “window rect intersects monitor” are supporting signals, not proof that the user can see the UI.
- Capture the real HWND with `PrintWindow` and assert non-background text pixels; see the pixel harness work in issue [#27](https://github.com/Coh1e/termole/issues/27).
- A window that exists at `-21845,-21845` is off-screen; a View with text but blank pixels is a renderer bug. Both have happened.
- Keep failure screenshots in the test artifact directory or `docs/evidence/`.

## Pixel harness (#27)

- `scripts/capture-window.ps1` captures one HWND with `PrintWindow(PW_RENDERFULLCONTENT)` and writes a PNG plus tolerant statistics (`colors`, `background`, `inkPixels`, `inkRatio`, `strongPixels`, `bandStrongPixels`). `strongPixels` uses a stronger channel difference (96) so a filled selection block can be told apart from glyph antialiasing; the script still asserts nothing. `Add-Type` scratch defaults to a `capture-temp` directory beside the PNG (override with `-TempPath`), because sandboxed test children can be barred from the user temp directory. Use it for manual evidence too:
  ```bat
  powershell -NoProfile -File scripts\capture-window.ps1 -Hwnd 0x1234 -OutputPath out\view.png
  ```
- `tests/visual/` is the pixel test home: `visual_alt_screen` (controlled alt-screen child) and `visual_fzf` (real fzf) are both in the default suite. `visual_fzf` registers only when fzf is found, using the same probe as `t06_fzf`.
- `visual_fzf_interaction` (public Session + View) and `visual_fzf_standalone` (`termole -e`) drive the same real fzf through the View input path with `WM_KEYDOWN`/`WM_CHAR`: arrow movement, typed filtering, Enter and Escape, with a capture after every step. They register only when fzf and PowerShell are found. Do not replace this with `--filter`, `--no-color`, direct `Session::send_key`, or an alt-screen removal. Evidence: `docs/t06-validation.md`.
- Run the visual label with `ctest --preset windows-visual`; the default `windows` preset also runs the visual tests because their only label is `visual`.
- Renderer cell metrics are device pixels: the D2D target is pinned to 96 DPI so one DIP equals one pixel. If `screen_text` has content but the window is blank, check for a DIP/pixel mix-up before changing colours, fzf flags or alt-screen (#26).
- Every run writes PNG and stats under `<build>/tests/visual/<scenario>/`; the fzf case also writes the candidate and pointer band captures (`fzf-candidate.*`, `fzf-pointer.*`). Inspect the PNG on failure. `visual_fzf` joined the default suite when #26 was fixed; keep its assertions unchanged.
- Cross-process `PrintWindow` sends messages to the target window. A test that waits for the capture process must keep pumping messages (`MsgWaitForMultipleObjectsEx`), not block in `WaitForSingleObject`.
- The fzf case maps the candidate and pointer rows from `screen_text()` to pixel row bands and asserts ink inside those bands, so a footer-only render cannot pass.
- Interaction row bands use the measured `View::cell_height()`, not `height / rows`: the latter accumulates rounding error and can be half a row off near the bottom. The standalone case measures a probe View with the same default font/DPI before mapping the app's window.
- Thresholds are deliberately coarse (`strongest channel difference >= 24`, `ink >= 500 px` and `>= 0.08%`). Do not replace them with exact colours or whole-image hashes.

## Selectors

- `fzf` is an external selector: it owns candidate input, its UI, and result output.
- `termole -e <program>` is foot-style execute: the program gets the ConPTY, not the caller pipes.
- `termole --stdio <program>` is the explicit Termole extension that forwards caller stdin/stdout; Termole does not parse selector results.
- `termole` is a console launcher: normal mode returns immediately; `-e`/`--stdio` wait and return the program exit code. `--stdio` requires piped/file stdin/stdout.
- See [ADR 0006](../adr/0006-terminal-selector-boundary.md) and [ADR 0007](../adr/0007-console-launcher-wait.md).
