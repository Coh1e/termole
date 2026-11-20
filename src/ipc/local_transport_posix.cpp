#include "ipc/local_transport.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace termole::ipc {
namespace {
volatile sig_atomic_t stop_requested = 0;

void stop_listener(int)
{
    stop_requested = 1;
}

struct ListenerSignals {
    struct sigaction previous_interrupt{};
    struct sigaction previous_terminate{};
    bool have_interrupt = false;
    bool have_terminate = false;

    ListenerSignals()
    {
        stop_requested = 0;
        struct sigaction action{};
        action.sa_handler = stop_listener;
        ::sigemptyset(&action.sa_mask);
        have_interrupt = ::sigaction(SIGINT, &action, &previous_interrupt) == 0;
        have_terminate = ::sigaction(SIGTERM, &action, &previous_terminate) == 0;
    }

    ~ListenerSignals()
    {
        if (have_interrupt) ::sigaction(SIGINT, &previous_interrupt, nullptr);
        if (have_terminate) ::sigaction(SIGTERM, &previous_terminate, nullptr);
        stop_requested = 0;
    }
};

struct Socket {
    int fd = -1;
    ~Socket() { if (fd >= 0) ::close(fd); }

};

struct Endpoint {
    sockaddr_un address{};
    bool bound = false;
    ~Endpoint() { if (bound) ::unlink(address.sun_path); }
};

bool open_endpoint(std::string_view name, Endpoint& endpoint, std::string& error)
{
    if (name.empty() || name.size() > 32 ||
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
            std::string_view::npos) {
        error = "endpoint name must have 1–32 letters, digits, hyphens or underscores";
        return false;
    }
    const std::string directory = "/tmp/termole-" + std::to_string(::geteuid());
    if (::mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
        error = std::strerror(errno);
        return false;
    }
    struct stat status{};
    if (::lstat(directory.c_str(), &status) != 0 || !S_ISDIR(status.st_mode) ||
        status.st_uid != ::geteuid() || (status.st_mode & 0077)) {
        error = "the endpoint directory is not private to the current user";
        return false;
    }
    const std::string path = directory + "/" + std::string(name) + ".sock";
    if (path.size() >= sizeof(endpoint.address.sun_path)) return false;
    endpoint.address.sun_family = AF_UNIX;
    std::memcpy(endpoint.address.sun_path, path.c_str(), path.size() + 1);
#ifdef __APPLE__
    endpoint.address.sun_len = sizeof(endpoint.address);
#endif
    return true;
}

bool restrict_socket(int fd)
{
    if (::fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) return false;
    const timeval timeout{ 4, 0 };
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0)
        return false;
#ifdef __APPLE__
    const int enabled = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0)
        return false;
#endif
    return true;
}

} // namespace

int connect_local(std::string_view name, std::string& error)
{
    Endpoint endpoint;
    if (!open_endpoint(name, endpoint, error)) return invalid_handle;
    Socket socket{ ::socket(AF_UNIX, SOCK_STREAM, 0) };
    if (socket.fd < 0 || !restrict_socket(socket.fd) ||
        ::connect(socket.fd, reinterpret_cast<sockaddr*>(&endpoint.address),
                  sizeof(endpoint.address)) != 0) {
        error = std::strerror(errno);
        return invalid_handle;
    }
    const int fd = socket.fd;
    socket.fd = -1;
    return fd;
}

bool run_local_listener(std::string_view name, const ServeClient& serve, std::string& error)
{
    Endpoint endpoint;
    if (!open_endpoint(name, endpoint, error)) return false;
    Socket server{ ::socket(AF_UNIX, SOCK_STREAM, 0) };
    if (server.fd < 0 || !restrict_socket(server.fd) ||
        ::bind(server.fd, reinterpret_cast<sockaddr*>(&endpoint.address),
               sizeof(endpoint.address)) != 0) {
        error = std::strerror(errno);
        return false;
    }
    endpoint.bound = true;
    if (::listen(server.fd, 1) != 0) {
        error = std::strerror(errno);
        return false;
    }
    const ListenerSignals signals;
    while (!stop_requested) {
        pollfd listener{ server.fd, POLLIN, 0 };
        const int ready = ::poll(&listener, 1, 250);
        if (!ready || (ready < 0 && errno == EINTR)) continue;
        if (ready < 0) {
            error = std::strerror(errno);
            return false;
        }
        Socket client{ ::accept(server.fd, nullptr, nullptr) };
        if (client.fd < 0 && errno == EINTR) continue;
        if (client.fd < 0) {
            error = std::strerror(errno);
            return false;
        }
        if (!restrict_socket(client.fd)) continue;
        if (!serve(client.fd)) return true;
    }
    return true;
}

void close_local(int fd)
{
    ::close(fd);
}

bool send_bytes(int fd, std::span<const uint8_t> input)
{
#ifdef __APPLE__
    const int flags = 0;
#else
    const int flags = MSG_NOSIGNAL;
#endif
    while (!input.empty()) {
        const auto count = ::send(fd, input.data(), input.size(), flags);
        if (count < 0 && errno == EINTR && !stop_requested) continue;
        if (count <= 0) return false;
        input = input.subspan(static_cast<size_t>(count));
    }
    return true;
}

bool read_bytes(int fd, std::span<uint8_t> output)
{
    while (!output.empty()) {
        const auto count = ::recv(fd, output.data(), output.size(), 0);
        if (count < 0 && errno == EINTR && !stop_requested) continue;
        if (count <= 0) return false;
        output = output.subspan(static_cast<size_t>(count));
    }
    return true;
}

} // namespace termole::ipc
