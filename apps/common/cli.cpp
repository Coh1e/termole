#include "common/cli.h"

#include <string_view>

namespace termole::app {
namespace {

bool starts_with(std::wstring_view text, std::wstring_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool is_option(std::wstring_view token)
{
    return token.size() > 1 && token.front() == L'-';
}

bool is_environment_entry(std::wstring_view entry)
{
    const size_t separator = entry.find(L'=');
    return separator != std::wstring_view::npos && separator > 0 && entry.front() != L'=';
}

CliParseResult error(std::wstring message)
{
    CliParseResult result;
    result.action = CliAction::Error;
    result.message = std::move(message);
    return result;
}

} // namespace

CliParseResult parse_cli(int argc, wchar_t** argv)
{
    CliParseResult result;
    if (argc <= 1) return result;

    for (int index = 1; index < argc; ++index) {
        const std::wstring token = argv[index];

        if (!result.options.program && token == L"--") {
            if (index + 1 < argc) {
                result.options.program = argv[++index];
                if (result.options.program->empty()) return error(L"program name is empty");
                for (++index; index < argc; ++index) result.options.arguments.emplace_back(argv[index]);
            }
            return result;
        }

        if (!result.options.program && is_option(token)) {
            if (token == L"--help" || token == L"-h") {
                result.action = CliAction::Help;
                return result;
            }
            if (token == L"--version") {
                result.action = CliAction::Version;
                return result;
            }

            const auto take_value = [&](std::wstring_view option, std::wstring& value) -> bool {
                if (token == option) {
                    if (index + 1 >= argc) return false;
                    value = argv[++index];
                    return true;
                }
                if (starts_with(token, std::wstring(option) + L"=")) {
                    value = token.substr(option.size() + 1);
                    return true;
                }
                return false;
            };

            if (token == L"--cwd" || starts_with(token, L"--cwd=")) {
                std::wstring value;
                if (!take_value(L"--cwd", value)) return error(L"--cwd requires a directory");
                if (value.empty()) return error(L"--cwd requires a non-empty directory");
                result.options.working_directory = std::move(value);
                continue;
            }
            if (token == L"--env" || starts_with(token, L"--env=")) {
                std::wstring value;
                if (!take_value(L"--env", value)) return error(L"--env requires KEY=VALUE");
                if (!is_environment_entry(value)) {
                    return error(L"invalid --env entry (expected KEY=VALUE): " + value);
                }
                result.options.environment.push_back(std::move(value));
                continue;
            }
            return error(L"unknown option: " + token);
        }

        if (!result.options.program) {
            if (token.empty()) return error(L"program name is empty");
            result.options.program = token;
            continue;
        }
        result.options.arguments.push_back(token);
    }

    return result;
}

std::wstring help_text()
{
    return L"Termole - Windows native terminal\n"
           L"\n"
           L"Usage:\n"
           L"  termole [options] [--] [program [arguments...]]\n"
           L"\n"
           L"Options:\n"
           L"  --cwd <directory>    Set the child working directory\n"
           L"  --env <KEY=VALUE>    Add or override a child environment variable (repeatable)\n"
           L"  --help, -h           Show this help\n"
           L"  --version            Show version\n"
           L"\n"
           L"Everything after -- is the target program and its arguments. Without --,\n"
           L"the first non-option is the target program and every following token is\n"
           L"passed to it unchanged. Without a target, the default PowerShell starts.\n";
}

std::wstring version_text()
{
    return L"Termole " TERMOLE_VERSION L"\n";
}

} // namespace termole::app
