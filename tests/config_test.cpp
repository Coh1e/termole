#include "common/config.h"

#include <iostream>

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) return 2;
    auto loaded = termole::app::load_config(argv[1], argc >= 3 ? std::optional<std::wstring>{ argv[2] }
                                                               : std::nullopt);
    std::wcout << L"ok=" << loaded.ok << L" error=" << loaded.error << L"\n";
    if (!loaded.ok) return 1;
    termole::app::CliOptions options;
    termole::app::apply_config(options, loaded.config,
                               argc >= 3 ? std::optional<std::wstring>{ argv[2] } : std::nullopt);
    std::wcout << L"columns=" << options.columns << L" rows=" << options.rows
               << L" program=" << (options.program ? *options.program : L"<none>")
               << L" args=" << options.arguments.size()
               << L" env=" << options.environment.size() << L"\n";
    for (const auto& argument : options.arguments) std::wcout << L"arg=" << argument << L"\n";
    for (const auto& entry : options.environment) std::wcout << L"env=" << entry << L"\n";
    return 0;
}
