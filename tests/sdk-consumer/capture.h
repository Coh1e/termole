#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace capture {

struct Stats {
    int width = 0;
    int height = 0;
    unsigned long long colors = 0;
    uint32_t background = 0;
    unsigned long long ink_pixels = 0;
    double ink_ratio = 0.0;
    unsigned long long strong_pixels = 0;
    bool print_window_ok = false;
};

struct Sample {
    int x, y;
    COLORREF color = CLR_INVALID;
};

enum class Method { PrintWindow, PrintClient };

// Captures hwnd, writes a PNG, and returns pixel statistics. PrintClient sends
// WM_PRINTCLIENT synchronously to isolate the off-screen paint path.
bool window(HWND hwnd, const std::filesystem::path& png, Stats& out_stats, std::string& error,
            std::span<Sample> samples = {}, Method method = Method::PrintWindow);

bool write_stats(const std::filesystem::path& path, const Stats& stats, std::string& error);

} // namespace capture
