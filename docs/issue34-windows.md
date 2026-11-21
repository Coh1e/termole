# T2 Windows clipboard acceptance (#34)

Windows 10.0.26300.0, PowerShell 7.6.9. Launching the no-argument public entry with an empty Termole configuration opened the default shell. The shell printed `Termole 中文 test` and waited at a PowerShell `Read-Host` prompt. The View visibly highlighted all 17 display columns of the line after a mouse drag. [Windows selection capture](evidence/issue34-windows-selection.png).

The copy/paste round trip remains unverified. The Windows clipboard returned no text after the copy, and the editable prompt received an empty string during the diagnostic run. An independent Win32 probe opened and emptied the clipboard from its own window. Both publishing a populated movable UTF-16 buffer with `SetClipboardData(CF_UNICODETEXT)` and reading it with `GetClipboardData` returned NULL with Win32 error 5 (access denied).

## Reproduce in a Windows desktop session

Use a disposable desktop with clipboard access. The check writes to the system clipboard and drives the View by posting mouse events and the Ctrl+C and Ctrl+V character messages. From the repository root, run:

```powershell
powershell -NoProfile -File tests/visual/standalone-clipboard.ps1 -Termole .local/build/mingw-debug/bin/termole.exe -Artifacts .local/issue34
```

The command prints the unique artifact directory. It verifies a new clipboard sequence and exact, case-sensitive clipboard text before pasting, then compares the foreground program's entire received string to the expected value. A successful run keeps the before, selection, and paste screenshots and the recipient's UTF-8 text file.
