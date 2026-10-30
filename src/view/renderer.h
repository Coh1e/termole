#pragma once

#include "common/error.h"
#include "common/win.h"
#include "core/terminal.h"

#include <d2d1.h>
#include <dwrite.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <utility>

namespace termole {

enum class Theme : uint32_t {
    System = 0,
    Dark = 1,
    Light = 2,
};

struct Appearance {
    std::wstring font_family = L"Consolas";
    uint32_t font_size_dip = 16;
    Theme theme = Theme::System;
};

// Draws one Session snapshot into a child HWND with Direct2D and DirectWrite.
// All methods are UI-thread only, matching the View contract.
class Renderer {
public:
    ~Renderer() = default;

    static Result<std::unique_ptr<Renderer>> create(HWND window, Appearance appearance);

    void resize(uint32_t width, uint32_t height);
    void set_dpi(uint32_t dpi);
    Result<void> set_appearance(const Appearance& appearance);
    bool paint(const Snapshot& snapshot, bool focused);

    [[nodiscard]] float cell_width() const noexcept { return cell_width_; }
    [[nodiscard]] float cell_height() const noexcept { return cell_height_; }

private:
    Renderer(HWND window, Appearance appearance)
        : window_(window), font_family_(std::move(appearance.font_family)),
          font_size_dip_(appearance.font_size_dip), theme_(appearance.theme) {}

    Result<void> initialize();
    Result<void> create_format();
    bool ensure_target();
    bool create_target();
    void resolve_theme_colors();

    HWND window_ = nullptr;
    uint32_t dpi_ = 96;
    std::wstring font_family_ = L"Consolas";
    uint32_t font_size_dip_ = 16;
    Theme theme_ = Theme::System;
    uint32_t default_foreground_ = kDefaultForeground;
    uint32_t default_background_ = kDefaultBackground;
    float cell_width_ = 8.0f;
    float cell_height_ = 16.0f;

    win::com_ptr<ID2D1HwndRenderTarget> target_;
    win::com_ptr<IDWriteTextFormat> format_;
    win::com_ptr<IDWriteTextFormat> bold_format_;
    win::com_ptr<ID2D1SolidColorBrush> brush_;
};

} // namespace termole
