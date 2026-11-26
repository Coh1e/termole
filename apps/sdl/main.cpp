#include "platform/posix_session.h"
#include "ipc/session_ipc.h"
#include "core/input.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {
using termole::platform::PosixFrame;
using termole::platform::PosixSession;
using termole::ipc::RemoteSession;

constexpr int font_pixels = 18;
constexpr uint32_t normal_foreground = 0xe4e7ec;
constexpr uint32_t normal_background = 0x171b22;

using Texture = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>;
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

struct Glyph {
    Texture texture{ nullptr, SDL_DestroyTexture };
    int left = 0;
    int top = 0;
};

class Font {
public:
    ~Font()
    {
        if (face_) FT_Done_Face(face_);
        if (library_) FT_Done_FreeType(library_);
    }

    bool open()
    {
        if (FT_Init_FreeType(&library_)) return false;
        if (const char* path = std::getenv("TERMOLE_FONT"); path && *path) {
            if (FT_New_Face(library_, path, 0, &face_)) return false;
        } else {
#ifdef __APPLE__
            constexpr const char* fallback = "/System/Library/Fonts/Menlo.ttc";
#else
            constexpr const char* fallback = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf";
#endif
            if (FT_New_Face(library_, fallback, 0, &face_)) return false;
        }
        if (FT_Set_Pixel_Sizes(face_, 0, font_pixels)) return false;
        width = std::max(1L, (face_->size->metrics.max_advance + 63) / 64);
        height = std::max(1L, (face_->size->metrics.height + 63) / 64);
        ascender = static_cast<int>(face_->size->metrics.ascender / 64);
        return true;
    }

