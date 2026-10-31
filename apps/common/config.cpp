#include "common/config.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <type_traits>

namespace termole::app {
namespace {

struct Value {
    enum class Kind { String, Integer, Boolean, StringArray } kind = Kind::String;
    std::wstring string;
    long long integer = 0;
    bool boolean = false;
    std::vector<std::wstring> array;
};

std::wstring trim(std::wstring_view text)
{
    size_t first = 0;
    while (first < text.size() && std::iswspace(text[first])) ++first;
    size_t last = text.size();
    while (last > first && std::iswspace(text[last - 1])) --last;
    return std::wstring(text.substr(first, last - first));
}

std::wstring strip_comment(std::wstring_view line)
{
    wchar_t quote = 0;
    bool escaped = false;
    for (size_t index = 0; index < line.size(); ++index) {
        const wchar_t character = line[index];
        if (quote == L'"' && escaped) {
            escaped = false;
            continue;
        }
        if (quote == L'"' && character == L'\\') {
            escaped = true;
            continue;
        }
        if (quote != 0) {
            if (character == quote) quote = 0;
            continue;
        }
        if (character == L'"' || character == L'\'') {
            quote = character;
        } else if (character == L'#') {
            return std::wstring(line.substr(0, index));
        }
    }
    return std::wstring(line);
}

bool parse_string(std::wstring_view text, size_t& index, std::wstring& result, std::wstring& error)
{
    if (index >= text.size() || (text[index] != L'"' && text[index] != L'\'')) {
        error = L"expected a quoted string";
        return false;
    }
    const wchar_t quote = text[index++];
    result.clear();
    while (index < text.size()) {
        const wchar_t character = text[index++];
        if (character == quote) return true;
        if (quote == L'"' && character == L'\\') {
            if (index >= text.size()) {
                error = L"unfinished string escape";
                return false;
            }
            const wchar_t escaped = text[index++];
            switch (escaped) {
            case L'n': result.push_back(L'\n'); break;
            case L'r': result.push_back(L'\r'); break;
            case L't': result.push_back(L'\t'); break;
            case L'\\': result.push_back(L'\\'); break;
            case L'"': result.push_back(L'"'); break;
            case L'u': {
                if (index + 4 > text.size()) {
                    error = L"unfinished \\u escape";
                    return false;
                }
                wchar_t* end = nullptr;
                const std::wstring digits(text.substr(index, 4));
                const unsigned long value = std::wcstoul(digits.c_str(), &end, 16);
                if (!end || *end != L'\0' || value > 0xFFFF) {
                    error = L"invalid \\u escape";
                    return false;
                }
                result.push_back(static_cast<wchar_t>(value));
                index += 4;
                break;
            }
            default:
                error = L"unsupported string escape";
                return false;
            }
            continue;
        }
        result.push_back(character);
    }
    error = L"unterminated string";
    return false;
}

bool parse_array(std::wstring_view text, size_t& index, Value& value, std::wstring& error)
{
    ++index; // [
    value.kind = Value::Kind::StringArray;
    for (;;) {
        while (index < text.size() && std::iswspace(text[index])) ++index;
        if (index >= text.size()) {
            error = L"unterminated array";
            return false;
        }
        if (text[index] == L']') {
            ++index;
            return true;
        }
        std::wstring item;
        if (!parse_string(text, index, item, error)) return false;
        value.array.push_back(std::move(item));
        while (index < text.size() && std::iswspace(text[index])) ++index;
        if (index >= text.size()) {
            error = L"unterminated array";
            return false;
        }
        if (text[index] == L',') {
            ++index;
            continue;
        }
        if (text[index] == L']') {
            ++index;
            return true;
        }
        error = L"expected ',' or ']' in array";
        return false;
    }
}

bool parse_value(std::wstring_view text, Value& value, std::wstring& error)
{
    const std::wstring trimmed = trim(text);
    if (trimmed.empty()) {
        error = L"missing value";
        return false;
    }
    if (trimmed[0] == L'"' || trimmed[0] == L'\'') {
        size_t index = 0;
        std::wstring parsed;
        if (!parse_string(trimmed, index, parsed, error)) return false;
        if (index != trimmed.size()) {
            error = L"unexpected text after string";
            return false;
        }
        value.kind = Value::Kind::String;
        value.string = std::move(parsed);
        return true;
    }
    if (trimmed[0] == L'[') {
        size_t index = 0;
        if (!parse_array(trimmed, index, value, error)) return false;
        if (index != trimmed.size()) {
            error = L"unexpected text after array";
            return false;
        }
        return true;
    }
    if (trimmed == L"true" || trimmed == L"false") {
        value.kind = Value::Kind::Boolean;
        value.boolean = trimmed == L"true";
        return true;
    }
    wchar_t* end = nullptr;
    const long long number = std::wcstoll(trimmed.c_str(), &end, 10);
    if (!end || *end != L'\0') {
        error = L"expected a string, integer, boolean or string array";
        return false;
    }
    value.kind = Value::Kind::Integer;
    value.integer = number;
    return true;
}

bool assign(ConfigValues& values, const std::wstring& key, const Value& value, std::wstring& error)
{
    const auto string_value = [&](std::optional<std::wstring>& target) {
        if (value.kind != Value::Kind::String) return false;
        target = value.string;
        return true;
    };
    const auto bool_value = [&](std::optional<bool>& target) {
        if (value.kind != Value::Kind::Boolean) return false;
        target = value.boolean;
        return true;
    };
    const auto integer_value = [&](auto& target, long long low, long long high) {
        if (value.kind != Value::Kind::Integer || value.integer < low || value.integer > high)
            return false;
        using Target = typename std::remove_reference_t<decltype(target)>::value_type;
        target = static_cast<Target>(value.integer);
        return true;
    };

    if (key == L"font") {
        if (!string_value(values.font_family)) error = L"font must be a string";
        else return true;
    } else if (key == L"font_size") {
        if (!integer_value(values.font_size, 8, 96)) error = L"font_size must be 8..96";
        else return true;
    } else if (key == L"theme") {
        if (value.kind != Value::Kind::String) {
            error = L"theme must be \"system\", \"dark\" or \"light\"";
        } else if (value.string == L"system") {
            values.theme = TERMOLE_THEME_SYSTEM;
            return true;
        } else if (value.string == L"dark") {
            values.theme = TERMOLE_THEME_DARK;
            return true;
        } else if (value.string == L"light") {
            values.theme = TERMOLE_THEME_LIGHT;
            return true;
        } else {
            error = L"theme must be \"system\", \"dark\" or \"light\"";
        }
    } else if (key == L"columns") {
        if (!integer_value(values.columns, 1, 1000)) error = L"columns must be 1..1000";
        else return true;
    } else if (key == L"rows") {
        if (!integer_value(values.rows, 1, 1000)) error = L"rows must be 1..1000";
        else return true;
    } else if (key == L"x") {
        if (!integer_value(values.window_x, -100000, 100000)) error = L"x must be an integer";
        else return true;
    } else if (key == L"y") {
        if (!integer_value(values.window_y, -100000, 100000)) error = L"y must be an integer";
        else return true;
    } else if (key == L"center") {
        if (!bool_value(values.center)) error = L"center must be a boolean";
        else return true;
    } else if (key == L"floating") {
        if (!bool_value(values.floating)) error = L"floating must be a boolean";
        else return true;
    } else if (key == L"borderless") {
        if (!bool_value(values.borderless)) error = L"borderless must be a boolean";
        else return true;
    } else if (key == L"opacity") {
        if (!integer_value(values.opacity, 10, 100)) error = L"opacity must be 10..100";
        else return true;
    } else if (key == L"title") {
        if (!string_value(values.title)) error = L"title must be a string";
        else return true;
    } else if (key == L"icon") {
        if (!string_value(values.icon)) error = L"icon must be a string";
        else return true;
    } else if (key == L"keep_window") {
        if (!bool_value(values.keep_window)) error = L"keep_window must be a boolean";
        else return true;
    } else if (key == L"close_on_exit") {
        if (!bool_value(values.close_on_exit)) error = L"close_on_exit must be a boolean";
        else return true;
    } else if (key == L"program") {
        if (!string_value(values.program)) error = L"program must be a string";
        else return true;
    } else if (key == L"cwd") {
        if (!string_value(values.working_directory)) error = L"cwd must be a string";
        else return true;
    } else if (key == L"args") {
        if (value.kind != Value::Kind::StringArray) {
            error = L"args must be an array of strings";
        } else {
            values.arguments = value.array;
            return true;
        }
    } else if (key == L"env") {
        if (value.kind != Value::Kind::StringArray) {
            error = L"env must be an array of strings";
        } else {
            values.environment = value.array;
            return true;
        }
    } else {
        error = L"unknown key: " + key;
    }
    return false;
}

std::optional<std::wstring> read_utf8(const std::wstring& path, std::wstring& error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = L"could not open the configuration file";
        return std::nullopt;
    }
    std::string bytes{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
    if (bytes.empty()) return std::wstring{};
    const int length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                                             static_cast<int>(bytes.size()), nullptr, 0);
    if (length <= 0) {
        error = L"configuration is not valid UTF-8";
        return std::nullopt;
    }
    std::wstring text(static_cast<size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                          static_cast<int>(bytes.size()), text.data(), length);
    if (!text.empty() && text.front() == 0xFEFF) text.erase(text.begin());
    return text;
}

struct PipeActionInfo {
    std::wstring_view suffix;
    std::wstring_view table;
};

constexpr PipeActionInfo kPipeActions[]{
    { L"visible", L"pipe.visible" },
    { L"selected", L"pipe.selected" },
    { L"scrollback", L"pipe.scrollback" },
};

std::optional<PipeAction> pipe_action_from_name(std::wstring_view name)
{
    for (size_t index = 0; index < std::size(kPipeActions); ++index) {
        if (name == kPipeActions[index].suffix) return static_cast<PipeAction>(index);
    }
    return std::nullopt;
}

struct NamedKey {
    std::wstring_view name;
    uint32_t virtual_key;
};

constexpr NamedKey kNamedKeys[]{
    { L"insert", VK_INSERT },   { L"delete", VK_DELETE }, { L"home", VK_HOME },
    { L"end", VK_END },         { L"pageup", VK_PRIOR },  { L"pagedown", VK_NEXT },
    { L"up", VK_UP },           { L"down", VK_DOWN },     { L"left", VK_LEFT },
    { L"right", VK_RIGHT },     { L"space", VK_SPACE },   { L"tab", VK_TAB },
    { L"enter", VK_RETURN },    { L"escape", VK_ESCAPE }, { L"backspace", VK_BACK },
};

std::wstring lowercase(std::wstring_view text)
{
    std::wstring result(text);
    for (wchar_t& character : result) character = static_cast<wchar_t>(std::towlower(character));
    return result;
}

std::optional<uint32_t> virtual_key_from_name(const std::wstring& name)
{
    if (name.size() == 1) {
        const wchar_t character = name.front();
        if (character >= L'a' && character <= L'z') {
            return static_cast<uint32_t>(L'A' + (character - L'a'));
        }
        if (character >= L'0' && character <= L'9') {
            return static_cast<uint32_t>(character);
        }
        return std::nullopt;
    }
    if (name.size() >= 2 && name[0] == L'f') {
        const std::wstring digits = name.substr(1);
        if (std::all_of(digits.begin(), digits.end(),
                        [](wchar_t character) { return character >= L'0' && character <= L'9'; })) {
            const int number = std::wcstol(digits.c_str(), nullptr, 10);
            if (number >= 1 && number <= 24) {
                return static_cast<uint32_t>(VK_F1 + number - 1);
            }
        }
    }
    for (const NamedKey& key : kNamedKeys) {
        if (name == key.name) return key.virtual_key;
    }
    return std::nullopt;
}

std::wstring key_name_from_virtual_key(uint32_t virtual_key)
{
    if (virtual_key >= L'A' && virtual_key <= L'Z') {
        return std::wstring(1, static_cast<wchar_t>(L'a' + (virtual_key - L'A')));
    }
    if (virtual_key >= L'0' && virtual_key <= L'9') {
        return std::wstring(1, static_cast<wchar_t>(virtual_key));
    }
    if (virtual_key >= VK_F1 && virtual_key <= VK_F24) {
        return L"f" + std::to_wstring(virtual_key - VK_F1 + 1);
    }
    for (const NamedKey& key : kNamedKeys) {
        if (virtual_key == key.virtual_key) return std::wstring(key.name);
    }
    return L"0x" + std::to_wstring(virtual_key);
}

std::vector<std::wstring> split_chord_parts(const std::wstring& text)
{
    std::vector<std::wstring> parts;
    size_t begin = 0;
    while (begin <= text.size()) {
        const size_t plus = text.find(L'+', begin);
        const size_t end = plus == std::wstring::npos ? text.size() : plus;
        parts.push_back(text.substr(begin, end - begin));
        if (plus == std::wstring::npos) break;
        begin = plus + 1;
    }
    return parts;
}

struct PipeIssue {
    int line = 0;
    std::wstring detail;
};

std::optional<std::wstring> builtin_shortcut_name(const KeyChord& chord)
{
    const auto matches = [&](uint32_t modifiers, uint32_t key) {
        return chord.modifiers == modifiers && chord.virtual_key == key;
    };
    if (matches(MOD_CONTROL, 'C') || matches(MOD_CONTROL | MOD_SHIFT, 'C')) return L"copy";
    if (matches(MOD_CONTROL, 'V') || matches(MOD_CONTROL | MOD_SHIFT, 'V')) return L"paste";
    if (matches(MOD_SHIFT, VK_INSERT)) return L"paste";
    return std::nullopt;
}

std::optional<PipeIssue> validate_pipes(const ConfigValues& values)
{
    std::map<std::pair<uint32_t, uint32_t>, PipeAction> bound;
    for (size_t index = 0; index < values.pipes.size(); ++index) {
        const auto& slot = values.pipes[index];
        if (!slot) continue;
        const auto action = static_cast<PipeAction>(index);
        const std::wstring name = pipe_action_name(action);
        if (!slot->key || trim(*slot->key).empty()) {
            return PipeIssue{ slot->table_line, name + L" requires key" };
        }
        if (!slot->command || trim(*slot->command).empty()) {
            return PipeIssue{ slot->command_line != 0 ? slot->command_line : slot->table_line,
                              name + L" requires command" };
        }
        std::wstring detail;
        const auto chord = parse_key_chord(*slot->key, detail);
        if (!chord) {
            return PipeIssue{ slot->key_line,
                              L"invalid key binding \"" + *slot->key + L"\": " + detail };
        }
        if (const auto conflict = builtin_shortcut_name(*chord)) {
            return PipeIssue{ slot->key_line,
                              L"key \"" + format_key_chord(*chord) + L"\" conflicts with the " +
                                  L"built-in " + *conflict + L" shortcut" };
        }
        const auto key = std::make_pair(chord->virtual_key, chord->modifiers);
        const auto found = bound.find(key);
        if (found != bound.end()) {
            return PipeIssue{ slot->key_line,
                              L"key \"" + format_key_chord(*chord) + L"\" is already bound to " +
                                  pipe_action_name(found->second) };
        }
        bound.emplace(key, action);
    }
    return std::nullopt;
}

} // namespace

