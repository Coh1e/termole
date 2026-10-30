#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace termole::app {

enum class ExitPolicy {
    Auto,
    KeepWindow,
    CloseOnExit,
};

struct CliOptions {
    std::optional<std::wstring> program;
    std::vector<std::wstring> arguments;
    std::optional<std::wstring> working_directory;
    std::vector<std::wstring> environment;
    std::optional<std::wstring> font_family;
    uint32_t font_size_dip = 0;
    uint32_t theme = 0;
    ExitPolicy exit_policy = ExitPolicy::Auto;
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
