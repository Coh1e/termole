#include "capture.h"

#include <wincodec.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_set>

namespace {

template <class T>
void release(T*& pointer)
{
    if (pointer) {
        pointer->Release();
        pointer = nullptr;
    }
}

bool write_png(const std::filesystem::path& path, int width, int height, const void* bits,
               std::string& error)
{
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* properties = nullptr;

    HRESULT result = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&factory));
    if (SUCCEEDED(result)) result = factory->CreateStream(&stream);
    if (SUCCEEDED(result)) result = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(result)) {
        result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    }
    if (SUCCEEDED(result)) result = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(result)) result = encoder->CreateNewFrame(&frame, &properties);
    if (SUCCEEDED(result)) result = frame->Initialize(properties);
    if (SUCCEEDED(result)) {
        result = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(result)) result = frame->SetPixelFormat(&format);
    if (SUCCEEDED(result) && !IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) {
        result = E_FAIL;
    }
    const UINT stride = static_cast<UINT>(width) * 4u;
    if (SUCCEEDED(result)) {
        result = frame->WritePixels(static_cast<UINT>(height), stride,
                                    stride * static_cast<UINT>(height),
                                    static_cast<BYTE*>(const_cast<void*>(bits)));
    }
    if (SUCCEEDED(result)) result = frame->Commit();
    if (SUCCEEDED(result)) result = encoder->Commit();

    release(properties);
    release(frame);
    release(encoder);
    release(stream);
    release(factory);

    if (FAILED(result)) {
        error = "could not encode PNG (HRESULT " +
            std::to_string(static_cast<unsigned long>(result)) + ")";
        return false;
    }
    return true;
}

uint8_t channel(uint32_t pixel, int shift)
{
    return static_cast<uint8_t>((pixel >> shift) & 0xFFu);
}

} // namespace

namespace capture {

bool window(HWND hwnd, const std::filesystem::path& png, Stats& out_stats, std::string& error,
            std::span<Sample> samples)
{
    out_stats = Stats{};
    RECT rectangle{};
    if (!::GetWindowRect(hwnd, &rectangle)) {
        error = "GetWindowRect failed";
        return false;
    }
    const int width = rectangle.right - rectangle.left;
    const int height = rectangle.bottom - rectangle.top;
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        error = "unexpected window size";
        return false;
    }

    const HDC screen = ::GetDC(nullptr);
    if (!screen) {
        error = "GetDC failed";
        return false;
    }
    const HDC memory = ::CreateCompatibleDC(screen);
    if (!memory) {
        ::ReleaseDC(nullptr, screen);
        error = "CreateCompatibleDC failed";
        return false;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    const HBITMAP bitmap = ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) ::DeleteObject(bitmap);
        ::DeleteDC(memory);
        ::ReleaseDC(nullptr, screen);
        error = "CreateDIBSection failed";
        return false;
    }
    std::memset(bits, 0, static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    const HGDIOBJ previous = ::SelectObject(memory, bitmap);

    BOOL printed = ::PrintWindow(hwnd, memory, PW_RENDERFULLCONTENT);
    if (!printed) printed = ::PrintWindow(hwnd, memory, 0);
    ::GdiFlush();
    for (auto& sample : samples) sample.color = ::GetPixel(memory, sample.x, sample.y);

    const auto* pixels = static_cast<const uint32_t*>(bits);
    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    const uint32_t background = pixel_count != 0 ? pixels[0] : 0;
    const uint8_t background_red = channel(background, 16);
    const uint8_t background_green = channel(background, 8);
    const uint8_t background_blue = channel(background, 0);

    std::unordered_set<uint32_t> colors;
    colors.reserve(std::min<size_t>(pixel_count, 4096));
    unsigned long long ink = 0;
    unsigned long long strong = 0;
    for (size_t index = 0; index < pixel_count; ++index) {
        const uint32_t pixel = pixels[index];
        colors.insert(pixel & 0x00FFFFFFu);
        const int red = static_cast<int>(channel(pixel, 16)) - static_cast<int>(background_red);
        const int green = static_cast<int>(channel(pixel, 8)) - static_cast<int>(background_green);
        const int blue = static_cast<int>(channel(pixel, 0)) - static_cast<int>(background_blue);
        const int difference = std::max({ std::abs(red), std::abs(green), std::abs(blue) });
        if (difference >= 24) ++ink;
        if (difference >= 96) ++strong;
    }

    out_stats.width = width;
    out_stats.height = height;
    out_stats.colors = static_cast<unsigned long long>(colors.size());
    out_stats.background = background;
    out_stats.ink_pixels = ink;
    out_stats.ink_ratio =
        pixel_count == 0 ? 0.0 : static_cast<double>(ink) / static_cast<double>(pixel_count);
    out_stats.strong_pixels = strong;
    out_stats.print_window_ok = printed != FALSE;

    if (printed) (void)write_png(png, width, height, bits, error);

    ::SelectObject(memory, previous);
    ::DeleteObject(bitmap);
    ::DeleteDC(memory);
    ::ReleaseDC(nullptr, screen);

    if (!printed) {
        error = "PrintWindow failed";
        return false;
    }
    return error.empty();
}

bool write_stats(const std::filesystem::path& path, const Stats& stats, std::string& error)
{
    std::FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"w") != 0 || !file) {
        error = "could not open stats file";
        return false;
    }
    std::fprintf(file,
                 "width=%d\nheight=%d\ncolors=%llu\nbackground=0x%08X\ninkPixels=%llu\n"
                 "inkRatio=%.6f\nstrongPixels=%llu\nprintWindow=%s\n",
                 stats.width, stats.height, stats.colors, stats.background, stats.ink_pixels,
                 stats.ink_ratio, stats.strong_pixels, stats.print_window_ok ? "true" : "false");
    std::fclose(file);
    return true;
}

} // namespace capture
