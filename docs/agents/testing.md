# Testing

## Inner loop

With the Windows build toolchain on PATH, build one target and run one test:

```sh
cmake --build --preset windows-mingw --target termole_wrap_test
ctest --preset windows-mingw -R t17_wrap --output-on-failure
```

Run the full suite before committing:

```sh
cmake --workflow --preset windows-mingw
```

The workflow configures, builds and runs the full Debug suite.

To enable the pre-commit whitespace check, run `git config core.hooksPath .githooks` from the checkout.

## Launcher tests

- End-to-end CLI contracts go through the public entry (`termole`, `-e`, `--stdio`). `--gui` is the internal GUI role, used only by tests about window policy or geometry.
- Launcher tests are grouped under `t26_*`. `t26_launcher` checks waiting, stdio bytes, slow consumers, cancellation, start failures and console behavior. `t26_cli_pipelines` checks real CMD and PowerShell pipelines, byte order, exit codes, early close, fzf `--filter` and NUL file redirection. `t26_cli_parallel` checks isolation of arguments, working directories and environment across simultaneous `-e` calls. The launcher's `PrintWindow` check is a window-content smoke signal. Visual conclusions belong to `tests/visual/`.
- To make a program fail to start, hold a real copy open exclusively; renaming a text file to `.exe` pops the loader's "unsupported application" dialog and the test hangs until someone clicks OK.
- A failed pixel check writes `window-capture.bmp` into the test artifact directory.

## SDK host tests

`t22_sdk_consumer` is the external-embedding home. It runs `cmake --install` into
`<build>/tests/sdk-consumer/package`, checks relative links and Markdown heading
anchors across the installed documents with Windows PowerShell, builds `tests/sdk-consumer` and the shipped
`share/termole/examples/sdk-host` with `find_package(Termole CONFIG REQUIRED)`
against that prefix, then runs the consumer linked against the installed static SDK.
The consumer drives a real CMD/ConPTY and View, checks the public ABI/ownership/error
rules and captures the View and host with `PrintWindow`. Pixel stats and PNGs are
written under `<build>/tests/sdk-consumer/artifacts/`. Keep this test out-of-tree: linking an in-tree
target would stop proving that the development package is sufficient.

## UI evidence

Run `visual_preview_redraw` when changing how the Windows View paints a partial
alternate-screen redraw. The test pauses output for 240 ms between writing a new
heading and erasing the old tail. The View keeps the painted frame across that
pause. With the cursor hidden, it checks redraw progress at 500 ms intervals
and resumes painting when it sees no further replacement of old glyphs. A
stopped writer and a continuously updating counter exercise both cases.

Once a hidden-cursor frame is on screen, the test queues another footer and
reads the viewport, drags a mouse selection and captures `WM_PRINTCLIENT`
before dispatching `WM_PAINT`. All three must still show the painted footer.

At the current DPI and at 192 DPI the test compares the complete short sentence
with a same-DPI reference and checks its final `n.` against independently drawn
glyphs. Both counter frames must show the complete sentence. The PNGs and pixel
measurements are in `<build>/tests/visual/preview-redraw/`.

Build the standalone Windows desktop collector and run the offline replay with
the following commands. [Recorded frames from #54](https://github.com/Coh1e/termole/issues/54#issuecomment-7212010345)

```sh
cmake --build --preset windows-mingw --target termole_release_dxgi_capture
ctest --preset windows-mingw -R release_preview_residue_control --output-on-failure
```

The replay uses saved pixels and DXGI acquisition records. The first short
preview must immediately follow a post-input `LICENSE` presentation. Mouse-only
updates count for pointer visibility but do not present a new frame. An
overlapping pointer, unknown pointer state or missed presentation makes the run
inconclusive. If a readable frame shows the short heading with text left over
from the previous preview, the run fails even if a later frame is obscured.
The historical mixed frame contains 1,379 pixels from the `LICENSE` tail.

Before changing the default View, run the same IME, selection, copy/paste, focus and window interaction scenarios on both implementations. Verify visible behavior with screenshots or pixel checks.

For changes involving View, renderer, window placement or selector UI:

- **Screenshots/pixels are the pass criterion.** `screen_text()`, `viewport_text()`, `MainWindowHandle`, `paint_count` and “window rect intersects monitor” are supporting signals, not proof that the user can see the UI.
- Capture the real HWND with `PrintWindow` and assert non-background text pixels; see the pixel harness work in issue [#27](https://github.com/Coh1e/termole/issues/27).
- A window that exists at `-21845,-21845` is off-screen; a View with text but blank pixels is a renderer bug. Both have happened.
- Keep failure screenshots in the test artifact directory.

## Pixel harness (#27)

- `scripts/capture-window.ps1` captures one HWND with `PrintWindow(PW_RENDERFULLCONTENT)` and writes a PNG plus tolerant statistics (`colors`, `background`, `inkPixels`, `inkRatio`, `strongPixels`, `bandStrongPixels`). `strongPixels` uses a stronger channel difference (96) so a filled selection block can be told apart from glyph antialiasing; the script still asserts nothing. `Add-Type` scratch defaults to a `capture-temp` directory beside the PNG (override with `-TempPath`), because sandboxed test children can be barred from the user temp directory. Use it for manual evidence too:
  ```bat
  powershell -NoProfile -File scripts\capture-window.ps1 -Hwnd 0x1234 -OutputPath out\view.png
  ```
- `tests/visual/` is the pixel test home: `visual_alt_screen` (controlled alt-screen child) and `visual_fzf` (real fzf) are both in the default suite. `visual_fzf` registers only when fzf is found, using the same probe as `t06_fzf`.
- `visual_fzf_interaction` (public Session + View) and `visual_fzf_standalone` (`termole -e`) drive the same real fzf through the View input path with `WM_KEYDOWN`/`WM_CHAR`: arrow movement, typed filtering, Enter and Escape, with a capture after every step. They register only when fzf and PowerShell are found. Do not replace this with `--filter`, `--no-color`, direct `Session::send_key`, or an alt-screen removal.
- `visual_input_interaction` (public Session + View) and `visual_input_standalone` (`termole -e`) are the T12 mouse/paste acceptance home. A controlled child (`tests/visual/t12_input_child.cpp`) enables `?1000/1002/1003/1006/2004` and `?1`, reads the real ConPTY input bytes, and writes both an exact-byte log and a fixed-row status screen. The tests post `WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/`WM_LBUTTONUP`/`WM_MOUSEWHEEL` to the View, use real `WM_CHAR`/key messages for Ctrl+C/V, Shift+Insert and application cursor keys, and assert fixed-row pixel bands plus the child's bytes. They register whenever PowerShell is found; the standalone case also needs `termole_app`.
- Run the visual label with `ctest --preset windows-mingw-visual`; the default `windows-mingw` preset also runs the visual tests because their only label is `visual`. The T12 input cases use posted View messages rather than hardware `SendInput`: they cover the View message path, the ConPTY encoding and the target program, not the physical mouse/focus path.
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
