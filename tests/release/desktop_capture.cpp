// Observe actual Windows presentations of the packaged fzf preview. Only the
// selector's unobstructed preview rectangle leaves the GPU.
#include "common/win.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {
namespace fs = std::filesystem;
using termole::win::com_ptr;
using termole::win::unique_handle;
using Pixels = std::vector<std::uint8_t>;

// A caller can retry an unobservable transition; input and evidence I/O errors
// are failures of the test itself.
struct Unobservable : std::runtime_error { using std::runtime_error::runtime_error; };

void dx(HRESULT status, const char* operation)
{
    if (FAILED(status)) {
        char code[24];
        std::snprintf(code, sizeof(code), " (0x%08lX)", static_cast<unsigned long>(status));
        throw Unobservable(std::string(operation) + code);
    }
}

LONGLONG tick()
{
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

std::uintptr_t number(const wchar_t* input)
{
    wchar_t* end = nullptr;
    errno = 0;
    auto value = std::wcstoull(input, &end, 10);
    if (*input < L'0' || *input > L'9' || !value || *end || errno == ERANGE ||
        value > UINTPTR_MAX)
        throw std::runtime_error("invalid handle or process ID");
    return static_cast<std::uintptr_t>(value);
}

std::wstring image_name(HANDLE process)
{
    wchar_t path[32768];
    DWORD size = ARRAYSIZE(path);
    if (!QueryFullProcessImageNameW(process, 0, path, &size))
        throw std::runtime_error("cannot identify process image");
    return {path, size};
}

bool active_desktop()
{
    HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!input) return false;
    BOOL receives_input = FALSE;
    DWORD needed = 0;
    bool active = GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_IO,
                      &receives_input, sizeof(receives_input), &needed) && receives_input;
    CloseDesktop(input);
    return active;
}

void require_active_desktop()
{
    if (!active_desktop()) throw Unobservable("the capture process is not on the active input desktop");
}

