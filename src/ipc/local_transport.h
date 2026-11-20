#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace termole::ipc {

#ifdef _WIN32
using LocalHandle = void*;
inline constexpr LocalHandle invalid_handle = nullptr;
#else
using LocalHandle = int;
inline constexpr LocalHandle invalid_handle = -1;
#endif

using ServeClient = std::function<bool(LocalHandle)>;

LocalHandle connect_local(std::string_view name, std::string& error);
bool run_local_listener(std::string_view name, const ServeClient& serve, std::string& error);
void close_local(LocalHandle handle);
bool send_bytes(LocalHandle handle, std::span<const uint8_t> bytes);
bool read_bytes(LocalHandle handle, std::span<uint8_t> bytes);

} // namespace termole::ipc
