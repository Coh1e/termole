#include "session/session.h"
#include "core/input.h"
#include "core/utf8_encode.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace termole {
namespace {

void append_utf8(std::string& out, char32_t codepoint)
{
    char bytes[4];
    out.append(bytes, termole_utf8_encode(codepoint, bytes));
}

Cell cell_from_c(const termole_cell& in)
{
    Cell out;
    out.character = in.codepoint_count > 0 ? static_cast<char32_t>(in.codepoints[0]) : U' ';
    out.width = in.width;
    out.continuation = in.continuation != 0;
    for (uint8_t index = 1; index < in.codepoint_count; ++index)
        out.combining.push_back(static_cast<char32_t>(in.codepoints[index]));
    out.foreground = in.foreground_is_default ? kColorDefault : in.foreground;
    out.background = in.background_is_default ? kColorDefault : in.background;
    out.attributes = in.attributes;
    return out;
}

std::string line_text_from(const Cell* cells, size_t count)
{
    std::string line;
    for (size_t index = 0; index < count; ++index) {
        const Cell& cell = cells[index];
        if (cell.continuation) continue;
        append_utf8(line, cell.character == 0 ? U' ' : cell.character);
        for (const char32_t combining : cell.combining) append_utf8(line, combining);
    }
    while (!line.empty() && line.back() == ' ') line.pop_back();
    return line;
}

std::wstring utf8_to_wide_local(std::string_view text)
{
    if (text.empty()) return {};
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                             static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                          result.data(), length);
    return result;
}

} // namespace

void NativeSession::on_write_pty(void* userdata, const void* data, size_t size)
{
    auto* session = static_cast<NativeSession*>(userdata);
    if (session && data && size) {
        (void)session->write_raw(std::string_view(static_cast<const char*>(data), size));
    }
}

void NativeSession::on_bell(void* userdata)
{
    auto* session = static_cast<NativeSession*>(userdata);
    if (session) session->push_event(TERMOLE_EVENT_BELL);
}

void NativeSession::on_title_changed(void* userdata)
{
    auto* session = static_cast<NativeSession*>(userdata);
    if (!session) return;
    char buffer[512];
    size_t length = 0;
    if (termole_vt_title(session->vt_, buffer, sizeof(buffer), &length) == TERMOLE_CORE_OK) {
        session->push_event(TERMOLE_EVENT_TITLE_CHANGED, 0, std::string(buffer, length));
    }
}

NativeSession::NativeSession(uint16_t columns, uint16_t rows)
{
    if (termole_vt_create(columns, rows, &vt_) != TERMOLE_CORE_OK) {
        vt_ = nullptr;
        return;
    }
    const termole_vt_callbacks callbacks{ this, &NativeSession::on_write_pty,
                                          &NativeSession::on_bell,
                                          &NativeSession::on_title_changed };
    (void)termole_vt_set_callbacks(vt_, &callbacks);
}

NativeSession::~NativeSession()
{
    try {
        (void)terminate();
    } catch (...) {
        // A destructor reached through the C ABI must never throw.
    }
    termole_vt_free(vt_);
    vt_ = nullptr;
}

Result<std::shared_ptr<NativeSession>> NativeSession::create(const SessionOptions& options)
{
    if (options.columns != 0 &&
        (options.columns < kMinColumns || options.columns > kMaxColumns)) {
        return std::unexpected(make_error(TERMOLE_STATUS_INVALID_ARGUMENT,
                                          "columns must be between 1 and 1000"));
    }
    if (options.rows != 0 && (options.rows < kMinRows || options.rows > kMaxRows)) {
        return std::unexpected(
            make_error(TERMOLE_STATUS_INVALID_ARGUMENT, "rows must be between 1 and 1000"));
    }

    const uint16_t columns = options.columns == 0 ? 80 : options.columns;
    const uint16_t rows = options.rows == 0 ? 24 : options.rows;

    auto session = std::shared_ptr<NativeSession>(new NativeSession(columns, rows));
    session->data_event_.reset(::CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!session->data_event_) {
        return std::unexpected(win::system_error("CreateEventW", ::GetLastError()));
    }
    auto pty = platform::WindowsPty::spawn_native(options);
    if (!pty) return std::unexpected(std::move(pty.error()));
    session->pty_ = std::move(*pty);

    // The reader holds a raw pointer, not a shared_ptr: the NativeSession's
    // destructor joins this thread, and a shared_ptr cycle here would keep the
    // session (and its process tree) alive forever after termole_session_destroy.
    session->reader_ = std::thread([pointer = session.get()] { pointer->read_loop(); });
    session->exit_thread_ = std::thread([pointer = session.get()] { pointer->exit_loop(); });
    return session;
}

