#include "view/renderer.h"

#include <algorithm>
#include <string>
#include <utility>

namespace termole {
namespace {

struct GraphicsState {
    bool initialized = false;
    std::string error;
    win::com_ptr<ID2D1Factory> d2d;
    win::com_ptr<IDWriteFactory> dwrite;
};

GraphicsState& graphics_state()
{
    static GraphicsState state;
    return state;
}

bool ensure_graphics(std::string& error)
{
    GraphicsState& state = graphics_state();
    if (!state.initialized) {
        state.initialized = true;
        const HRESULT d2d = ::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, state.d2d.put());
        if (FAILED(d2d)) state.error = win::hresult_error("D2D1CreateFactory", d2d).message;
        if (state.error.empty()) {
            const HRESULT dwrite = ::DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(state.dwrite.put()));
            if (FAILED(dwrite))
                state.error = win::hresult_error("DWriteCreateFactory", dwrite).message;
        }
    }
    if (!state.error.empty()) {
        error = state.error;
        return false;
    }
    return true;
}

struct ResolvedCell {
    uint32_t foreground;
    uint32_t background;
    uint16_t attributes;
};

ResolvedCell resolve(const Cell& cell, bool cursor)
{
    uint32_t foreground = cell.foreground == kColorDefault ? kDefaultForeground : cell.foreground;
    uint32_t background = cell.background == kColorDefault ? kDefaultBackground : cell.background;
    if ((cell.attributes & kAttrInverse) != 0) std::swap(foreground, background);
    if (cursor) std::swap(foreground, background);
    return { foreground, background, cell.attributes };
}

void append_character(std::wstring& output, char32_t character)
{
    if (character == 0) character = U' ';
    if (character <= 0xFFFF) {
        output.push_back(static_cast<wchar_t>(character));
    } else if (character <= 0x10FFFF) {
        const char32_t value = character - 0x10000;
        output.push_back(static_cast<wchar_t>(0xD800 + (value >> 10)));
        output.push_back(static_cast<wchar_t>(0xDC00 + (value & 0x3FF)));
    } else {
        output.push_back(L'?');
    }
}

} // namespace

Result<std::unique_ptr<Renderer>> Renderer::create(HWND window, uint32_t font_size_dip)
{
    auto renderer =
        std::unique_ptr<Renderer>(new Renderer(window, font_size_dip == 0 ? 16 : font_size_dip));
    auto initialized = renderer->initialize();
    if (!initialized) return std::unexpected(initialized.error());
    return renderer;
}

Result<void> Renderer::initialize()
{
    std::string error;
    if (!ensure_graphics(error)) return std::unexpected(make_error(TERMOLE_STATUS_SYSTEM_ERROR, error));
    dpi_ = ::GetDpiForWindow(window_);
    if (dpi_ == 0) dpi_ = 96;
    auto format = create_format();
    if (!format) return format;
    create_target();
    return {};
}

Result<void> Renderer::create_format()
{
    const float font_pixels = static_cast<float>(font_size_dip_) * static_cast<float>(dpi_) / 96.0f;
    win::com_ptr<IDWriteTextFormat> format;
    win::com_ptr<IDWriteTextFormat> bold;

    HRESULT status = graphics_state().dwrite->CreateTextFormat(
        L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, font_pixels, L"en-us", format.put());
    if (FAILED(status)) {
        status = graphics_state().dwrite->CreateTextFormat(
            L"Courier New", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, font_pixels, L"en-us", format.put());
    }
    if (FAILED(status)) {
        return std::unexpected(win::hresult_error("IDWriteFactory::CreateTextFormat", status));
    }

    status = graphics_state().dwrite->CreateTextFormat(
        L"Consolas", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, font_pixels, L"en-us", bold.put());
    if (FAILED(status)) {
        status = graphics_state().dwrite->CreateTextFormat(
            L"Courier New", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, font_pixels, L"en-us", bold.put());
    }
    if (FAILED(status)) {
        return std::unexpected(
            win::hresult_error("IDWriteFactory::CreateTextFormat(bold)", status));
    }

    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    bold->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    bold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    bold->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    win::com_ptr<IDWriteTextLayout> layout;
    status = graphics_state().dwrite->CreateTextLayout(L"MMMMMMMMMM", 10, format.get(), 10000.0f,
                                                       1000.0f, layout.put());
    if (FAILED(status)) {
        return std::unexpected(win::hresult_error("IDWriteFactory::CreateTextLayout", status));
    }
    DWRITE_TEXT_METRICS metrics{};
    status = layout->GetMetrics(&metrics);
    if (FAILED(status) || metrics.width <= 0.0f || metrics.height <= 0.0f) {
        return std::unexpected(make_error(TERMOLE_STATUS_SYSTEM_ERROR,
                                          "DirectWrite returned invalid monospace cell metrics"));
    }

    format_ = std::move(format);
    bold_format_ = std::move(bold);
    cell_width_ = metrics.width / 10.0f;
    cell_height_ = metrics.height;
    return {};
}

