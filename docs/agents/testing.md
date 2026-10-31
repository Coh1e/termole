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

- `scripts/capture-window.ps1` captures one HWND with `PrintWindow(PW_RENDERFULLCONTENT)` and writes a PNG plus tolerant statistics (`colors`, `background`, `inkPixels`, `inkRatio`). It asserts nothing; callers choose thresholds. Use it for manual evidence too:
  ```bat
  powershell -NoProfile -File scripts\capture-window.ps1 -Hwnd 0x1234 -OutputPath out\view.png
  ```
- `tests/visual/` is the pixel test home: `visual_alt_screen` (controlled alt-screen child) is in the default suite. `visual_fzf` registers only when fzf is found and is labelled `known-red` until [#26](https://github.com/Coh1e/termole/issues/26) is fixed.
- Run both scenarios with `ctest --preset windows-visual`; the default `windows` preset excludes `known-red` so `build.cmd` stays green before #26.
- Every run writes PNG and stats under `<build>/tests/visual/<scenario>/`; the fzf case also writes the candidate and pointer band captures (`fzf-candidate.*`, `fzf-pointer.*`). Inspect the PNG on failure. Remove the `known-red` label after #26 and the fzf case joins the default suite unchanged.
- Cross-process `PrintWindow` sends messages to the target window. A test that waits for the capture process must keep pumping messages (`MsgWaitForMultipleObjectsEx`), not block in `WaitForSingleObject`.
- The fzf case maps the candidate and pointer rows from `screen_text()` to pixel row bands and asserts ink inside those bands, so a footer-only render cannot pass.
- Thresholds are deliberately coarse (`strongest channel difference >= 24`, `ink >= 500 px` and `>= 0.08%`). Do not replace them with exact colours or whole-image hashes.

## Selectors

- `fzf` is an external selector: it owns candidate input, its UI, and result output.
- `termole -e <program>` is foot-style execute: the program gets the ConPTY, not the caller pipes.
- `termole --stdio <program>` is the explicit Termole extension that forwards caller stdin/stdout; Termole does not parse selector results.
- `termole` is a console launcher: normal mode returns immediately; `-e`/`--stdio` wait and return the program exit code. `--stdio` requires piped/file stdin/stdout.
- See [ADR 0006](../adr/0006-terminal-selector-boundary.md) and [ADR 0007](../adr/0007-console-launcher-wait.md).