void NativeSession::read_loop()
{
    std::array<char, 8192> buffer{};
    for (;;) {
        const auto read = pty_->read_output(buffer.data(), buffer.size());
        if (!read) {
            push_event(TERMOLE_EVENT_ERROR, 0, read.error().message);
            break;
        }
        if (*read == 0) break;
        {
            std::lock_guard lock(mutex_);
            if (vt_) (void)termole_vt_feed(vt_, buffer.data(), *read);
            last_output_tick_ = ::GetTickCount64();
            ++revision_;
        }
        if (data_event_) ::SetEvent(data_event_.get());
    }

    const int32_t process_exit_code = pty_->wait_for_exit();
    exit_code_.store(process_exit_code, std::memory_order_release);
    running_.store(false, std::memory_order_release);
    push_event(TERMOLE_EVENT_EXITED, static_cast<int32_t>(process_exit_code));
    if (data_event_) ::SetEvent(data_event_.get());
}

void NativeSession::exit_loop()
{
    (void)pty_->wait_for_exit();
    // Closing the pseudoconsole lets the reader drain the final bytes before
    // the session reports completion.
    pty_->close_console();
}

void NativeSession::push_event(termole_event_type type, int32_t exit_code, std::string text)
{
    constexpr size_t kMaxEventQueue = 256;
    std::lock_guard lock(event_mutex_);

    if (type == TERMOLE_EVENT_TITLE_CHANGED && !events_.empty() &&
        events_.back().type == TERMOLE_EVENT_TITLE_CHANGED) {
        events_.back().text = std::move(text);
    } else {
        if (events_.size() >= kMaxEventQueue) {
            const auto disposable = std::find_if(
                events_.begin(), events_.end(), [](const EventRecord& event) {
                    return event.type != TERMOLE_EVENT_EXITED && event.type != TERMOLE_EVENT_ERROR;
                });
            if (disposable != events_.end()) events_.erase(disposable);
            else events_.pop_front();
        }
        events_.push_back(EventRecord{ type, exit_code, std::move(text) });
    }
    if (data_event_) ::SetEvent(data_event_.get());
}

bool NativeSession::poll_event(EventRecord& out)
{
    std::lock_guard lock(event_mutex_);
    if (events_.empty()) return false;
    out = std::move(events_.front());
    events_.pop_front();
    return true;
}

Result<int32_t> NativeSession::exit_code() const
{
    if (running()) {
        return std::unexpected(
            make_error(TERMOLE_STATUS_INVALID_STATE, "session is still running"));
    }
    return exit_code_.load(std::memory_order_acquire);
}

std::string NativeSession::scrollback_text() const
{
    std::lock_guard lock(mutex_);
    if (!vt_) return {};
    const termole_frame* frame = nullptr;
    if (termole_vt_frame(vt_, &frame) != TERMOLE_CORE_OK || !frame) return {};
    uint32_t history = 0;
    (void)termole_vt_history_rows(vt_, &history);
    std::string result;
    for (uint32_t row = 0; row < history; ++row) {
        if (row != 0) result.push_back('\n');
        std::vector<Cell> cells(static_cast<size_t>(frame->columns));
        for (int column = 0; column < frame->columns; ++column) {
            termole_cell cell;
            if (termole_vt_cell(vt_, static_cast<int>(row), column, &cell) == TERMOLE_CORE_OK)
                cells[static_cast<size_t>(column)] = cell_from_c(cell);
        }
        result += line_text_from(cells.data(), cells.size());
    }
    return result;
}