bool Renderer::create_target()
{
    RECT client{};
    ::GetClientRect(window_, &client);
    const UINT32 width = static_cast<UINT32>(std::max<LONG>(1, client.right - client.left));
    const UINT32 height = static_cast<UINT32>(std::max<LONG>(1, client.bottom - client.top));
    const HRESULT status = graphics_state().d2d->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(window_, D2D1::SizeU(width, height)), target_.put());
    if (FAILED(status)) return false;

    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 1.0f), brush_.put()))) {
        brush_.reset();
        target_.reset();
        return false;
    }
    return true;
}

bool Renderer::ensure_target()
{
    if (target_ && brush_) return true;
    return create_target();
}

void Renderer::resize(uint32_t width, uint32_t height)
{
    if (!target_) return;
    if (FAILED(target_->Resize(D2D1::SizeU(std::max(1u, width), std::max(1u, height))))) {
        target_.reset();
        brush_.reset();
    }
}

void Renderer::set_dpi(uint32_t dpi)
{
    if (dpi == 0 || dpi == dpi_) return;
    const uint32_t previous_dpi = dpi_;
    dpi_ = dpi;
    auto format = create_format();
    if (!format) {
        dpi_ = previous_dpi;
        return;
    }
    target_.reset();
    brush_.reset();
}

bool Renderer::paint(const Snapshot& snapshot, bool focused)
{
    if (!ensure_target()) return false;

    const bool cursor_visible = focused && snapshot.cursor_visible;
    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(kDefaultBackground));

    for (int row = 0; row < snapshot.rows; ++row) {
        const float top = static_cast<float>(row) * cell_height_;
        const bool cursor_row = cursor_visible && row == snapshot.cursor_row;

        // Backgrounds first, merged per run.
        int background_start = -1;
        uint32_t background_color = 0;
        const auto flush_background = [&](int end) {
            if (background_start < 0) return;
            const float left = static_cast<float>(background_start) * cell_width_;
            const float right = static_cast<float>(end) * cell_width_;
            brush_->SetColor(D2D1::ColorF(background_color, 1.0f));
            target_->FillRectangle(D2D1::RectF(left, top, right, top + cell_height_), brush_.get());
            background_start = -1;
        };

        for (int column = 0; column < snapshot.columns; ++column) {
            const bool cursor = cursor_row && column == snapshot.cursor_column;
            const ResolvedCell resolved = resolve(
                snapshot.cells[static_cast<size_t>(row) * snapshot.columns + column], cursor);
            if (resolved.background == kDefaultBackground) {
                flush_background(column);
                continue;
            }
            if (background_start < 0) {
                background_start = column;
                background_color = resolved.background;
            } else if (resolved.background != background_color) {
                flush_background(column);
                background_start = column;
                background_color = resolved.background;
            }
        }
        flush_background(snapshot.columns);

        // Text runs, merged by foreground colour and style.
        int run_start = -1;
        uint32_t run_color = 0;
        uint16_t run_style = 0;
        std::wstring run;
        const auto flush_text = [&](int end) {
            if (run_start < 0 || run.empty()) {
                run_start = -1;
                run.clear();
                return;
            }
            const float left = static_cast<float>(run_start) * cell_width_;
            brush_->SetColor(D2D1::ColorF(
                run_color, (run_style & kAttrDim) != 0 ? 0.65f : 1.0f));
            IDWriteTextFormat* text_format =
                (run_style & kAttrBold) != 0 ? bold_format_.get() : format_.get();
            const float right = left + (static_cast<float>(run.size()) + 2.0f) * cell_width_;
            target_->DrawTextW(run.c_str(), static_cast<UINT32>(run.size()), text_format,
                               D2D1::RectF(left, top, right, top + cell_height_ * 2.0f),
                               brush_.get(), D2D1_DRAW_TEXT_OPTIONS_NONE,
                               DWRITE_MEASURING_MODE_NATURAL);
            if ((run_style & kAttrUnderline) != 0) {
                const float thickness = std::max(1.0f, cell_height_ / 14.0f);
                target_->FillRectangle(
                    D2D1::RectF(left, top + cell_height_ * 0.84f,
                                left + static_cast<float>(end - run_start) * cell_width_,
                                top + cell_height_ * 0.84f + thickness),
                    brush_.get());
            }
            run_start = -1;
            run.clear();
        };

        for (int column = 0; column < snapshot.columns; ++column) {
            const bool cursor = cursor_row && column == snapshot.cursor_column;
            const ResolvedCell resolved = resolve(
                snapshot.cells[static_cast<size_t>(row) * snapshot.columns + column], cursor);
            const uint16_t style = resolved.attributes &
                static_cast<uint16_t>(kAttrBold | kAttrUnderline | kAttrDim | kAttrItalic);
            if (run_start >= 0 && (resolved.foreground != run_color || style != run_style)) {
                flush_text(column);
            }
            if (run_start < 0) {
                run_start = column;
                run_color = resolved.foreground;
                run_style = style;
            }
            const char32_t character =
                (resolved.attributes & kAttrHidden) != 0
                ? U' '
                : snapshot.cells[static_cast<size_t>(row) * snapshot.columns + column].character;
            append_character(run, character);
        }
        flush_text(snapshot.columns);
    }

    const HRESULT status = target_->EndDraw();
    if (status == D2DERR_RECREATE_TARGET) {
        target_.reset();
        brush_.reset();
        return false;
    }
    return SUCCEEDED(status);
}

} // namespace termole