std::wstring pipe_action_name(PipeAction action)
{
    const size_t index = static_cast<size_t>(action);
    if (index < std::size(kPipeActions)) return std::wstring(kPipeActions[index].table);
    return L"pipe";
}

std::optional<KeyChord> parse_key_chord(std::wstring_view input, std::wstring& detail)
{
    constexpr std::wstring_view kNeedsCtrlOrAlt = L"needs ctrl or alt";
    const std::wstring text = lowercase(trim(input));
    if (text.empty()) {
        detail = L"empty key binding";
        return std::nullopt;
    }
    std::vector<std::wstring> parts = split_chord_parts(text);
    if (parts.size() < 2) {
        detail.assign(kNeedsCtrlOrAlt);
        return std::nullopt;
    }

    KeyChord chord;
    for (size_t index = 0; index + 1 < parts.size(); ++index) {
        const std::wstring& part = parts[index];
        if (part.empty()) {
            detail = L"empty modifier";
            return std::nullopt;
        }
        uint32_t flag = 0;
        if (part == L"ctrl" || part == L"control") flag = MOD_CONTROL;
        else if (part == L"shift") flag = MOD_SHIFT;
        else if (part == L"alt") flag = MOD_ALT;
        else {
            detail = L"unknown modifier \"" + part + L"\"";
            return std::nullopt;
        }
        if ((chord.modifiers & flag) != 0) {
            detail = L"duplicate modifier \"" + part + L"\"";
            return std::nullopt;
        }
        chord.modifiers |= flag;
    }
    if ((chord.modifiers & (MOD_CONTROL | MOD_ALT)) == 0) {
        detail.assign(kNeedsCtrlOrAlt);
        return std::nullopt;
    }

    const std::wstring& base = parts.back();
    if (base.empty()) {
        detail = L"empty key name";
        return std::nullopt;
    }
    const auto virtual_key = virtual_key_from_name(base);
    if (!virtual_key) {
        detail = L"unknown key \"" + base + L"\"";
        return std::nullopt;
    }
    chord.virtual_key = *virtual_key;
    return chord;
}

