#pragma once

#include <windows.h>

namespace termole::app {

// True when the standard handle is a pipe or a file rather than a console.
// This is the --stdio precondition: console stdin/stdout would mix keyboard
// input and terminal output into the data channel.
bool standard_stream_is_redirected(DWORD standard_handle);

// True when this process is the only client of its console. That is the
// practical test for a console allocated for this launch (Explorer or
// CREATE_NEW_CONSOLE) rather than inherited from a calling shell. A console
// shared with any other process is never treated as ours.
bool console_is_exclusive();

// Hides a console allocated for this process so no blank console window is
// left behind. A console shared with the caller is never touched.
bool hide_own_console();

// Detaches from this process's console. The console closes once its last
// client (possibly a child that inherited it) detaches.
void release_console();

} // namespace termole::app
