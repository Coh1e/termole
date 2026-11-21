# Windows full-screen fzf resize (#35)

Tested on Windows x64 build 26300.10075, PowerShell 7.6.9 and fzf
0.75.0 (c7bf2b8e), using the [T1 executable](standalone-t1-windows.md).
Opened Termole without arguments using an empty application-data directory.
In the default Shell, disabled PSReadLine history saving, cleared the screen
and ran the following command.

```powershell
$owner=$PID; $pick="pear-one","pear-two","plum" | & "D:/playground/mingw64/bin/fzf.exe" --layout=reverse --no-sort
```

Entered `pear` and pressed Down. The View showed two matches and highlighted
`pear-two`. Resized the standalone window twice. The status line redrew to
each new width, with the filter, count and highlighted row visible throughout.

| Stage | View size in pixels | Non-background pixels | View capture |
| --- | --- | ---: | --- |
| Selected second row | 978 × 584 | 5,994 | [Before resize](evidence/standalone-t3-selected.png) |
| Reduced window | 618 × 344 | 5,254 | [Smaller](evidence/standalone-t3-small.png) |
| Enlarged window | 1038 × 634 | 6,098 | [Larger](evidence/standalone-t3-large.png) |

Pressed Enter and issued the command below. The PID comparison succeeded
and the output file contained `pear-two 7.6.9`. The [final View capture](evidence/standalone-t3-returned.png)
shows the output followed by the PowerShell prompt.

```powershell
if($PID -eq $owner){ "$pick $($PSVersionTable.PSVersion)" | Tee-Object -FilePath result.txt }
```
