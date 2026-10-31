#pragma once

namespace termole::app {

// Internal GUI role. The launcher starts it with --gui; it is not a documented
// user entry point.
int run_gui(int argc, wchar_t** argv);

} // namespace termole::app