size_t NativeSession::scrollback_line_count() const
{
    std::lock_guard lock(mutex_);
    if (!vt_) return 0;
    uint32_t history = 0;
    if (termole_vt_history_rows(vt_, &history) != TERMOLE_CORE_OK) return 0;
    return history;
}

Result<void> NativeSession::terminate()
{
    std::lock_guard terminate_lock(terminate_mutex_);
    if (stopping_.exchange(true)) return {};

    const bool was_running = running_.exchange(false, std::memory_order_acq_rel);
    if (!pty_) return {};
    auto terminated = pty_->terminate_process(was_running);
    pty_->close_input();
    // Keep the reader draining while the pseudoconsole closes.
    pty_->close_console();

    try {
        if (reader_.joinable()) reader_.join();
        if (exit_thread_.joinable()) exit_thread_.join();
    } catch (const std::exception& exception) {
        if (terminated)
            return std::unexpected(make_error(TERMOLE_STATUS_SYSTEM_ERROR, exception.what()));
    }

    return terminated;
}

Result<void> NativeSession::write(std::string_view bytes)
{
    return write_raw(bytes);
}

Result<void> NativeSession::write_raw(std::string_view bytes)
{
    if (bytes.empty()) return {};
    if (stopping_.load(std::memory_order_acquire)) {
        return std::unexpected(make_error(TERMOLE_STATUS_INVALID_STATE, "session input is closed"));
    }
    return pty_->write_input(bytes);
}

Result<void> NativeSession::send_key(termole_key key)
{
    const auto state = modes();
    if (!state) return std::unexpected(make_error(TERMOLE_STATUS_INVALID_STATE, "terminal modes unavailable"));
    const char* bytes = termole_key_sequence(key, state->application_cursor);
    if (!bytes) return std::unexpected(make_error(TERMOLE_STATUS_INVALID_ARGUMENT, "unknown key"));
    return write_raw(bytes);
}

Result<void> NativeSession::resize(uint16_t columns, uint16_t rows)
{
    if (columns < kMinColumns || columns > kMaxColumns || rows < kMinRows || rows > kMaxRows) {
        return std::unexpected(make_error(TERMOLE_STATUS_INVALID_ARGUMENT,
                                          "columns and rows must be between 1 and 1000"));
    }

    std::lock_guard terminate_lock(terminate_mutex_);
    if (stopping_.load(std::memory_order_acquire)) {
        return std::unexpected(
            make_error(TERMOLE_STATUS_INVALID_STATE, "session is already terminated"));
    }

    {
        std::lock_guard lock(mutex_);
        if (vt_) (void)termole_vt_resize(vt_, columns, rows);
        ++revision_;
    }
    auto resized = pty_->resize_console(columns, rows);
    if (!resized) return resized;
    if (data_event_) ::SetEvent(data_event_.get());
    return {};
}

bool NativeSession::attach_view() noexcept
{
    bool expected = false;
    return view_attached_.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
}

void NativeSession::detach_view() noexcept
{
    view_attached_.store(false, std::memory_order_release);
}

namespace {

// The Session mutex protects the borrowed active frame and its scrollback.
struct BorrowedViewport {
    termole_frame frame;
    int first_document_row;
    int history_rows_in_frame;

