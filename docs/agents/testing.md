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

## UI evidence

For changes involving View, renderer, window placement or selector UI:

- **Screenshots/pixels are the pass criterion.** `screen_text()`, `viewport_text()`, `MainWindowHandle`, `paint_count` and “window rect intersects monitor” are supporting signals, not proof that the user can see the UI.
- Capture the real HWND with `PrintWindow` and assert non-background text pixels; see the pixel harness work in issue [#27](https://github.com/Coh1e/termole/issues/27).
- A window that exists at `-21845,-21845` is off-screen; a View with text but blank pixels is a renderer bug. Both have happened.
- Keep failure screenshots in the test artifact directory or `docs/evidence/`.

## Selectors

- `fzf` and `rofi` are external selectors: they own candidate input, their UI, and result output.
- `termole -e <program>` is the generic execute seam; Termole does not parse selector results.
- See ADR 0006 once the spec rewrite lands; until then, read the existing picker ADR as **superseded**, not as current direction.
