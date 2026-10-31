#pragma once

#include <string>

namespace termole::app {

// Runs a configured pipe command through the Windows shell. The text goes to
// the command's stdin on a worker thread, so a slow or non-reading command
// never blocks the UI thread. stdout and stderr point at NUL; the command's
// exit code is deliberately not reported.
class PipeRunner {
public:
    PipeRunner();
    explicit PipeRunner(std::wstring shell);

    PipeRunner(const PipeRunner&) = delete;
    PipeRunner& operator=(const PipeRunner&) = delete;

    // Starts `<shell> /d /s /c "<command>"`. Returns false and fills `error`
    // only when the shell or its writer thread cannot start.
    bool launch(const std::wstring& command, std::string text, std::wstring& error);

private:
    std::wstring shell_;
};

} // namespace termole::app