void verify_clear(HWND other, const RECT& area, HWND root, HWND level)
{
    if (!IsWindowVisible(other) || IsIconic(other)) return;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(other, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return;
    RECT bounds{}, overlap{};
    if (!GetWindowRect(other, &bounds)) throw Unobservable("cannot verify overlapping window bounds");
    if (!IntersectRect(&overlap, &bounds, &area)) return;
    BYTE alpha{};
    DWORD flags{};
    COLORREF key{};
    if (GetLayeredWindowAttributes(other, &key, &alpha, &flags) && (flags & LWA_ALPHA) && alpha == 0) return;
    // Topmost shell windows can have a large bounding box and an empty or
    // nonrectangular visible region. An ordinary window has no custom region.
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    if (region) {
        const int kind = GetWindowRgn(other, region);
        OffsetRect(&overlap, -bounds.left, -bounds.top);
        const bool clear = kind != ERROR && !RectInRegion(region, &overlap);
        DeleteObject(region);
        if (clear) return;
    }
    char window_class[128]{}, hit_class[128]{}, reason[650]{};
    GetClassNameA(other, window_class, ARRAYSIZE(window_class));
    POINT center{area.left + (area.right - area.left) / 2, area.top + (area.bottom - area.top) / 2};
    HWND hit = GetAncestor(WindowFromPoint(center), GA_ROOT);
    GetClassNameA(hit, hit_class, ARRAYSIZE(hit_class));
    std::snprintf(reason, sizeof(reason),
                  "preview occluded by class '%s', rect=(%ld,%ld,%ld,%ld), preview=(%ld,%ld,%ld,%ld), "
                  "z-level=%s, pickerForeground=%d, pickerTopmost=%d, occluderTopmost=%d, "
                  "centerRoot='%s' (picker=%d, occluder=%d)",
                  window_class, bounds.left, bounds.top, bounds.right, bounds.bottom,
                  area.left, area.top, area.right, area.bottom, level == root ? "top-level" : "child",
                  GetForegroundWindow() == root, bool(GetWindowLongPtrW(root, GWL_EXSTYLE) & WS_EX_TOPMOST),
                  bool(GetWindowLongPtrW(other, GWL_EXSTYLE) & WS_EX_TOPMOST), hit_class, hit == root, hit == other);
    throw Unobservable(reason);
}

struct Picker {
    HWND root, view;
    RECT view_rect{}, area{};
    DWORD pid{};
    unique_handle process;

    Picker(HWND root_window, HWND view_window, const fs::path& exe) : root(root_window), view(view_window)
    {
        GetWindowThreadProcessId(root, &pid);
        process.reset(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
        if (!process || !fs::equivalent(image_name(process.get()), exe) || GetAncestor(view, GA_ROOT) != root)
            throw std::runtime_error("window does not belong to the candidate GUI");
        wchar_t name[64]{};
        if (!GetClassNameW(view, name, ARRAYSIZE(name)) || std::wcscmp(name, L"TermoleViewWindow") != 0)
            throw std::runtime_error("unexpected preview window class");
        if (!GetWindowRect(view, &view_rect)) throw Unobservable("cannot locate preview window");
        // fzf's right-half preview. The insets match the cropped #54 control.
        area = {view_rect.left + (view_rect.right - view_rect.left) / 2 + 9,
                view_rect.top + 2, view_rect.right - 9, view_rect.bottom - 5};
        if (area.right <= area.left || area.bottom <= area.top) throw Unobservable("empty preview area");
    }

    void verify_visible() const
    {
        DWORD root_pid{}, view_pid{}, cloaked{};
        RECT current{};
        if (!GetWindowThreadProcessId(root, &root_pid) || root_pid != pid ||
            !GetWindowThreadProcessId(view, &view_pid) || view_pid != pid ||
            WaitForSingleObject(process.get(), 0) != WAIT_TIMEOUT || GetAncestor(view, GA_ROOT) != root ||
            !GetWindowRect(view, &current) || !EqualRect(&view_rect, &current) ||
            !IsWindowVisible(view) || IsIconic(root) ||
            FAILED(DwmGetWindowAttribute(root, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) || cloaked)
            throw Unobservable("preview moved or is not on the active visible desktop");
        require_active_desktop();

        // Walk the complete z-order above the view, including its siblings and
        // windows separate from this process. A small popup may cover only one glyph.
        for (HWND item = view;;) {
            HWND parent = item == root ? nullptr : GetParent(item);
            if (item != root && !parent) throw Unobservable("preview window hierarchy changed");
            unsigned visited = 0;
            for (HWND above = GetTopWindow(parent); above != item; above = GetWindow(above, GW_HWNDNEXT)) {
                if (!above || ++visited > 4096) throw Unobservable("preview z-order changed");
                verify_clear(above, area, root, item);
            }
            if (item == root) break;
            item = parent;
        }
        for (HWND child = GetTopWindow(view); child; child = GetWindow(child, GW_HWNDNEXT))
            verify_clear(child, area, root, view);
    }
};

class ConsoleQuery {
    unique_handle fzf_, input_;
    DWORD pid_;
    std::vector<INPUT_RECORD> records_;
public:
    ConsoleQuery(DWORD pid, std::wstring_view text) : pid_(pid)
    {
        fzf_.reset(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid));
        if (!fzf_ || _wcsicmp(fs::path(image_name(fzf_.get())).filename().c_str(), L"fzf.exe") != 0)
            throw std::runtime_error("invalid fzf process");
        if (text.empty() || text.size() > 128) throw std::runtime_error("invalid query length");
        for (wchar_t letter : text) {
            INPUT_RECORD key{};
            key.EventType = KEY_EVENT;
            key.Event.KeyEvent.wRepeatCount = 1;
            key.Event.KeyEvent.uChar.UnicodeChar = letter;
            key.Event.KeyEvent.bKeyDown = TRUE;
            records_.push_back(key);
            key.Event.KeyEvent.bKeyDown = FALSE;
            records_.push_back(key);
        }
        FreeConsole();
        if (!AttachConsole(pid)) throw std::runtime_error("cannot attach to fzf console");
        input_.reset(CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_EXISTING, 0, nullptr));
        if (input_.get() == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open fzf console input");
    }
    ~ConsoleQuery() { FreeConsole(); }

    LONGLONG send()
    {
        DWORD ids[128]{};
        const DWORD count = GetConsoleProcessList(ids, ARRAYSIZE(ids));
        if (!count || count > ARRAYSIZE(ids) || std::find(ids, ids + count, pid_) == ids + count ||
            WaitForSingleObject(fzf_.get(), 0) != WAIT_TIMEOUT)
            throw std::runtime_error("fzf no longer owns the attached console");
        const DWORD size = static_cast<DWORD>(records_.size());
        DWORD written = 0;
        auto begin = tick();
        if (!WriteConsoleInputW(input_.get(), records_.data(), size, &written) || written != size)
            throw std::runtime_error("could not write the complete query");
        return begin;
    }
};

enum class PointerState { clear, overlap, unknown };

const char* pointer_name(PointerState state)
{
    switch (state) {
    case PointerState::clear: return "clear";
    case PointerState::overlap: return "overlap";
    default: return "unknown";
    }
}

constexpr PointerState pointer_bounds(std::int64_t left, std::int64_t top, UINT width, UINT height, const RECT& area)
{
    if (!width || !height) return PointerState::unknown;
    return left < area.right && top < area.bottom && left + width > area.left && top + height > area.top
        ? PointerState::overlap : PointerState::clear;
}

struct DuplicationPointer {
    std::optional<DXGI_OUTDUPL_POINTER_POSITION> position;
    UINT width{}, height{};

    constexpr void update(const DXGI_OUTDUPL_FRAME_INFO& info, const DXGI_OUTDUPL_POINTER_SHAPE_INFO& shape)
    {
        if (info.LastMouseUpdateTime.QuadPart) position = info.PointerPosition;
        if (!info.PointerShapeBufferSize) return;
        width = shape.Width;
        height = 0;
        if (shape.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR ||
            shape.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR)
            height = shape.Height;
        else if (shape.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME && !(shape.Height % 2))
            height = shape.Height / 2; // Stacked AND and XOR masks.
    }

    constexpr PointerState within(const RECT& area) const
    {
        if (!position) return PointerState::unknown;
        if (!position->Visible) return PointerState::clear;
        // DXGI already supplies the top-left, local to the duplicated output.
        return pointer_bounds(position->Position.x, position->Position.y, width, height, area);
    }

    constexpr PointerState observe(const DXGI_OUTDUPL_FRAME_INFO& info,
                                   const DXGI_OUTDUPL_POINTER_SHAPE_INFO& shape, const RECT& area,
                                   PointerState initial = PointerState::unknown)
    {
        const auto prior = position ? within(area) : initial;
        update(info, shape);
        if (!position) return initial;
        const auto latest = within(area);
        if (info.LastPresentTime.QuadPart && info.LastMouseUpdateTime.QuadPart > info.LastPresentTime.QuadPart) {
            // DXGI only reports the last move. Even identical endpoints can hide
            // an intervening pointer position at the earlier presentation.
            return prior == PointerState::overlap || latest == PointerState::overlap
                ? PointerState::overlap : PointerState::unknown;
        }
        return latest;
    }
};

// Replay independent mouse, shape and presentation events against one physical pixel.
static_assert([] {
    DuplicationPointer cursor{};
    DXGI_OUTDUPL_FRAME_INFO mouse{};
    mouse.LastMouseUpdateTime.QuadPart = 1;
    mouse.PointerPosition = {{-2, 0}, TRUE};
    cursor.update(mouse, {});
    const RECT pixel{0, 1, 1, 2};
    if (cursor.within(pixel) != PointerState::unknown) return false;
    DXGI_OUTDUPL_FRAME_INFO shape_change{};
    shape_change.LastMouseUpdateTime.QuadPart = 0;
    shape_change.PointerShapeBufferSize = 4;
    cursor.update(shape_change, {DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME, 3, 4, 1, {2, 1}});
    auto presentation = shape_change;
    presentation.PointerShapeBufferSize = 0;
    cursor.update(presentation, {}); // Retain the last visibility, position and shape.
    if (cursor.within(pixel) != PointerState::overlap ||
        cursor.within({0, 2, 1, 3}) != PointerState::clear) return false;
    mouse.PointerPosition.Position.x = -3;
    cursor.update(mouse, {});
    if (cursor.within(pixel) != PointerState::clear) return false;
    mouse.PointerPosition = {{0, 0}, FALSE}; // Pointer left the duplicated output.
    cursor.update(mouse, {});
    if (cursor.within(pixel) != PointerState::clear) return false;
    mouse.PointerPosition.Visible = TRUE;
    cursor.update(mouse, {});
    cursor.update(shape_change, {}); // The driver reported a change but supplied no usable dimensions.
    return cursor.within(pixel) == PointerState::unknown;
}());

static_assert([] {
    const RECT pixel{0, 0, 1, 1};
    DXGI_OUTDUPL_FRAME_INFO frame{};
    frame.LastPresentTime.QuadPart = 0;
    frame.LastMouseUpdateTime.QuadPart = 1;
    frame.PointerPosition = {{0, 0}, TRUE};
    frame.PointerShapeBufferSize = 4;
    DuplicationPointer cursor{};
    cursor.observe(frame, {DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR, 1, 1, 4, {}}, pixel);
    frame.LastPresentTime.QuadPart = 2;
    frame.LastMouseUpdateTime.QuadPart = 3;
    frame.PointerPosition.Position.x = 1;
    frame.PointerShapeBufferSize = 0;
    if (cursor.observe(frame, {}, pixel) != PointerState::overlap) return false;
    frame.LastPresentTime.QuadPart = 4;
    frame.LastMouseUpdateTime.QuadPart = 5;
    if (cursor.observe(frame, {}, pixel) != PointerState::unknown) return false;
    frame.LastMouseUpdateTime.QuadPart = 0;
    return cursor.observe(frame, {}, pixel) == PointerState::clear;
}(), "A later pointer move off the preview cannot clear an earlier presentation");

std::optional<CURSORINFO> current_cursor()
{
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetCursorInfo(&info)) return std::nullopt;
    return info;
}

// The first desktop presentation may precede DXGI's initial mouse-only event.
// Bracket that bootstrap interval with the physical Win32 cursor (hotspot coordinates).
PointerState initial_pointer(const std::optional<CURSORINFO>& before, const RECT& area)
{
    const auto after = current_cursor();
    if (!before || !after || before->flags != after->flags)
        return PointerState::unknown;
    if (!(after->flags & CURSOR_SHOWING)) return PointerState::clear;
    if (before->hCursor != after->hCursor || before->ptScreenPos.x != after->ptScreenPos.x ||
        before->ptScreenPos.y != after->ptScreenPos.y)
        return PointerState::unknown;
    ICONINFO icon{};
    if (!GetIconInfo(after->hCursor, &icon)) return PointerState::unknown;
    BITMAP bitmap{};
    const bool color = icon.hbmColor != nullptr;
    const auto bytes = GetObjectW(color ? icon.hbmColor : icon.hbmMask, sizeof(bitmap), &bitmap);
    if (icon.hbmColor) DeleteObject(icon.hbmColor);
    if (icon.hbmMask) DeleteObject(icon.hbmMask);
    if (bytes != sizeof(bitmap) || bitmap.bmWidth <= 0 || bitmap.bmHeight <= 0 ||
        (!color && bitmap.bmHeight % 2)) return PointerState::unknown;
    return pointer_bounds(std::int64_t(after->ptScreenPos.x) - icon.xHotspot,
                          std::int64_t(after->ptScreenPos.y) - icon.yHotspot,
                          UINT(bitmap.bmWidth), UINT(color ? bitmap.bmHeight : bitmap.bmHeight / 2), area);
}

struct Desktop {
    com_ptr<ID3D11Device> device;
    com_ptr<ID3D11DeviceContext> context;
    com_ptr<IDXGIOutputDuplication> duplication;
    com_ptr<ID3D11Texture2D> staging;
    D3D11_BOX box{};
    UINT width, height;
    DuplicationPointer pointer;
    RECT output_area{};

    explicit Desktop(const RECT& area) : width(area.right - area.left), height(area.bottom - area.top)
    {
        com_ptr<IDXGIFactory1> factory;
        dx(CreateDXGIFactory1(IID_PPV_ARGS(factory.put())), "CreateDXGIFactory1");
        com_ptr<IDXGIAdapter1> adapter;
        com_ptr<IDXGIOutput> output;
        DXGI_OUTPUT_DESC desc{};
        for (UINT a = 0; !output; ++a) {
            HRESULT hr = factory->EnumAdapters1(a, adapter.put());
            if (hr == DXGI_ERROR_NOT_FOUND) throw Unobservable("preview is not fully on one desktop output");
            dx(hr, "EnumAdapters1");
            for (UINT i = 0;; ++i) {
                hr = adapter->EnumOutputs(i, output.put());
                if (hr == DXGI_ERROR_NOT_FOUND) break;
                dx(hr, "EnumOutputs");
                dx(output->GetDesc(&desc), "GetDesc");
                RECT overlap{};
                if (desc.AttachedToDesktop && IntersectRect(&overlap, &desc.DesktopCoordinates, &area) &&
                    EqualRect(&overlap, &area)) break;
                output.reset();
            }
        }
        if (desc.Rotation != DXGI_MODE_ROTATION_IDENTITY && desc.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED)
            throw Unobservable("preview on rotated output");
        dx(D3D11CreateDevice(adapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                             D3D11_SDK_VERSION, device.put(), nullptr, context.put()), "D3D11CreateDevice");
        com_ptr<IDXGIOutput1> output1;
        dx(output->QueryInterface(IID_PPV_ARGS(output1.put())), "IDXGIOutput1");
        dx(output1->DuplicateOutput(device.get(), duplication.put()), "DuplicateOutput");
        DXGI_OUTDUPL_DESC surface{};
        duplication->GetDesc(&surface);
        if (surface.ModeDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) throw Unobservable("unexpected desktop format");
        box = {UINT(area.left - desc.DesktopCoordinates.left), UINT(area.top - desc.DesktopCoordinates.top),
               0, UINT(area.right - desc.DesktopCoordinates.left), UINT(area.bottom - desc.DesktopCoordinates.top), 1};
        output_area = {LONG(box.left), LONG(box.top), LONG(box.right), LONG(box.bottom)};
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = width;
        texture.Height = height;
        texture.ArraySize = texture.MipLevels = texture.SampleDesc.Count = 1;
        texture.Format = surface.ModeDesc.Format;
        texture.Usage = D3D11_USAGE_STAGING;
        texture.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        dx(device->CreateTexture2D(&texture, nullptr, staging.put()), "create preview texture");
    }

    PointerState observe_pointer(const DXGI_OUTDUPL_FRAME_INFO& info, const std::optional<CURSORINFO>& before,
                                 const RECT& area)
    {
        DXGI_OUTDUPL_POINTER_SHAPE_INFO shape{};
        if (info.PointerShapeBufferSize) {
            std::vector<BYTE> bytes(info.PointerShapeBufferSize);
            UINT required{};
            if (FAILED(duplication->GetFramePointerShape(info.PointerShapeBufferSize, bytes.data(),
                                                         &required, &shape))) shape = {};
        }
        const auto initial = pointer.position ? PointerState::unknown : initial_pointer(before, area);
        return pointer.observe(info, shape, output_area, initial);
    }

    Pixels read(IDXGIResource* resource)
    {
        com_ptr<ID3D11Texture2D> texture;
        dx(resource->QueryInterface(IID_PPV_ARGS(texture.put())), "desktop texture");
        context->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, texture.get(), 0, &box);
        Pixels pixels(size_t(width) * height * 4);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        dx(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped), "map preview pixels");
        for (UINT y = 0; y < height; ++y)
            std::memcpy(pixels.data() + size_t(y) * width * 4,
                        static_cast<const BYTE*>(mapped.pData) + size_t(y) * mapped.RowPitch, size_t(width) * 4);
        context->Unmap(staging.get(), 0);
        return pixels;
    }
};

