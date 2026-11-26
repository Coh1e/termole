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

// Captures hwnd with PrintWindow(PW_RENDERFULLCONTENT), writes a PNG for the
// evidence trail, and returns tolerant pixel statistics. The caller decides
// whether the numbers are acceptable; this helper asserts nothing.
bool window(HWND hwnd, const std::filesystem::path& png, Stats& out_stats, std::string& error,
            std::span<Sample> samples = {});

bool write_stats(const std::filesystem::path& path, const Stats& stats, std::string& error);

} // namespace capture
