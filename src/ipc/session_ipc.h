#pragma once

#include "core/frame.h"
#include "ipc/local_transport.h"

#ifdef _WIN32
#include "session/session.h"
#else
#include "platform/posix_session.h"
#endif

#include <memory>
#include <span>
#include <string_view>

namespace termole::ipc {

#ifdef _WIN32
using ServerSessionOptions = SessionOptions;
#else
using ServerSessionOptions = platform::PosixSessionOptions;
#endif

class RemoteSession {
public:
    static std::unique_ptr<RemoteSession> connect(std::string_view name, bool detach,
                                                  std::string& error);
    ~RemoteSession();

    RemoteSession(const RemoteSession&) = delete;
    RemoteSession& operator=(const RemoteSession&) = delete;

    bool write(std::string_view bytes, std::string& error);
    bool resize(uint16_t columns, uint16_t rows, std::string& error);
    std::optional<SessionFrame> frame_after(uint64_t revision);
    termole_modes modes();
    bool running();
    int wait() const { return exit_code_; }

private:
    explicit RemoteSession(LocalHandle socket) : socket_(socket) {}
    bool request(uint8_t type, std::span<const uint8_t> input, std::vector<uint8_t>& output,
                 std::string& error);

    LocalHandle socket_;
    int exit_code_ = 125;
    bool connected_ = true;
};

int run_server(std::string_view name, const ServerSessionOptions& options);

} // namespace termole::ipc