std::wstring format_key_chord(const KeyChord& chord)
{
    std::wstring text;
    if ((chord.modifiers & MOD_CONTROL) != 0) text += L"ctrl+";
    if ((chord.modifiers & MOD_SHIFT) != 0) text += L"shift+";
    if ((chord.modifiers & MOD_ALT) != 0) text += L"alt+";
    text += key_name_from_virtual_key(chord.virtual_key);
    return text;
}

std::vector<PipeBinding> pipe_bindings(const Config& config)
{
    std::vector<PipeBinding> result;
    for (size_t index = 0; index < config.root.pipes.size(); ++index) {
        const auto& slot = config.root.pipes[index];
        if (!slot || !slot->key || !slot->command) continue;
        std::wstring detail;
        const auto chord = parse_key_chord(*slot->key, detail);
        if (!chord) continue;
        result.push_back(PipeBinding{ static_cast<PipeAction>(index), *chord, *slot->command });
    }
    return result;
}

std::wstring default_config_path()
{
    std::array<wchar_t, 32768> appdata{};
    const DWORD length = ::GetEnvironmentVariableW(L"APPDATA", appdata.data(),
                                                   static_cast<DWORD>(appdata.size()));
    if (length == 0 || length >= appdata.size()) return {};
    return std::wstring(appdata.data(), length) + L"\\Termole\\config.toml";
}

