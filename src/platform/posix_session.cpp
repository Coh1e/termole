#include "platform/posix_session.h"

#include <cerrno>
#include <utility>

namespace termole::platform {

std::unique_ptr<PosixSession> PosixSession::create(const PosixSessionOptions& options,
                                                    std::string& error)
{
    std::unique_ptr<PosixSession> session(new PosixSession());
    session->pty_ = Pty::spawn(
        PtyOptions{ options.arguments, options.working_directory, options.columns, options.rows },
        error);
    if (!session->pty_) return nullptr;
    if (termole_vt_create(options.columns, options.rows, &session->vt_) != TERMOLE_CORE_OK) {
        error = "could not create the terminal core";
        session->stop();
        return nullptr;
    }
    const termole_vt_callbacks callbacks{
        session.get(),
        [](void* userdata, const void* data, size_t size) noexcept {
            auto& session = *static_cast<PosixSession*>(userdata);
            try {
                session.replies_.append(static_cast<const char*>(data), size);
            } catch (...) {
                // The C callback cannot report allocation failure.
                session.pty_->close();
            }
        },
        nullptr, nullptr
    };
    (void)termole_vt_set_callbacks(session->vt_, &callbacks);
    session->reader_ = std::thread([raw = session.get()] { raw->read_loop(); });
    return session;
}

PosixSession::~PosixSession()
{
    stop();
    if (vt_) {
        termole_vt_free(vt_);
        vt_ = nullptr;
    }
}

void PosixSession::read_loop()
{
    char buffer[4096];
    for (;;) {
        const ssize_t read = pty_ ? pty_->read(buffer, sizeof(buffer)) : 0;
        if (read <= 0) break;
        std::unique_lock lock(mutex_);
        if (vt_) (void)termole_vt_feed(vt_, buffer, static_cast<size_t>(read));
        ++revision_;
        flush_replies(std::move(lock));
    }
    const int code = pty_ ? pty_->wait() : -1;
    std::lock_guard lock(mutex_);
    exit_code_ = code;
    stopped_ = true;
}

void PosixSession::flush_replies(std::unique_lock<std::mutex> state_lock)
{
    if (replies_.empty()) return;
    // Collect feed and resize replies in VT order. A slow PTY writer must
    // never hold the VT lock needed by frame readers.
    state_lock.unlock();
    std::lock_guard input_lock(input_mutex_);
    state_lock.lock();
    std::string bytes;
    bytes.swap(replies_);
    state_lock.unlock();
    (void)write_all(bytes);
}

bool PosixSession::write_all(std::string_view bytes)
{
    while (!bytes.empty()) {
        errno = 0; // A closed Pty can return -1 without issuing a system call.
        const ssize_t written = pty_->write(bytes.data(), bytes.size());
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        bytes.remove_prefix(static_cast<size_t>(written));
    }
    return true;
}

bool PosixSession::write(std::string_view bytes, std::string& error)
{
    std::lock_guard lock(input_mutex_);
    if (!write_all(bytes)) {
        error = "PTY write failed";
        return false;
    }
    return true;
}

bool PosixSession::resize(unsigned short columns, unsigned short rows, std::string& error)
{
    if (!columns || !rows || columns > 1000 || rows > 1000) {
        error = "terminal rows and columns must be between 1 and 1000";
        return false;
    }
    if (!pty_) { error = "PTY is closed"; return false; }
    if (!pty_->resize(columns, rows, error)) return false;
    std::unique_lock lock(mutex_);
    if (termole_vt_resize(vt_, columns, rows) != TERMOLE_CORE_OK) {
        error = "could not resize the terminal core";
        return false;
    }
    ++revision_;
    flush_replies(std::move(lock));
    return true;
}

std::optional<PosixFrame> PosixSession::frame_after(uint64_t revision) const
{
    std::lock_guard lock(mutex_);
    if (revision_ == revision) return std::nullopt;
    const termole_frame* frame = nullptr;
    if (termole_vt_frame(vt_, &frame) != TERMOLE_CORE_OK || !frame) return std::nullopt;
    const size_t count = static_cast<size_t>(frame->columns) * frame->rows;
    return PosixFrame{ revision_, frame->columns, frame->rows,
                      frame->cursor_column, frame->cursor_row, frame->cursor_visible != 0,
                      { frame->cells, frame->cells + count } };
}

bool PosixSession::running() const
{
    std::lock_guard lock(mutex_);
    return !stopped_;
}

termole_modes PosixSession::modes() const
{
    std::lock_guard lock(mutex_);
    termole_modes result{};
    (void)termole_vt_modes(vt_, &result);
    return result;
}

std::string PosixSession::text() const
{
    std::lock_guard lock(mutex_);
    if (!vt_) return {};
    size_t needed = 0;
    if (termole_vt_text(vt_, nullptr, 0, &needed) != TERMOLE_CORE_OUT_OF_SPACE) return {};
    std::string buffer(needed + 1, '\0');
    size_t written = 0;
    if (termole_vt_text(vt_, buffer.data(), buffer.size(), &written) != TERMOLE_CORE_OK)
        return {};
    buffer.resize(written);
    return buffer;
}

int PosixSession::wait()
{
    if (reader_.joinable()) reader_.join();
    std::lock_guard lock(mutex_);
    return exit_code_;
}

void PosixSession::stop()
{
    if (pty_) pty_->close();
    if (reader_.joinable()) reader_.join();
}

} // namespace termole::platform
