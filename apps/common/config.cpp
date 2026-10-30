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

} // namespace

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
            } else if (section.rfind(L"profiles.", 0) == 0) {
                const std::wstring name = section.substr(9);
                if (name.empty()) {
                    result.ok = false;
                    result.error = path + L":" + std::to_wstring(line_number) + L": empty profile name";
                    return result;
                }
                current = &result.config.profiles[name];
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
