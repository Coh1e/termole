#include "common/cli.h"

#include <termole/termole.h>

#include <windows.h>

#include <cstdlib>
#include <cwchar>
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
            if (token == L"--keep-window") {
                result.options.exit_policy = ExitPolicy::KeepWindow;
                continue;
            }
            if (token == L"--close-on-exit") {
                result.options.exit_policy = ExitPolicy::CloseOnExit;
                continue;
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

            if (token == L"--font" || starts_with(token, L"--font=")) {
                std::wstring value;
                if (!take_value(L"--font", value)) return error(L"--font requires a family name");
                if (value.empty()) return error(L"--font requires a non-empty family name");
                result.options.font_family = std::move(value);
                continue;
            }
            if (token == L"--font-size" || starts_with(token, L"--font-size=")) {
                std::wstring value;
                if (!take_value(L"--font-size", value)) return error(L"--font-size requires a number");
                wchar_t* end = nullptr;
                const unsigned long parsed = std::wcstoul(value.c_str(), &end, 10);
                if (!end || *end != L'\0' || parsed < 8 || parsed > 96) {
                    return error(L"--font-size must be an integer from 8 to 96");
                }
                result.options.font_size_dip = static_cast<uint32_t>(parsed);
                continue;
            }
            if (token == L"--theme" || starts_with(token, L"--theme=")) {
                std::wstring value;
                if (!take_value(L"--theme", value)) return error(L"--theme requires a name");
                if (value == L"system") result.options.theme = TERMOLE_THEME_SYSTEM;
                else if (value == L"dark") result.options.theme = TERMOLE_THEME_DARK;
                else if (value == L"light") result.options.theme = TERMOLE_THEME_LIGHT;
                else return error(L"--theme must be system, dark or light");
                continue;
            }
            if (token == L"--center") {
                result.options.center = true;
                continue;
            }
            if (token == L"--floating") {
                result.options.floating = true;
                continue;
            }
            if (token == L"--borderless") {
                result.options.borderless = true;
                continue;
            }
            if (token == L"--title" || starts_with(token, L"--title=")) {
                std::wstring value;
                if (!take_value(L"--title", value)) return error(L"--title requires text");
                result.options.window_title = std::move(value);
                continue;
            }
            if (token == L"--icon" || starts_with(token, L"--icon=")) {
                std::wstring value;
                if (!take_value(L"--icon", value)) return error(L"--icon requires a path");
                if (value.empty()) return error(L"--icon requires a non-empty path");
                if (::GetFileAttributesW(value.c_str()) == INVALID_FILE_ATTRIBUTES)
                    return error(L"--icon file was not found: " + value);
                result.options.window_icon = std::move(value);
                continue;
            }
            if (token == L"--columns" || starts_with(token, L"--columns=") ||
                token == L"--rows" || starts_with(token, L"--rows=")) {
                const bool columns = starts_with(token, L"--columns");
                std::wstring value;
                if (!take_value(columns ? L"--columns" : L"--rows", value))
                    return error(columns ? L"--columns requires a number" : L"--rows requires a number");
                wchar_t* end = nullptr;
                const unsigned long parsed = std::wcstoul(value.c_str(), &end, 10);
                if (!end || *end != L'\0' || parsed < 1 || parsed > 1000)
                    return error(L"grid dimensions must be 1..1000");
                if (columns) result.options.columns = static_cast<uint16_t>(parsed);
                else result.options.rows = static_cast<uint16_t>(parsed);
                continue;
            }
            if (token == L"--x" || starts_with(token, L"--x=") || token == L"--y" ||
                starts_with(token, L"--y=")) {
                const bool x = starts_with(token, L"--x");
                std::wstring value;
                if (!take_value(x ? L"--x" : L"--y", value))
                    return error(x ? L"--x requires a number" : L"--y requires a number");
                wchar_t* end = nullptr;
                const long parsed = std::wcstol(value.c_str(), &end, 10);
                if (!end || *end != L'\0') return error(L"window position must be an integer");
                if (x) result.options.window_x = parsed;
                else result.options.window_y = parsed;
                continue;
            }
            if (token == L"--opacity" || starts_with(token, L"--opacity=")) {
                std::wstring value;
                if (!take_value(L"--opacity", value)) return error(L"--opacity requires 1..100");
                wchar_t* end = nullptr;
                const unsigned long parsed = std::wcstoul(value.c_str(), &end, 10);
                if (!end || *end != L'\0' || parsed < 10 || parsed > 100)
                    return error(L"--opacity must be an integer percentage from 10 to 100");
                result.options.opacity_percent = static_cast<uint8_t>(parsed);
                continue;
            }

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

    if (result.options.center && (result.options.window_x || result.options.window_y)) {
        return error(L"--center cannot be combined with --x/--y");
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
           L"  --font <FAMILY>      Font family (default follows the system)\n"
           L"  --font-size <DIP>    Font size in DIPs, 8..96 (default 16)\n"
           L"  --theme <NAME>       system, dark or light\n"
           L"  --columns <N>        Grid columns (applet window)\n"
           L"  --rows <N>           Grid rows (applet window)\n"
           L"  --x <PX> --y <PX>    Window position (applet window)\n"
           L"  --center             Center the applet window\n"
           L"  --floating           Keep the applet above other windows\n"
           L"  --borderless         Remove the applet caption/border\n"
           L"  --opacity <1..100>   Applet opacity percentage\n"
           L"  --title <TEXT>       Applet title\n"
           L"  --icon <PATH>        Applet icon file\n"
           L"  --keep-window        Keep the window open when the child exits\n"
           L"  --close-on-exit      Close the window even when the child fails\n"
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