ConfigLoadResult load_config(const std::wstring& path, const std::optional<std::wstring>& profile)
{
    ConfigLoadResult result;
    std::wstring read_error;
    const auto text = read_utf8(path, read_error);
    if (!text) {
        result.ok = false;
        result.error = path + L": " + read_error;
        return result;
    }

    ConfigValues* current = &result.config.root;
    PipeConfig* current_pipe = nullptr;
    std::wstring current_pipe_name;
    std::wistringstream input(*text);
    std::wstring line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        const std::wstring content = trim(strip_comment(line));
        if (content.empty()) continue;

        if (content.front() == L'[') {
            if (content.back() != L']') {
                result.ok = false;
                result.error = path + L":" + std::to_wstring(line_number) + L": missing ']'";
                return result;
            }
            const std::wstring section = trim(content.substr(1, content.size() - 2));
            if (section == L"termole") {
                current = &result.config.root;
                current_pipe = nullptr;
            } else if (section.rfind(L"pipe.", 0) == 0) {
                const std::wstring name = section.substr(5);
                const auto action = pipe_action_from_name(name);
                if (!action) {
                    result.ok = false;
                    result.error = path + L":" + std::to_wstring(line_number) +
                        L": unsupported table [" + section + L"]";
                    return result;
                }
                auto& slot = result.config.root.pipes[static_cast<size_t>(*action)];
                if (!slot) {
                    slot.emplace();
                    slot->table_line = line_number;
                }
                current = &result.config.root;
                current_pipe = &*slot;
                current_pipe_name = section;
            } else if (section.rfind(L"profiles.", 0) == 0) {
                const std::wstring name = section.substr(9);
                if (name.empty()) {
                    result.ok = false;
                    result.error = path + L":" + std::to_wstring(line_number) + L": empty profile name";
                    return result;
                }
                if (name.rfind(L"pipe.", 0) == 0 || name.find(L".pipe.") != std::wstring::npos) {
                    result.ok = false;
                    result.error = path + L":" + std::to_wstring(line_number) +
                        L": pipe bindings are global; use [pipe.visible], [pipe.selected] or "
                        L"[pipe.scrollback]";
                    return result;
                }
                current = &result.config.profiles[name];
                current_pipe = nullptr;
            } else {
                result.ok = false;
                result.error = path + L":" + std::to_wstring(line_number) +
                    L": unsupported table [" + section + L"]";
                return result;
            }
            continue;
        }

        const size_t equals = content.find(L'=');
        if (equals == std::wstring::npos) {
            result.ok = false;
            result.error = path + L":" + std::to_wstring(line_number) + L": expected key = value";
            return result;
        }
        const std::wstring key = trim(content.substr(0, equals));
        if (key.empty()) {
            result.ok = false;
            result.error = path + L":" + std::to_wstring(line_number) + L": empty key";
            return result;
        }
        Value value;
        std::wstring value_error;
        if (!parse_value(content.substr(equals + 1), value, value_error)) {
            result.ok = false;
            result.error = path + L":" + std::to_wstring(line_number) + L": " + value_error;
            return result;
        }
        if (current_pipe) {
            if (key != L"key" && key != L"command") {
                result.ok = false;
                result.error = path + L":" + std::to_wstring(line_number) +
                    L": unknown key in [" + current_pipe_name + L"]: " + key;
                return result;
            }
            if (value.kind != Value::Kind::String) {
                result.ok = false;
                result.error = path + L":" + std::to_wstring(line_number) + L": " + key +
                    L" must be a string";
                return result;
            }
            if (key == L"key") {
                current_pipe->key = value.string;
                current_pipe->key_line = line_number;
            } else {
                current_pipe->command = value.string;
                current_pipe->command_line = line_number;
            }
            continue;
        }
        if (!assign(*current, key, value, value_error)) {
            result.ok = false;
            result.error = path + L":" + std::to_wstring(line_number) + L": " + value_error;
            return result;
        }
    }

    if (profile) {
        if (result.config.profiles.find(*profile) == result.config.profiles.end()) {
            result.ok = false;
            result.error = path + L": profile not found: " + *profile;
        }
    }
    if (const auto issue = validate_pipes(result.config.root)) {
        result.ok = false;
        result.error = path + L":" + std::to_wstring(issue->line) + L": " + issue->detail;
    }
    return result;
}

