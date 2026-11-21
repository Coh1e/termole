# Local terminal sessions

Use the same Termole executable to host a session and open its View.

```sh
termole --server --pipe dev
termole --client --pipe dev --detach
```

Add `-- PROGRAM [ARGS...]` to the server command to select the child program.
The default endpoint is `default`. Names can contain 1–32 ASCII letters,
digits, hyphens or underscores. Each endpoint owns one session and accepts
one attached View at a time. Use a separate endpoint for each hosted session.

Closing an attached View normally ends its server and child. With
`--detach`, the server keeps the child and current terminal state alive.
Connect to the same endpoint to resume. When the child exits while a View
is attached, the client returns the child's exit code.

Windows uses a named pipe restricted to the current user and logon session.
Linux and macOS use a Unix socket in a directory owned by the current user
with mode `0700`. A server stopped forcibly on Unix may leave its socket
path behind. A new endpoint name allows a new session immediately.

The protocol carries input bytes, viewport size, terminal modes, visible
cells and exit status. It uses a versioned, length-prefixed byte format.
The client renders the visible grid from the server's session state. Drag
to select visible cells, use Ctrl+Shift+C to copy and Ctrl+Shift+V to paste.
The macOS View also supports Command+C and Command+V.