    template<class CellType, class Convert>
    void copy_cells(termole_vt* vt, CellType* dest, Convert convert) const
    {
        for (int row = 0; row < history_rows_in_frame; ++row) {
            for (int column = 0; column < frame.columns; ++column) {
                termole_cell cell{};
                if (termole_vt_cell(vt, first_document_row + row, column, &cell) != TERMOLE_CORE_OK) {
                    // Ghostty omits trailing blanks on some scrollback rows.
                    cell.width = 1;
                    cell.foreground_is_default = cell.background_is_default = 1;
                }
                *dest++ = convert(cell);
            }
        }
        const size_t live_cells =
            static_cast<size_t>(frame.rows - history_rows_in_frame) * frame.columns;
        std::transform(frame.cells, frame.cells + live_cells, dest, convert);
    }
};

std::optional<BorrowedViewport> borrow_viewport(termole_vt* vt, int scroll_offset)
{
    const termole_frame* frame = nullptr;
    uint32_t history = 0;
    if (termole_vt_frame(vt, &frame) != TERMOLE_CORE_OK ||
        termole_vt_history_rows(vt, &history) != TERMOLE_CORE_OK) return std::nullopt;
    const int offset = std::clamp(scroll_offset, 0, static_cast<int>(history));
    BorrowedViewport viewport{ *frame, static_cast<int>(history) - offset,
                               std::min<int>(offset, frame->rows) };
    if (offset) viewport.frame.cursor_visible = 0;
    return viewport;
}

} // namespace

Snapshot NativeSession::snapshot(int scroll_offset) const
{
    std::lock_guard lock(mutex_);
    Snapshot result;
    const auto viewport = borrow_viewport(vt_, scroll_offset);
    if (!viewport) return result;
    const auto& frame = viewport->frame;
    result.columns = frame.columns;
    result.rows = frame.rows;
    result.cells.resize(static_cast<size_t>(frame.columns) * frame.rows);
    viewport->copy_cells(vt_, result.cells.data(), cell_from_c);
    result.cursor_column = frame.cursor_column;
    result.cursor_row = frame.cursor_row;
    result.cursor_visible = frame.cursor_visible != 0;
    termole_modes modes{};
    result.first_document_row = viewport->first_document_row;
    result.live_alternate_screen = viewport->history_rows_in_frame == 0 &&
        termole_vt_modes(vt_, &modes) == TERMOLE_CORE_OK && modes.alternate_screen;
    result.last_output_tick = last_output_tick_;
    return result;
}

termole_status NativeSession::read_viewport(int scroll_offset, termole_viewport& out) const
{
    std::lock_guard lock(mutex_);
    const auto viewport = borrow_viewport(vt_, scroll_offset);
    if (!viewport) return TERMOLE_STATUS_INVALID_STATE;
    const auto& frame = viewport->frame;
    const size_t count = static_cast<size_t>(frame.columns) * frame.rows;
    auto* cells = out.frame.cells;
    if (count > out.capacity) {
        cells = static_cast<termole_cell*>(std::realloc(cells, count * sizeof(*cells)));
        if (!cells) return TERMOLE_STATUS_OUT_OF_MEMORY;
        out.capacity = count;
    }
    viewport->copy_cells(vt_, cells, [](const termole_cell& cell) { return cell; });
    out.frame = frame;
    out.frame.cells = cells;
    out.first_document_row = viewport->first_document_row;
    return TERMOLE_STATUS_OK;
}

std::optional<SessionFrame> NativeSession::frame_after(uint64_t revision) const
{
    std::lock_guard lock(mutex_);
    if (!vt_ || revision_ == revision) return std::nullopt;
    const termole_frame* frame = nullptr;
    if (termole_vt_frame(vt_, &frame) != TERMOLE_CORE_OK || !frame) return std::nullopt;
    const size_t count = static_cast<size_t>(frame->columns) * frame->rows;
    return SessionFrame{ revision_, frame->columns, frame->rows, frame->cursor_column,
                         frame->cursor_row, frame->cursor_visible != 0,
                         { frame->cells, frame->cells + count } };
}

std::optional<termole_modes> NativeSession::modes() const
{
    std::lock_guard lock(mutex_);
    termole_modes result{};
    if (termole_vt_modes(vt_, &result) != TERMOLE_CORE_OK) return std::nullopt;
    return result;
}

