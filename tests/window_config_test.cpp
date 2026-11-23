#include "common/config.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    if (condition) {
        std::cout << "PASS: " << what << "\n";
    } else {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

std::filesystem::path write_config(const char* name, const std::string& body)
{
    const auto path = std::filesystem::path(name);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << body;
    return path;
}

} // namespace

int main()
{
    const auto path = write_config(
        "window-config-test.toml",
        "[window]\n"
        "width = 1200\n"
        "height = 800\n"
        "columns = 100\n"
        "rows = 30\n"
        "title = \"Window Probe\"\n"
        "center = true\n"
        "decorations = false\n"
        "opacity = 70\n");
    const auto loaded = termole::app::load_config(path.wstring(), std::nullopt);
    check(loaded.ok, "window table parses");
    if (!loaded.ok) {
        std::wcerr << loaded.error << L"\n";
        return 1;
    }

    termole::app::CliOptions options;
    termole::app::apply_config(options, loaded.config, std::nullopt);
    check(options.window_width && *options.window_width == 1200, "width applies");
    check(options.window_height && *options.window_height == 800, "height applies");
    check(options.columns == 100 && options.rows == 30, "grid applies");
    check(options.window_title && *options.window_title == L"Window Probe", "title applies");
    check(options.center, "center applies");
    check(options.borderless, "decorations=false maps to borderless");
    check(options.opacity_percent && *options.opacity_percent == 70, "opacity applies");

    termole::app::CliOptions explicit_options;
    explicit_options.window_width = 900;
    explicit_options.columns = 50;
    termole::app::apply_config(explicit_options, loaded.config, std::nullopt);
    check(explicit_options.window_width && *explicit_options.window_width == 900,
          "explicit CLI width wins");
    check(explicit_options.columns == 50, "explicit CLI columns win");

    const auto bad = write_config("window-config-bad.toml",
                                  "[window]\nwidth = 10\n");
    const auto rejected = termole::app::load_config(bad.wstring(), std::nullopt);
    check(!rejected.ok, "out-of-range width is rejected");

    const auto plugins = write_config("plugin-config-test.toml",
                                      "plugins = [\"markdown-quote.js\"]\n");
    const auto enabled = termole::app::load_config(plugins.wstring(), std::nullopt);
    check(enabled.ok && enabled.config.plugins == std::vector<std::wstring>{L"markdown-quote.js"},
          "global configuration opts in to the example plugin");
    check(loaded.config.plugins.empty(), "plugins are disabled when the key is absent");
    write_config("plugin-config-test.toml", "[profiles.demo]\nplugins = [\"x.js\"]\n");
    check(!termole::app::load_config(plugins.wstring(), std::nullopt).ok,
          "a profile cannot enable local plugins");
    write_config("plugin-config-test.toml", "plugins = [\"x.exe\"]\n");
    check(!termole::app::load_config(plugins.wstring(), std::nullopt).ok,
          "plugin configuration requires .js paths");
    write_config("plugin-config-test.toml", "plugins = ['//server/share/x.js']\n");
    check(!termole::app::load_config(plugins.wstring(), std::nullopt).ok,
          "plugin configuration rejects network share paths");

    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(bad, error);
    std::filesystem::remove(plugins, error);

    if (failures != 0) {
        std::cerr << failures << " window config check(s) failed\n";
        return 1;
    }
    std::cout << "PASS: window definitions\n";
    return 0;
}
