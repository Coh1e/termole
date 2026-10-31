#pragma once

#include "common/cli.h"

#include <termole/termole.h>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace termole::app {

enum class PipeAction : uint8_t { Visible, Selected, Scrollback };

// One [pipe.<action>] table. Both fields are optional at parse time so the
// loader can report which one is missing; a usable binding needs both.
struct PipeConfig {
    std::optional<std::wstring> key;
    std::optional<std::wstring> command;
    int key_line = 0;
    int command_line = 0;
    int table_line = 0;
};

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
    // Indexed by PipeAction. Pipe bindings are global: profiles do not
    // override them.
    std::array<std::optional<PipeConfig>, 3> pipes;
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

// Normalized key chord: a virtual-key code plus MOD_* modifier flags.
struct KeyChord {
    uint32_t virtual_key = 0;
    uint32_t modifiers = 0;
};

struct PipeBinding {
    PipeAction action = PipeAction::Visible;
    KeyChord chord;
    std::wstring command;
};

// Display name used by diagnostics: "pipe.visible", "pipe.selected" or
// "pipe.scrollback".
std::wstring pipe_action_name(PipeAction action);

// Parses the chord syntax documented in docs/config.md. On failure, detail
// explains the problem without repeating the original text.
std::optional<KeyChord> parse_key_chord(std::wstring_view text, std::wstring& detail);
std::wstring format_key_chord(const KeyChord& chord);

// Validated pipe bindings from the root configuration. Profiles do not
// override them.
std::vector<PipeBinding> pipe_bindings(const Config& config);

// Parses the small TOML subset used by Termole: root keys and
// [profiles.<name>] tables, strings, integers, booleans and string arrays.
ConfigLoadResult load_config(const std::wstring& path, const std::optional<std::wstring>& profile);
std::wstring default_config_path();

// Fills CLI defaults from the selected profile without overriding explicit CLI
// values.
void apply_config(CliOptions& options, const Config& config,
                  const std::optional<std::wstring>& profile);

} // namespace termole::app
