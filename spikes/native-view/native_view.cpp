// Native API feasibility probe, following Microsoft's DirectWrite HelloWorld
// resource separation pattern. No terminal engine or product ABI is implemented.
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

struct TextResources {
    wil::com_ptr<ID2D1Factory> d2d;
    wil::com_ptr<IDWriteFactory> dwrite;
    wil::com_ptr<IDWriteTextFormat> format;

    TextResources()
    {
        THROW_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.put()));
        THROW_IF_FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwrite.put())));
        THROW_IF_FAILED(dwrite->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 22, L"zh-CN", format.put()));
    }

    void draw(ID2D1RenderTarget* target, const std::wstring& text)
    {
        wil::com_ptr<ID2D1SolidColorBrush> brush;
        THROW_IF_FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0.88f, 0.90f, 0.93f), brush.put()));
        const auto size = target->GetSize();
        target->BeginDraw();
        target->Clear(D2D1::ColorF(0.06f, 0.07f, 0.09f));
        target->DrawTextW(text.data(), static_cast<UINT32>(text.size()), format.get(),
            D2D1::RectF(20, 18, size.width - 20, size.height - 18), brush.get());
        THROW_IF_FAILED(target->EndDraw());
    }
};

// Externally-owned snapshot only; deliberately not named Session. This proves
// HWND ownership and API rendering primitives, not a complete terminal View.
struct ChildView {
    TextResources& resources;
    const std::wstring& snapshot;
    wil::unique_hwnd window;
    wil::com_ptr<ID2D1HwndRenderTarget> target;

    ChildView(HWND parent, TextResources& renderer, const std::wstring& text) : resources(renderer), snapshot(text)
    {
        window.reset(CreateWindowExW(0, L"TermoleNativeProbeChild", nullptr,
            WS_CHILD | WS_VISIBLE, 0, 0, 620, 220, parent, nullptr, GetModuleHandleW(nullptr), nullptr));
        THROW_LAST_ERROR_IF(!window);
        THROW_IF_FAILED(resources.d2d->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(window.get(), D2D1::SizeU(620, 220)), target.put()));
        resources.draw(target.get(), snapshot);
    }
};

void save_snapshot(TextResources& text, const std::wstring& snapshot, const std::filesystem::path& path)
{
    wil::com_ptr<IWICImagingFactory> wic;
    THROW_IF_FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(wic.put())));
    wil::com_ptr<IWICBitmap> bitmap;
    THROW_IF_FAILED(wic->CreateBitmap(620, 220, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapCacheOnLoad, bitmap.put()));
    wil::com_ptr<ID2D1RenderTarget> target;
    THROW_IF_FAILED(text.d2d->CreateWicBitmapRenderTarget(bitmap.get(), D2D1::RenderTargetProperties(), target.put()));
    text.draw(target.get(), snapshot);

    // Ensure the image contains text, not just a successfully allocated surface.
    std::vector<BYTE> pixels(620 * 220 * 4);
    THROW_IF_FAILED(bitmap->CopyPixels(nullptr, 620 * 4, static_cast<UINT>(pixels.size()), pixels.data()));
    size_t bright_pixels = 0;
    for (size_t i = 0; i < pixels.size(); i += 4)
        if (pixels[i] > 100 && pixels[i + 1] > 100 && pixels[i + 2] > 100) ++bright_pixels;
    THROW_HR_IF(E_FAIL, bright_pixels < 100);

    std::filesystem::create_directories(path.parent_path());
    wil::com_ptr<IWICStream> stream;
    THROW_IF_FAILED(wic->CreateStream(stream.put()));
    THROW_IF_FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    wil::com_ptr<IWICBitmapEncoder> encoder;
    THROW_IF_FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
    THROW_IF_FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
    wil::com_ptr<IWICBitmapFrameEncode> frame;
    THROW_IF_FAILED(encoder->CreateNewFrame(frame.put(), nullptr));
    THROW_IF_FAILED(frame->Initialize(nullptr));
    THROW_IF_FAILED(frame->SetSize(620, 220));
    auto format = GUID_WICPixelFormat32bppBGRA;
    THROW_IF_FAILED(frame->SetPixelFormat(&format));
    THROW_IF_FAILED(frame->WriteSource(bitmap.get(), nullptr));
    THROW_IF_FAILED(frame->Commit());
    THROW_IF_FAILED(encoder->Commit());
}

int wmain(int argc, wchar_t** argv)
try {
    if (argc != 2) return 2;
    THROW_IF_FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    const auto uninitialize = wil::scope_exit([] { CoUninitialize(); });
    WNDCLASSW child{};
    child.lpfnWndProc = DefWindowProcW;
    child.hInstance = GetModuleHandleW(nullptr);
    child.lpszClassName = L"TermoleNativeProbeChild";
    THROW_LAST_ERROR_IF(RegisterClassW(&child) == 0);
    const auto unregister = wil::scope_exit([&] { UnregisterClassW(child.lpszClassName, child.hInstance); });

    wil::unique_hwnd standalone(CreateWindowExW(0, L"STATIC", L"Standalone shell", WS_OVERLAPPEDWINDOW,
        0, 0, 640, 260, nullptr, nullptr, child.hInstance, nullptr));
    wil::unique_hwnd host(CreateWindowExW(0, L"STATIC", L"Embedding host", WS_OVERLAPPEDWINDOW,
        0, 0, 800, 500, nullptr, nullptr, child.hInstance, nullptr));
    THROW_LAST_ERROR_IF(!standalone || !host);
    TextResources renderer;
    std::wstring snapshot = L"Termole / Pip - native rendering probe\n"
        L"PowerShell > hello\n"
        L"\u4e2d\u6587\u8f93\u51fa  combining: e\u0301  emoji: \U0001f42d\n"
        L"DirectWrite + Direct2D / Win32 child HWND";
    {
        ChildView view(standalone.get(), renderer, snapshot);
        THROW_HR_IF(E_FAIL, GetParent(view.window.get()) != standalone.get());
    }
    snapshot += L"\nSnapshot survives child destruction.";
    {
        ChildView view(host.get(), renderer, snapshot);
        THROW_HR_IF(E_FAIL, GetParent(view.window.get()) != host.get());
        THROW_IF_WIN32_BOOL_FALSE(MoveWindow(view.window.get(), 20, 30, 620, 220, FALSE));
        RECT rect{};
        THROW_IF_WIN32_BOOL_FALSE(GetClientRect(view.window.get(), &rect));
        THROW_HR_IF(E_FAIL, rect.right != 620 || rect.bottom != 220);
        save_snapshot(renderer, snapshot, std::filesystem::absolute(argv[1]));
    }
    THROW_HR_IF(E_FAIL, !IsWindow(standalone.get()) || !IsWindow(host.get()));
    std::cout << "PASS: native child ownership, independent snapshot lifetime, D2D HWND target, DirectWrite/WIC text\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
