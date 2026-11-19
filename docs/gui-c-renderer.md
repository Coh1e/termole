# C GUI renderer boundary (experimental)

Status: implemented and tested, not yet the product default.

The C core (`src/vt/termole_vt.c`) already renders terminal state into a
`termole_frame`. `src/gui/gdi_render.c` is a C-only Win32 GDI renderer for that
frame. It proves the GUI layer can be driven without C++ types and provides the
first piece of the planned C View.

## API

`src/gui/gdi_render.h`:

```c
termole_gdi_renderer* termole_gdi_create(const wchar_t* font_family,
                                         int font_size, int theme);
void termole_gdi_destroy(termole_gdi_renderer*);
float termole_gdi_cell_width(const termole_gdi_renderer*);
float termole_gdi_cell_height(const termole_gdi_renderer*);
void termole_gdi_paint(termole_gdi_renderer*, HDC, const termole_frame*, int focused);
```

Supported today: default/truecolor foreground and background, bold, italic,
underline, inverse, invisible, wide cells, cursor rectangle, dark/light theme,
DBCS/UTF-16 text.

Not yet: selection, hyperlinks, IME composition, font fallback, ligatures,
smooth scrolling, fractional DPI.

## Verification

`tests/gdi_render_test.c` creates a real `termole_vt`, feeds text, creates the C
renderer, paints into a memory DC and asserts non-background pixels.

```
PASS: feed text
PASS: frame
PASS: create GDI renderer
PASS: cell width
PASS: cell height
PASS: renderer paints non-background pixels
PASS: C GDI renderer
```

CTest label: `gdi_render_contract`.

## Migration path

1. Keep the C renderer as the GUI boundary.
2. Add a C View/window/input layer that owns this renderer.
3. Switch `termole_app` to the C View after pixel parity tests.
4. Only then remove `src/view/renderer.cpp` and reduce the C++ shell.

## Session snapshot ABI

A C-only View no longer needs C++ to obtain cells. The public C ABI exposes:

```c
termole_status termole_session_snapshot(termole_session* session, int32_t scroll_offset,
                                        termole_cell* out_cells, size_t capacity,
                                        size_t* out_required);
```

Call with `out_cells = NULL` to query `out_required`; the function returns
`TERMOLE_STATUS_OK`. A too-small buffer returns `INVALID_ARGUMENT` and still
writes the required count. `c_abi_characterization_test` exercises the probe and
the filled snapshot from pure C.
## C View

`include/termole/cview.h` / `src/gui/cview.c` is a C-only child-window View:

```c
termole_status termole_cview_create(HWND parent, termole_session* session,
                                    const termole_cview_options* options,
                                    termole_cview** out_view);
void termole_cview_destroy(termole_cview* view);
HWND termole_cview_hwnd(const termole_cview* view);
uint64_t termole_cview_paint_count(const termole_cview* view);
void termole_cview_set_selection(termole_cview*, int32_t start_row, int32_t start_column,
                                 int32_t end_row, int32_t end_column);
void termole_cview_clear_selection(termole_cview*);
termole_status termole_cview_get_selection_text(termole_cview*, char**, size_t*);
```

It owns a GDI renderer, polls the Session every 33 ms, calls
`termole_session_snapshot`, sizes the grid from the window and font metrics,
forwards keys through `termole_session_send_key` and characters through
`termole_session_write`, supports mouse-wheel scrollback via
`termole_session_get_scrollback_line_count`, draws the cursor from
`termole_session_get_cursor`, supports mouse-drag selection (highlighted cells via
`termole_cell.selected`) and Ctrl+C copy through
`termole_session_get_selection_text`, reports mouse input when the session enables tracking
(`termole_session_get_modes`; SGR 1006 and legacy X10), pastes the clipboard
with Ctrl+V / Shift+Insert (honouring bracketed paste), commits IME result
strings (`WM_IME_COMPOSITION` + `ImmGetCompositionStringW`), debounces grid
resizes by 80 ms, and repaints on `WM_PAINT`. The C view test also reads window
pixels and asserts visible text ink.

`tests/cview_test.cpp` creates a real Session, embeds the C View in a parent
window, pumps messages and asserts `paint_count > 0`:

```
PASS: parent window
PASS: session
PASS: C view created
PASS: C view painted
PASS: C view has a window
PASS: C view
```

Still missing for product parity: selection, clipboard, IME, mouse reporting,
scrollback wheel, resize debounce and font/theme configuration. The C++ View
remains the default until those pixel/behavior tests pass.
## C-only GUI prototype executable

`apps/cgui/main.c` builds the test-only target `termole_cgui_smoke`:

```
termole_session (C ABI)
  → termole_cview (C window + input)
  → termole_gdi_paint (C renderer)
```

It creates a top-level Win32 window, a default-shell Session, and embeds the C
View. It is not shipped and not the default `termole_app` path; it exists to
prove the full C GUI chain compiles and links without the C++ View. Automated
launch/close coverage is still pending.
## Product role: `termole --c-gui`

`termole_app` now links `apps/cgui/main.c` and exposes an explicit C GUI role:

```
termole.exe --c-gui
termole.exe --c-gui --smoke   # test-only: closes after 1.2 s
```

The normal `--gui` role still uses the C++ View; `--c-gui` bypasses it entirely.
CTest covers both:

```
cgui_smoke           termole_cgui_smoke --smoke
cgui_product_smoke   termole_app --c-gui --smoke
```

Both pass, so the shipped executable can run the full C chain on demand. The
default is unchanged until screenshot/pixel parity is demonstrated.
`--c-gui` accepts a small option subset without the C++ config path:

```
--title=TEXT   --width=PX   --height=PX
--font=NAME    --font-size=N   --theme=light
--program=PATH -- ARGS...
--columns=N   --rows=N
```

Example:

```
termole.exe --c-gui --title=C-GUI-Probe --width=900 --height=600 \
            --font-size=24 --theme=light
```

CTest `cgui_product_options` runs that form with `--smoke`.

To choose a child program, put the program in `--program=` and arguments after
`--`:

```
termole.exe --c-gui --program=cmd.exe -- /c echo C-GUI-PROBE
```

CTest `cgui_product_program` covers this form.
## Product screenshot test

`tests/cgui_visual_test.cpp` starts the shipped `termole.exe --c-gui`, waits for
the `C-GUI-Visual` window, captures it with `PrintWindow(PW_RENDERFULLCONTENT)`,
counts non-background pixels and closes it with `WM_CLOSE`.

```
PASS: C GUI process started
PASS: C GUI window found
PASS: C GUI window contains rendered text pixels
PASS: C GUI process exits after WM_CLOSE
PASS: C GUI exit code
PASS: C GUI visual
```

CTest label: `visual`.
## TOML configuration for `--c-gui`

The C++ entry point reads the normal Termole config and forwards window/font
values to the C GUI through process environment variables:

```
[window]
width = 700
height = 420
title = "C-GUI-Visual"
font = "Consolas"
font_size = 20
theme = "light"
```

```
termole.exe --c-gui --config=path\to\config.toml
```

`--no-config` skips it. CLI `--width/--height/--title/--font/...` still win over
the environment defaults. CTest `cgui_config_visual` starts the product exe with
that TOML, captures the window and asserts the configured 700x420 size plus text
pixels.