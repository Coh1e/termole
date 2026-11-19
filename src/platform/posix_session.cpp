#include "platform/posix_session.h"

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
        std::lock_guard lock(mutex_);
        if (vt_) (void)termole_vt_feed(vt_, buffer, static_cast<size_t>(read));
    }
    const int code = pty_ ? pty_->wait() : -1;
    std::lock_guard lock(mutex_);
    exit_code_ = code;
    stopped_ = true;
}

bool PosixSession::write(std::string_view bytes, std::string& error)
{
    if (bytes.empty()) return true;
    if (!pty_) { error = "PTY is closed"; return false; }
    const ssize_t written = pty_->write(bytes.data(), bytes.size());
    if (written < 0 || static_cast<size_t>(written) != bytes.size()) {
        error = "PTY write failed";
        return false;
    }
    return true;
}

bool PosixSession::resize(unsigned short columns, unsigned short rows, std::string& error)
{
    if (!pty_) { error = "PTY is closed"; return false; }
    return pty_->resize(columns, rows, error);
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