    const Glyph& glyph(SDL_Renderer* renderer, uint32_t codepoint)
    {
        const auto existing = glyphs_.find(codepoint);
        if (existing != glyphs_.end()) return existing->second;
        if (glyphs_.size() >= 512) glyphs_.clear();
        auto& glyph = glyphs_[codepoint];
        if (FT_Load_Char(face_, codepoint, FT_LOAD_RENDER)) return glyph;
        const auto slot = face_->glyph;
        const auto& bitmap = slot->bitmap;
        if (!bitmap.width || !bitmap.rows || bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
            return glyph;
        Surface surface(SDL_CreateSurface(static_cast<int>(bitmap.width),
                                          static_cast<int>(bitmap.rows),
                                          SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        if (!surface) return glyph;
        for (unsigned row = 0; row < bitmap.rows; ++row) {
            auto* pixels = static_cast<uint8_t*>(surface->pixels) + row * surface->pitch;
            const auto* alpha = bitmap.buffer + static_cast<ptrdiff_t>(row) * bitmap.pitch;
            for (unsigned column = 0; column < bitmap.width; ++column) {
                *pixels++ = 255;
                *pixels++ = 255;
                *pixels++ = 255;
                *pixels++ = alpha[column];
            }
        }
        glyph.texture.reset(SDL_CreateTextureFromSurface(renderer, surface.get()));
        if (glyph.texture) SDL_SetTextureBlendMode(glyph.texture.get(), SDL_BLENDMODE_BLEND);
        glyph.left = slot->bitmap_left;
        glyph.top = slot->bitmap_top;
        return glyph;
    }

    int width = 1;
    int height = 1;
    int ascender = 1;

private:
    FT_Library library_ = nullptr;
    FT_Face face_ = nullptr;
    std::unordered_map<uint32_t, Glyph> glyphs_;
};

void draw_color(SDL_Renderer* renderer, uint32_t color)
{
    SDL_SetRenderDrawColor(renderer, (color >> 16) & 255, (color >> 8) & 255, color & 255, 255);
}

void draw(SDL_Renderer* renderer, Font& font, const PosixFrame& frame, bool focused,
          const termole::FrameSelection& selection)
{
    draw_color(renderer, normal_background);
    SDL_RenderClear(renderer);
    const auto selected = selection.range(frame.cells.size());
    for (uint16_t row = 0; row < frame.rows; ++row) {
        for (uint16_t column = 0; column < frame.columns; ++column) {
            const size_t index = static_cast<size_t>(row) * frame.columns + column;
            const auto& cell = frame.cells[index];
            uint32_t fg = cell.foreground_is_default ? normal_foreground : cell.foreground;
            uint32_t bg = cell.background_is_default ? normal_background : cell.background;
            const bool cursor = focused && frame.cursor_visible && row == frame.cursor_row &&
                column == frame.cursor_column;
            if (static_cast<bool>(cell.attributes & TERMOLE_CELL_INVERSE) != cursor)
                std::swap(fg, bg);
            if (selected && index >= selected->first && index <= selected->second) bg = 0x264f78;
            const float x = static_cast<float>(column * font.width);
            const float y = static_cast<float>(row * font.height);
            const SDL_FRect box{ x, y, static_cast<float>(font.width), static_cast<float>(font.height) };
            if (bg != normal_background) {
                draw_color(renderer, bg);
                SDL_RenderFillRect(renderer, &box);
            }
            if (cell.continuation || (cell.attributes & TERMOLE_CELL_INVISIBLE)) continue;
            for (uint8_t index = 0; index < cell.codepoint_count; ++index) {
                const auto& glyph = font.glyph(renderer, cell.codepoints[index]);
                if (!glyph.texture) continue;
                const SDL_FRect dest{ x + glyph.left, y + font.ascender - glyph.top,
                                      static_cast<float>(glyph.texture->w),
                                      static_cast<float>(glyph.texture->h) };
                SDL_SetTextureColorMod(glyph.texture.get(), (fg >> 16) & 255, (fg >> 8) & 255, fg & 255);
                SDL_RenderTexture(renderer, glyph.texture.get(), nullptr, &dest);
            }
            if (cell.attributes & TERMOLE_CELL_UNDERLINE) {
                draw_color(renderer, fg);
                SDL_RenderLine(renderer, x, y + font.height - 2, x + font.width, y + font.height - 2);
            }
        }
    }
}

template<class Session>
void send_key(Session& session, const SDL_KeyboardEvent& key)
{
    std::string error;
    termole_key logical;
    switch (key.key) {
    case SDLK_RETURN: case SDLK_KP_ENTER: logical = TERMOLE_KEY_ENTER; break;
    case SDLK_ESCAPE: logical = TERMOLE_KEY_ESCAPE; break;
    case SDLK_TAB:
        if (key.mod & SDL_KMOD_SHIFT) {
            (void)session.write("\x1b[Z", error);
            return;
        }
        logical = TERMOLE_KEY_TAB;
        break;
    case SDLK_BACKSPACE: logical = TERMOLE_KEY_BACKSPACE; break;
    case SDLK_UP: logical = TERMOLE_KEY_ARROW_UP; break;
    case SDLK_DOWN: logical = TERMOLE_KEY_ARROW_DOWN; break;
    case SDLK_RIGHT: logical = TERMOLE_KEY_ARROW_RIGHT; break;
    case SDLK_LEFT: logical = TERMOLE_KEY_ARROW_LEFT; break;
    case SDLK_HOME: logical = TERMOLE_KEY_HOME; break;
    case SDLK_END: logical = TERMOLE_KEY_END; break;
    case SDLK_INSERT: logical = TERMOLE_KEY_INSERT; break;
    case SDLK_DELETE: logical = TERMOLE_KEY_DELETE; break;
    case SDLK_PAGEUP: logical = TERMOLE_KEY_PAGE_UP; break;
    case SDLK_PAGEDOWN: logical = TERMOLE_KEY_PAGE_DOWN; break;
    default:
        if ((key.mod & SDL_KMOD_CTRL) && key.key >= SDLK_A && key.key <= SDLK_Z) {
            const char control = static_cast<char>(key.key - SDLK_A + 1);
            (void)session.write({ &control, 1 }, error);
        }
        return;
    }
    (void)session.write(termole_key_sequence(logical, session.modes().application_cursor), error);
}

bool save_probe(SDL_Renderer* renderer, const char* path)
{
    Surface surface(SDL_RenderReadPixels(renderer, nullptr), SDL_DestroySurface);
    if (!surface) return false;
    Surface rgb(SDL_ConvertSurface(surface.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    if (!rgb) return false;
    size_t highlighted = 0;
    size_t glyph_pixels = 0;
    for (int y = 0; y < rgb->h; ++y) {
        const auto* pixel = static_cast<const uint8_t*>(rgb->pixels) + y * rgb->pitch;
        for (int x = 0; x < rgb->w; ++x, pixel += 4) {
            if (pixel[0] > 160 && pixel[1] < 90 && pixel[2] < 90) ++highlighted;
            if (pixel[0] > 190 && pixel[1] > 190 && pixel[2] > 190) ++glyph_pixels;
        }
    }
    if (highlighted < 100 || glyph_pixels < 100) return false;
    return SDL_SaveBMP(surface.get(), path);
}

termole::platform::PosixSessionOptions shell_options()
{
    termole::platform::PosixSessionOptions options;
    const char* shell = std::getenv("SHELL");
    options.arguments = { shell && *shell ? shell : "/bin/sh" };
    return options;
}

template<class Session>
int show_window(Session& terminal, const char* probe_path)
{
    const bool probe = probe_path != nullptr;
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("Termole", 880, 528, SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
    if (!window) return 1;
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateRenderer(window.get(), probe ? "software" : nullptr), SDL_DestroyRenderer);
    if (!renderer) return 1;
    Font font;
    if (!font.open()) {
        std::fputs("Could not open a terminal font. Set TERMOLE_FONT to a TrueType font.\n", stderr);
        return 1;
    }

    const auto resize = [&] {
        int width = 0;
        int height = 0;
        if (!SDL_GetRenderOutputSize(renderer.get(), &width, &height)) return;
        const auto columns = static_cast<uint16_t>(std::clamp(width / font.width, 1, 1000));
        const auto rows = static_cast<uint16_t>(std::clamp(height / font.height, 1, 1000));
        std::string error;
        (void)terminal.resize(columns, rows, error);
    };

    std::string error;
    resize();
    SDL_StartTextInput(window.get());
    std::optional<PosixFrame> frame;
    termole::FrameSelection selection;
    const auto cell_at = [&](float x, float y) {
        const int column = std::clamp(static_cast<int>(x / font.width), 0, frame->columns - 1);
        const int row = std::clamp(static_cast<int>(y / font.height), 0, frame->rows - 1);
        return static_cast<size_t>(row) * frame->columns + column;
    };
    bool redraw = true;
    bool focused = true;
    const uint64_t probe_deadline = SDL_GetTicks() + 4000;
    for (;;) {
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, 16)) {
            do {
                switch (event.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED: return 0;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: resize(); [[fallthrough]];
                case SDL_EVENT_WINDOW_EXPOSED: redraw = true; break;
                case SDL_EVENT_WINDOW_FOCUS_GAINED: focused = true; redraw = true; break;
                case SDL_EVENT_WINDOW_FOCUS_LOST: focused = false; redraw = true; break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (frame && event.button.button == SDL_BUTTON_LEFT) {
                        selection.begin(cell_at(event.button.x, event.button.y));
                        redraw = true;
                    }
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    if (frame && selection.dragging) {
                        selection.focus = cell_at(event.motion.x, event.motion.y);
                        redraw = true;
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (frame && event.button.button == SDL_BUTTON_LEFT && selection.dragging) {
                        selection.focus = cell_at(event.button.x, event.button.y);
                        selection.dragging = false;
                        redraw = true;
                    }
                    break;
                case SDL_EVENT_TEXT_INPUT:
                    (void)terminal.write(event.text.text, error);
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_C && frame && selection.anchor &&
                        (event.key.mod & SDL_KMOD_CTRL) &&
                        (event.key.mod & SDL_KMOD_SHIFT)) {
                        SDL_SetClipboardText(selection.text(*frame).c_str());
                    } else if (event.key.key == SDLK_V && (event.key.mod & SDL_KMOD_CTRL) &&
                               (event.key.mod & SDL_KMOD_SHIFT)) {
                        std::unique_ptr<char, decltype(&SDL_free)> text(SDL_GetClipboardText(), SDL_free);
                        if (text && *text) {
                            std::string bytes(text.get());
                            if (terminal.modes().bracketed_paste) termole::bracket_paste(bytes);
                            (void)terminal.write(bytes, error);
                        }
                    } else {
                        send_key(terminal, event.key);
                    }
                    break;
                default: break;
                }
            } while (SDL_PollEvent(&event));
        }
        if (auto next = terminal.frame_after(frame ? frame->revision : 0)) {
            frame = std::move(next);
            redraw = true;
        }
        if (redraw && frame) {
            draw(renderer.get(), font, *frame, focused, selection);
            if (probe && save_probe(renderer.get(), probe_path)) return 0;
            SDL_RenderPresent(renderer.get());
            redraw = false;
        }
        if (probe && SDL_GetTicks() >= probe_deadline) return 1;
        if (!terminal.running()) return probe ? 1 : terminal.wait();
    }
}

int usage()
{
    std::fputs("usage: termole [-- program [arguments...]]\n"
               "       termole --server [--pipe NAME] [-- program [arguments...]]\n"
               "       termole --client [--pipe NAME] [--detach]\n", stderr);
    return 2;
}

int run(int argc, char** argv)
{
    std::string error;
    if (argc > 1 && std::string_view(argv[1]) == "--client") {
        std::string_view name = "default";
        bool detach = false;
        for (int index = 2; index < argc; ++index) {
            const std::string_view token(argv[index]);
            if (token == "--pipe" && index + 1 < argc) name = argv[++index];
            else if (token == "--detach") detach = true;
            else return usage();
        }
        auto terminal = RemoteSession::connect(name, detach, error);
        if (terminal) return show_window(*terminal, nullptr);
    } else {
        auto options = shell_options();
        const char* probe = nullptr;
        if (argc == 3 && std::string_view(argv[1]) == "--test-render") {
            probe = argv[2];
            options.arguments = { "/bin/sh", "-c",
                "printf '\\033[48;2;220;30;30mSDL PIXEL PROBE\\033[0m'; sleep 2" };
        } else if (argc > 2 && std::string_view(argv[1]) == "--") {
            options.arguments.assign(argv + 2, argv + argc);
        } else if (argc != 1) {
            return usage();
        }
        auto terminal = PosixSession::create(options, error);
        if (terminal) return show_window(*terminal, probe);
    }
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc > 1 && std::string_view(argv[1]) == "--server") {
        auto options = shell_options();
        std::string_view name = "default";
        for (int index = 2; index < argc; ++index) {
            const std::string_view token(argv[index]);
            if (token == "--pipe" && index + 1 < argc) name = argv[++index];
            else if (token == "--" && index + 1 < argc) {
                options.arguments.assign(argv + index + 1, argv + argc);
                break;
            } else {
                return usage();
            }
        }
        return termole::ipc::run_server(name, options);
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    const int result = run(argc, argv);
    if (result != 0 && *SDL_GetError()) std::fprintf(stderr, "%s\n", SDL_GetError());
    SDL_Quit();
    return result;
}
