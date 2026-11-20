#include "ipc/session_ipc.h"

#include "ipc/session_protocol.h"

#include "ipc/local_transport.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <span>
#include <utility>

namespace termole::ipc {
namespace {
using Bytes = std::vector<uint8_t>;



constexpr size_t frame_prefix = 28;
constexpr size_t cell_size = 48;
constexpr size_t max_input = 1024 * 1024;

bool send_message(LocalHandle fd, uint8_t type, std::span<const uint8_t> payload)
{
    if (payload.size() > TERMOLE_IPC_MAX_PAYLOAD) return false;
    uint8_t header[TERMOLE_IPC_HEADER_SIZE];
    termole_ipc_write_header(header, { static_cast<uint32_t>(payload.size()), type,
                                      TERMOLE_IPC_VERSION });
    return send_bytes(fd, header) && send_bytes(fd, payload);
}

bool read_message(LocalHandle fd, termole_ipc_header& header, Bytes& payload, size_t limit)
{
    uint8_t bytes[TERMOLE_IPC_HEADER_SIZE];
    if (!read_bytes(fd, bytes) || !termole_ipc_read_header(bytes, &header) || header.size > limit)
        return false;
    payload.resize(header.size);
    return read_bytes(fd, payload);
}

void put(Bytes& bytes, size_t offset, uint32_t value)
{
    termole_ipc_store_u32(bytes.data() + offset, value);
}

uint32_t get(std::span<const uint8_t> bytes, size_t offset)
{
    return termole_ipc_load_u32(bytes.data() + offset);
}

Bytes encode_frame(const SessionFrame& frame)
{
    Bytes out(frame_prefix + frame.cells.size() * cell_size);
    put(out, 0, static_cast<uint32_t>(frame.revision));
    put(out, 4, static_cast<uint32_t>(frame.revision >> 32));
    put(out, 8, frame.columns);
    put(out, 12, frame.rows);
    put(out, 16, frame.cursor_column);
    put(out, 20, frame.cursor_row);
    put(out, 24, frame.cursor_visible);
    size_t offset = frame_prefix;
    for (const auto& cell : frame.cells) {
        for (unsigned i = 0; i < TERMOLE_CELL_MAX_CODEPOINTS; ++i)
            put(out, offset + i * 4, i < cell.codepoint_count ? cell.codepoints[i] : 0);
        put(out, offset + 32, cell.foreground);
        put(out, offset + 36, cell.background);
        put(out, offset + 40, cell.attributes);
        put(out, offset + 44, cell.codepoint_count | (uint32_t{cell.width} << 8) |
            (uint32_t{cell.continuation} << 16) | (uint32_t{cell.foreground_is_default} << 17) |
            (uint32_t{cell.background_is_default} << 18));
        offset += cell_size;
    }
    return out;
}

std::optional<SessionFrame> decode_frame(std::span<const uint8_t> bytes)
{
    if (bytes.size() < frame_prefix) return std::nullopt;
    const uint32_t columns = get(bytes, 8);
    const uint32_t rows = get(bytes, 12);
    if (!columns || columns > 1000 || !rows || rows > 1000 ||
        bytes.size() != frame_prefix + static_cast<size_t>(columns) * rows * cell_size ||
        get(bytes, 16) >= columns || get(bytes, 20) >= rows) return std::nullopt;
    SessionFrame frame{ get(bytes, 0) | (static_cast<uint64_t>(get(bytes, 4)) << 32),
                      static_cast<uint16_t>(columns), static_cast<uint16_t>(rows),
                      static_cast<uint16_t>(get(bytes, 16)), static_cast<uint16_t>(get(bytes, 20)),
                      get(bytes, 24) != 0, {} };
    frame.cells.resize(static_cast<size_t>(columns) * rows);
    size_t offset = frame_prefix;
    for (auto& cell : frame.cells) {
        for (unsigned i = 0; i < TERMOLE_CELL_MAX_CODEPOINTS; ++i)
            cell.codepoints[i] = get(bytes, offset + i * 4);
        cell.foreground = get(bytes, offset + 32);
        cell.background = get(bytes, offset + 36);
        cell.attributes = static_cast<uint16_t>(get(bytes, offset + 40));
        const uint32_t packed = get(bytes, offset + 44);
        cell.codepoint_count = packed & 255;
        if (cell.codepoint_count > TERMOLE_CELL_MAX_CODEPOINTS) return std::nullopt;
        cell.width = (packed >> 8) & 255;
        cell.continuation = (packed >> 16) & 1;
        cell.foreground_is_default = (packed >> 17) & 1;
        cell.background_is_default = (packed >> 18) & 1;
        offset += cell_size;
    }
    return frame;
}

bool send_error(LocalHandle fd, std::string_view error)
{
    return send_message(fd, TERMOLE_IPC_ERROR,
                        { reinterpret_cast<const uint8_t*>(error.data()), error.size() });
}

template<class Session>
bool serve_connection(LocalHandle fd, Session& session)
{
    Bytes input;
    termole_ipc_header request{};
    if (!read_message(fd, request, input, max_input)) return true;
    if (request.version != TERMOLE_IPC_VERSION) {
        (void)send_error(fd, "incompatible Termole protocol version");
        return true;
    }
    if (request.type != TERMOLE_IPC_ATTACH || input.size() != 1 || input[0] > 1) {
        (void)send_error(fd, "a valid attach request is required");
        return true;
    }
    const bool detach = input[0] != 0;
    if (!send_message(fd, TERMOLE_IPC_OK, {})) return true;
    while (read_message(fd, request, input, max_input)) {
        if (request.version != TERMOLE_IPC_VERSION) break;
        Bytes output;
        std::string error;
        switch (request.type) {
        case TERMOLE_IPC_FRAME:
            if (input.size() != 8) { error = "invalid frame request"; break; }
            if (auto frame = session.frame_after(get(input, 0) |
                    (static_cast<uint64_t>(get(input, 4)) << 32)))
                output = encode_frame(*frame);
            break;
        case TERMOLE_IPC_INPUT:
            if (!input.empty())
                (void)session.write({ reinterpret_cast<const char*>(input.data()), input.size() },
                                    error);
            break;
        case TERMOLE_IPC_RESIZE:
            if (input.size() != 8 || get(input, 0) > 1000 || get(input, 4) > 1000) {
                error = "invalid terminal size";
                break;
            }
            (void)session.resize(static_cast<uint16_t>(get(input, 0)),
                                static_cast<uint16_t>(get(input, 4)), error);
            break;
        case TERMOLE_IPC_MODES: {
            const auto modes = session.modes();
            output = { modes.application_cursor, modes.bracketed_paste };
            break;
        }
        case TERMOLE_IPC_EXIT_STATUS:
            output.resize(5);
            if (!session.running()) {
                output[0] = 1;
                put(output, 1, static_cast<uint32_t>(session.wait()));
            }
            break;
        default:
            error = "unknown Termole request";
            break;
        }
        if (!(error.empty() ? send_message(fd, TERMOLE_IPC_OK, output) : send_error(fd, error)))
            break;
    }
    return detach;
}
} // namespace

std::unique_ptr<RemoteSession> RemoteSession::connect(std::string_view name, bool detach,
                                                       std::string& error)
{
    const LocalHandle socket = connect_local(name, error);
    if (socket == invalid_handle) return nullptr;
    auto session = std::unique_ptr<RemoteSession>(new RemoteSession(socket));
    const uint8_t flags = detach;
    Bytes result;
    if (!session->request(TERMOLE_IPC_ATTACH, { &flags, 1 }, result, error)) return nullptr;
    return session;
}

RemoteSession::~RemoteSession()
{
    close_local(socket_);
}

bool RemoteSession::request(uint8_t type, std::span<const uint8_t> input, Bytes& output,
                            std::string& error)
{
    termole_ipc_header response{};
    if (!connected_ || !send_message(socket_, type, input) ||
        !read_message(socket_, response, output, TERMOLE_IPC_MAX_PAYLOAD) ||
        response.version != TERMOLE_IPC_VERSION) {
        connected_ = false;
        error = "Termole server connection closed";
        return false;
    }
    if (response.type != TERMOLE_IPC_OK) {
        error.assign(output.begin(), output.end());
        return false;
    }
    return true;
}

bool RemoteSession::write(std::string_view bytes, std::string& error)
{
    if (bytes.size() > max_input) {
        error = "terminal input exceeds the protocol limit";
        return false;
    }
    Bytes output;
    return request(TERMOLE_IPC_INPUT,
                   { reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size() }, output, error);
}

bool RemoteSession::resize(uint16_t columns, uint16_t rows, std::string& error)
{
    Bytes size(8), output;
    put(size, 0, columns);
    put(size, 4, rows);
    return request(TERMOLE_IPC_RESIZE, size, output, error);
}

std::optional<SessionFrame> RemoteSession::frame_after(uint64_t revision)
{
    Bytes input(8), output;
    put(input, 0, static_cast<uint32_t>(revision));
    put(input, 4, static_cast<uint32_t>(revision >> 32));
    std::string error;
    if (!request(TERMOLE_IPC_FRAME, input, output, error) || output.empty()) return std::nullopt;
    auto frame = decode_frame(output);
    if (!frame) connected_ = false;
    return frame;
}

termole_modes RemoteSession::modes()
{
    Bytes output;
    std::string error;
    termole_modes result{};
    if (request(TERMOLE_IPC_MODES, {}, output, error) && output.size() == 2) {
        result.application_cursor = output[0];
        result.bracketed_paste = output[1];
    }
    return result;
}

bool RemoteSession::running()
{
    Bytes output;
    std::string error;
    if (!request(TERMOLE_IPC_EXIT_STATUS, {}, output, error) || output.size() != 5) return false;
    if (!output[0]) return true;
    exit_code_ = static_cast<int>(get(output, 1));
    return false;
}

int run_server(std::string_view name, const ServerSessionOptions& options)
{
    std::string error;
#ifdef _WIN32
    auto created = NativeSession::create(options);
    if (!created) {
        std::fprintf(stderr, "%s\n", created.error().message.c_str());
        return 1;
    }
    struct ServerSession {
        NativeSession& session;
        std::optional<SessionFrame> frame_after(uint64_t revision) { return session.frame_after(revision); }
        termole_modes modes() const { return session.modes(); }
        bool running() const { return session.running(); }
        int wait() const { return session.exit_code().value_or(-1); }
        void write(std::string_view bytes, std::string& error) {
            if (auto result = session.write(bytes); !result) error = result.error().message;
        }
        void resize(uint16_t columns, uint16_t rows, std::string& error) {
            if (auto result = session.resize(columns, rows); !result) error = result.error().message;
        }
    } session{ **created };
#else
    auto created = platform::PosixSession::create(options, error);
    if (!created) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    auto& session = *created;
#endif
    if (!run_local_listener(name, [&](LocalHandle client) { return serve_connection(client, session); },
                            error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    return 0;
}
} // namespace termole::ipc