std::string NativeSession::selection_text(int start_row, int start_column, int end_row,
                                          int end_column) const
{
    std::lock_guard lock(mutex_);
    if (!vt_) return {};
    const termole_frame* frame = nullptr;
    if (termole_vt_frame(vt_, &frame) != TERMOLE_CORE_OK || !frame) return {};
    uint32_t history = 0;
    (void)termole_vt_history_rows(vt_, &history);
    const int total_rows = static_cast<int>(history) + frame->rows;
    int first_row = std::clamp(start_row, 0, total_rows - 1);
    int first_column = std::clamp(start_column, 0, frame->columns - 1);
    int last_row = std::clamp(end_row, 0, total_rows - 1);
    int last_column = std::clamp(end_column, 0, frame->columns - 1);
    if (first_row > last_row || (first_row == last_row && first_column > last_column)) {
        std::swap(first_row, last_row);
        std::swap(first_column, last_column);
    }
    std::string result;
    for (int row = first_row; row <= last_row; ++row) {
        const int begin = row == first_row ? first_column : 0;
        const int end = row == last_row ? last_column : frame->columns - 1;
        for (int column = begin; column <= end; ++column) {
            termole_cell cell;
            if (termole_vt_cell(vt_, row, column, &cell) != TERMOLE_CORE_OK) {
                // Ghostty can omit cells beyond the stored end of a history
                // row. Selection still includes those columns as spaces.
                result.push_back(' ');
                continue;
            }
            if (cell.continuation) continue;
            const Cell converted = cell_from_c(cell);
            append_utf8(result, converted.character == 0 ? U' ' : converted.character);
            for (const char32_t combining : converted.combining) append_utf8(result, combining);
        }
        if (row != last_row) result.push_back('\n');
    }
    return result;
}

std::pair<uint16_t, uint16_t> NativeSession::size() const
{
    std::lock_guard lock(mutex_);
    if (!vt_) return { 0, 0 };
    const termole_frame* frame = nullptr;
    if (termole_vt_frame(vt_, &frame) != TERMOLE_CORE_OK || !frame) return { 0, 0 };
    return { frame->columns, frame->rows };
}

std::string NativeSession::screen_text() const
{
    std::lock_guard lock(mutex_);
    if (!vt_) return {};
    const termole_frame* frame = nullptr;
    if (termole_vt_frame(vt_, &frame) != TERMOLE_CORE_OK || !frame) return {};
    uint32_t history = 0;
    (void)termole_vt_history_rows(vt_, &history);
    std::vector<std::string> lines;
    int last_content = -1;
    for (int row = 0; row < frame->rows; ++row) {
        std::vector<Cell> cells(static_cast<size_t>(frame->columns));
        for (int column = 0; column < frame->columns; ++column) {
            termole_cell cell;
            if (termole_vt_cell(vt_, static_cast<int>(history) + row, column, &cell) == TERMOLE_CORE_OK)
                cells[static_cast<size_t>(column)] = cell_from_c(cell);
        }
        std::string line = line_text_from(cells.data(), cells.size());
        if (!line.empty()) last_content = row;
        lines.push_back(std::move(line));
    }
    std::string result;
    for (int row = 0; row <= last_content; ++row) {
        result += lines[static_cast<size_t>(row)];
        if (row != last_content) result.push_back('\n');
    }
    return result;
}

std::wstring NativeSession::title() const
{
    std::lock_guard lock(mutex_);
    if (!vt_) return {};
    size_t needed = 0;
    if (termole_vt_title(vt_, nullptr, 0, &needed) != TERMOLE_CORE_OUT_OF_SPACE) return {};
    std::vector<char> buffer(needed + 1);
    if (termole_vt_title(vt_, buffer.data(), buffer.size(), &needed) != TERMOLE_CORE_OK) return {};
    return utf8_to_wide_local(std::string_view(buffer.data(), needed));
}

uint8_t NativeSession::mouse_mode() const
{
    return modes().value_or(termole_modes{}).mouse_tracking;
}

bool NativeSession::sgr_mouse() const
{
    return modes().value_or(termole_modes{}).mouse_sgr != 0;
}

bool NativeSession::bracketed_paste() const
{
    return modes().value_or(termole_modes{}).bracketed_paste != 0;
}

} // namespace termole
