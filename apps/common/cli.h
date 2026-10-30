#pragma once

#include <optional>
#include <string>
#include <vector>

namespace termole::app {

struct CliOptions {
    std::optional<std::wstring> program;
    std::vector<std::wstring> arguments;
    std::optional<std::wstring> working_directory;
    std::vector<std::wstring> environment;
};

enum class CliAction {
    Run,
    Help,
    Version,
    Error,
};

struct CliParseResult {
    CliAction action = CliAction::Run;
    CliOptions options;
    std::wstring message;
};

CliParseResult parse_cli(int argc, wchar_t** argv);
std::wstring help_text();
std::wstring version_text();

} // namespace termole::app
