#pragma once

#include "common/cli.h"

#include <termole/termole.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace termole::app {

struct ConfigValues {
    std::optional<std::wstring> font_family;
    std::optional<uint32_t> font_size;
    std::optional<uint32_t> theme;
    std::optional<uint16_t> columns;
    std::optional<uint16_t> rows;
    std::optional<int32_t> window_x;
    std::optional<int32_t> window_y;
    std::optional<bool> center;
    std::optional<bool> floating;
    std::optional<bool> borderless;
    std::optional<uint8_t> opacity;
    std::optional<std::wstring> title;
    std::optional<std::wstring> icon;
    std::optional<bool> keep_window;
    std::optional<bool> close_on_exit;
    std::optional<std::wstring> program;
    std::vector<std::wstring> arguments;
    std::optional<std::wstring> working_directory;
    std::vector<std::wstring> environment;
};

struct Config {
    ConfigValues root;
    std::map<std::wstring, ConfigValues> profiles;
};

struct ConfigLoadResult {
    Config config;
    std::wstring error;
    bool ok = true;
};

// Parses the small TOML subset used by Termole: root keys and
// [profiles.<name>] tables, strings, integers, booleans and string arrays.
ConfigLoadResult load_config(const std::wstring& path, const std::optional<std::wstring>& profile);
std::wstring default_config_path();

// Fills CLI defaults from the selected profile without overriding explicit CLI
// values.
void apply_config(CliOptions& options, const Config& config,
                  const std::optional<std::wstring>& profile);

} // namespace termole::app