void apply_config(CliOptions& options, const Config& config,
                  const std::optional<std::wstring>& profile)
{
    ConfigValues values = config.root;
    if (profile) {
        const auto found = config.profiles.find(*profile);
        if (found == config.profiles.end()) return;
        const ConfigValues& selected = found->second;
        if (selected.font_family) values.font_family = selected.font_family;
        if (selected.font_size) values.font_size = selected.font_size;
        if (selected.theme) values.theme = selected.theme;
        if (selected.columns) values.columns = selected.columns;
        if (selected.rows) values.rows = selected.rows;
        if (selected.window_x) values.window_x = selected.window_x;
        if (selected.window_y) values.window_y = selected.window_y;
        if (selected.center) values.center = selected.center;
        if (selected.floating) values.floating = selected.floating;
        if (selected.borderless) values.borderless = selected.borderless;
        if (selected.opacity) values.opacity = selected.opacity;
        if (selected.title) values.title = selected.title;
        if (selected.icon) values.icon = selected.icon;
        if (selected.keep_window) values.keep_window = selected.keep_window;
        if (selected.close_on_exit) values.close_on_exit = selected.close_on_exit;
        if (selected.program) values.program = selected.program;
        if (!selected.arguments.empty()) values.arguments = selected.arguments;
        if (selected.working_directory) values.working_directory = selected.working_directory;
        if (!selected.environment.empty()) {
            values.environment.insert(values.environment.end(), selected.environment.begin(),
                                      selected.environment.end());
        }
    }

    if (!options.font_family && values.font_family) options.font_family = values.font_family;
    if (options.font_size_dip == 0 && values.font_size) options.font_size_dip = *values.font_size;
    if (options.theme == TERMOLE_THEME_SYSTEM && values.theme) options.theme = *values.theme;
    if (options.columns == 0 && values.columns) options.columns = *values.columns;
    if (options.rows == 0 && values.rows) options.rows = *values.rows;
    if (!options.window_x && values.window_x) options.window_x = values.window_x;
    if (!options.window_y && values.window_y) options.window_y = values.window_y;
    if (!options.center && values.center) options.center = *values.center;
    if (!options.floating && values.floating) options.floating = *values.floating;
    if (!options.borderless && values.borderless) options.borderless = *values.borderless;
    if (!options.opacity_percent && values.opacity) options.opacity_percent = values.opacity;
    if (!options.window_title && values.title) options.window_title = values.title;
    if (!options.window_icon && values.icon) options.window_icon = values.icon;
    if (options.exit_policy == ExitPolicy::Auto) {
        if (values.keep_window && *values.keep_window) options.exit_policy = ExitPolicy::KeepWindow;
        else if (values.close_on_exit && *values.close_on_exit)
            options.exit_policy = ExitPolicy::CloseOnExit;
    }
    if (!options.program && values.program) {
        options.program = values.program;
        options.arguments = values.arguments;
    }
    if (!options.working_directory && values.working_directory)
        options.working_directory = values.working_directory;
    if (options.environment.empty()) options.environment = values.environment;
}

} // namespace termole::app
