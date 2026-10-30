#include "common/smoke.h"

#include <windows.h>

#include <array>
#include <fstream>

namespace termole::app {
#if defined(TERMOLE_ENABLE_SMOKE)
namespace {

std::optional<std::filesystem::path> smoke_result_path()
{
    std::array<wchar_t, 32768> buffer{};
    const DWORD length =
        ::GetEnvironmentVariableW(L"TERMOLE_SMOKE_RESULT", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return std::nullopt;
    return std::filesystem::path(std::wstring(buffer.data(), length));
}

} // namespace
#endif

SmokeProbe::SmokeProbe()
{
#if defined(TERMOLE_ENABLE_SMOKE)
    result_path_ = smoke_result_path();
#endif
}

SmokeState SmokeProbe::start(Session& session)
{
    if (!enabled()) return SmokeState::Disabled;
    if (started_) return SmokeState::Running;

    // Keep the literal marker out of the echoed command line: only actual
    // command output can satisfy the test.
    const auto result = session.write("Write-Output ('termole-' + 'smoke-ok')\r");
    if (!result) {
        write_result("WRITE FAILED: " + result.error().message());
        started_ = true;
        finished_ = true;
        failed_ = true;
        return SmokeState::Failed;
    }
    started_ = true;
    deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    return SmokeState::Running;
}

SmokeState SmokeProbe::poll(Session& session)
{
    if (!enabled()) return SmokeState::Disabled;
    if (!started_) return SmokeState::Running;
    if (finished_) return failed_ ? SmokeState::Failed : SmokeState::Passed;

    const auto text = session.screen_text();
    if (text && text->find("termole-smoke-ok") != std::string::npos) {
        write_result(*text);
        finished_ = true;
        return SmokeState::Passed;
    }
    if (std::chrono::steady_clock::now() >= deadline_) {
        write_result("TIMEOUT\n" + (text ? *text : std::string{}));
        finished_ = true;
        failed_ = true;
        return SmokeState::Failed;
    }
    return SmokeState::Running;
}

void SmokeProbe::write_result(const std::string& text)
{
    if (!result_path_) return;
    std::error_code error;
    if (result_path_->has_parent_path())
        std::filesystem::create_directories(result_path_->parent_path(), error);
    std::ofstream output(*result_path_, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

} // namespace termole::app