void save_bitmap(const fs::path& path, LONG width, LONG height, const Pixels& pixels)
{
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
    info.biSize = sizeof(info);
    info.biWidth = width;
    info.biHeight = -height;
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biSizeImage = static_cast<DWORD>(pixels.size());
    std::ofstream out(path, std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info), sizeof(info));
    out.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
    out.close();
}

struct Frame {
    DXGI_OUTDUPL_FRAME_INFO info;
    LONGLONG acquired, read;
    int image;
    PointerState pointer;
};

void capture(const Picker& picker, const fs::path& dir, ConsoleQuery* query)
{
    // The window reference must crop exactly the same physical pixels as DXGI.
    const RECT& view = picker.view_rect;
    const RECT& area = picker.area;
    std::ofstream roi(dir / "roi.csv", std::ios::binary);
    roi.exceptions(std::ios::badbit | std::ios::failbit);
    roi << "viewWidth,viewHeight,left,top,width,height\n"
        << view.right - view.left << ',' << view.bottom - view.top << ','
        << area.left - view.left << ',' << area.top - view.top << ','
        << area.right - area.left << ',' << area.bottom - area.top << '\n';
    roi.close();
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    const auto ms = [&](LONGLONG delta) { return delta * 1000.0 / frequency.QuadPart; };
    std::vector<Frame> frames;
    std::vector<Pixels> images;
    std::exception_ptr failure;
    LONGLONG zero = 0;
    try {
        picker.verify_visible();
        const auto initial_cursor = current_cursor();
        Desktop desktop(picker.area);
        const auto start = tick();
        while (!zero || ms(tick() - zero) < 2000) {
            picker.verify_visible();
            com_ptr<IDXGIResource> resource;
            DXGI_OUTDUPL_FRAME_INFO info{};
            const auto status = desktop.duplication->AcquireNextFrame(20, &info, resource.put());
            const auto acquired = tick();
            if (!zero && ms(acquired - start) > 3000) throw Unobservable("no baseline presentation");
            if (status == DXGI_ERROR_WAIT_TIMEOUT) continue;
            dx(status, "AcquireNextFrame");
            picker.verify_visible();
            const auto pointer = desktop.observe_pointer(info, initial_cursor, area);
            int image = -1;
            if (pointer == PointerState::clear && info.LastPresentTime.QuadPart && !info.ProtectedContentMaskedOut) {
                auto pixels = desktop.read(resource.get());
                picker.verify_visible();
                if (images.empty() || images.back() != pixels) {
                    if (images.size() >= (128u * 1024 * 1024) / pixels.size())
                        throw Unobservable("preview capture exceeded memory limit");
                    images.emplace_back(std::move(pixels));
                }
                image = static_cast<int>(images.size()) - 1;
            }
            frames.push_back({info, acquired, tick(), image, pointer});
            resource.reset();
            dx(desktop.duplication->ReleaseFrame(), "ReleaseFrame");
            if (pointer == PointerState::overlap) throw Unobservable("hardware pointer overlaps the preview");
            if (pointer == PointerState::unknown) throw Unobservable("cannot verify pointer visibility or bounds");
            if (!zero && image >= 0) {
                if (!query) { zero = acquired; break; }
                picker.verify_visible();
                zero = query->send();
            }
        }
        picker.verify_visible();
    } catch (...) {
        // Previously guarded frames can still prove residue even if a later
        // screen lock, occlusion, or access loss prevents a passing run.
        failure = std::current_exception();
    }
    for (size_t i = 0; i < images.size(); ++i) {
        wchar_t name[32];
        std::swprintf(name, ARRAYSIZE(name), L"surface-%04zu.bmp", i);
        save_bitmap(dir / name, picker.area.right - picker.area.left, picker.area.bottom - picker.area.top, images[i]);
    }
    std::ofstream csv(dir / "dxgi.csv", std::ios::binary);
    csv.exceptions(std::ios::badbit | std::ios::failbit);
    csv << "index,lastPresentQpc,presentMs,acquiredMs,readMs,accumulatedFrames,mouseQpc,protectedContentMaskedOut,imageIndex,pointerState\n";
    csv << std::fixed << std::setprecision(4);
    for (size_t i = 0; i < frames.size(); ++i) {
        const auto& f = frames[i];
        const auto present = f.info.LastPresentTime.QuadPart;
        csv << i << ',' << present << ',' << (present && zero ? ms(present - zero) : 0.0) << ','
            << (zero ? ms(f.acquired - zero) : 0.0) << ',' << (zero ? ms(f.read - zero) : 0.0) << ','
            << f.info.AccumulatedFrames << ',' << f.info.LastMouseUpdateTime.QuadPart << ','
            << (f.info.ProtectedContentMaskedOut != FALSE) << ',' << f.image << ',' << pointer_name(f.pointer) << '\n';
    }
    csv.close();
    if (failure) std::rethrow_exception(failure);
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    try {
        const bool preflight = argc == 2 && std::wcscmp(argv[1], L"--preflight") == 0;
        if (!preflight && argc != 5 && argc != 7)
            throw std::runtime_error("usage: desktop_capture --preflight | rootHwnd viewHwnd gui.exe emptyDirectory [fzfPid query]");
        if (!SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            throw Unobservable("cannot determine physical preview coordinates");
        require_active_desktop();
        if (preflight) {
            // Before a picker exists, probe the primary output at its origin.
            // Capture initializes DXGI again for the output containing the actual View.
            const Desktop desktop(RECT{0, 0, 1, 1});
            require_active_desktop();
            std::cout << "Desktop preflight passed: active input desktop and primary DXGI output\n";
            return 0;
        }
        const fs::path dir = argv[4];
        if (!fs::is_directory(dir) || !fs::is_empty(dir)) throw std::runtime_error("expected empty evidence directory");
        Picker picker(reinterpret_cast<HWND>(number(argv[1])), reinterpret_cast<HWND>(number(argv[2])), argv[3]);
        std::optional<ConsoleQuery> query;
        if (argc == 7) {
            auto pid = number(argv[5]);
            if (pid > MAXDWORD) throw std::runtime_error("invalid fzf PID");
            query.emplace(static_cast<DWORD>(pid), argv[6]);
        }
        capture(picker, dir, query ? &*query : nullptr);
        return 0;
    } catch (const Unobservable& e) {
        std::cerr << "Desktop not verified: " << e.what() << '\n';
        return 77;
    } catch (const std::exception& e) {
        std::cerr << "Desktop capture failed: " << e.what() << '\n';
        return 1;
    }
}
