#include "platform/posix_session.h"

namespace termole::platform {

std::unique_ptr<PosixSession> PosixSession::create(const PosixSessionOptions& options,
                                                    std::string& error)
{
    std::unique_ptr<PosixSession> session(new PosixSession(options.max_queued_reply_bytes));
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
            // An unresponsive child must not grow the reply queue indefinitely.
            if (size > session.max_queued_reply_bytes_ - session.replies_.size()) {
                session.pty_->close();
                return;
            }
            try {
                session.replies_.append(static_cast<const char*>(data), size);
                session.replies_ready_.notify_one();
            } catch (...) {
                // The C callback cannot report allocation failure.
                session.pty_->close();
            }
        },
        nullptr, nullptr
    };
    (void)termole_vt_set_callbacks(session->vt_, &callbacks);
    session->reader_ = std::thread([raw = session.get()] { raw->read_loop(); });
    session->writer_ = std::thread([raw = session.get()] { raw->write_replies(); });
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
        std::lock_guard lock(mutex_);
        if (vt_) (void)termole_vt_feed(vt_, buffer, static_cast<size_t>(read));
        ++revision_;
    }
    const int code = pty_ ? pty_->wait() : -1;
    std::lock_guard lock(mutex_);
    exit_code_ = code;
    stopped_ = true;
    replies_ready_.notify_one();
}

void PosixSession::write_replies()
{
    std::string bytes;
    for (;;) {
        {
            std::unique_lock lock(mutex_);
            replies_ready_.wait(lock, [&] { return stopped_ || !replies_.empty(); });
            if (stopped_) return;
            bytes.swap(replies_);
        }
        // A child may fill its output while waiting to read an in-flight paste.
        // Only writers serialize here. The PTY reader must keep draining output.
        std::lock_guard lock(input_mutex_);
        if (!write_all(bytes)) return;
        bytes.clear();
    }
}

bool PosixSession::write_all(std::string_view bytes)
{
    while (!bytes.empty()) {
        const ssize_t written = pty_->write(bytes.data(), bytes.size());
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
    std::lock_guard lock(mutex_);
    if (termole_vt_resize(vt_, columns, rows) != TERMOLE_CORE_OK) {
        error = "could not resize the terminal core";
        return false;
    }
    ++revision_;
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
    if (writer_.joinable()) writer_.join();
    std::lock_guard lock(mutex_);
    return exit_code_;
}

void PosixSession::stop()
{
    if (pty_) pty_->close();
    if (reader_.joinable()) reader_.join();
    if (writer_.joinable()) writer_.join();
}

} // namespace termole::platform
