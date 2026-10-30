#pragma once

#include <termole/termole.hpp>

#if defined(TERMOLE_ENABLE_SMOKE)
#include <chrono>
#include <filesystem>
#include <optional>
#endif

#include <string>

namespace termole::app {

enum class SmokeState {
    Disabled,
    Running,
    Passed,
    Failed,
};

// Test-only entry-point probe. When TERMOLE_SMOKE_RESULT is set, the test-only
// app variant types a real command into its real default shell and records the
// screen snapshot. Product entry points compile the no-op implementation instead,
// so no test hook or filesystem startup cost is present in the shipped binary.
class SmokeProbe {
public:
    SmokeProbe();

    [[nodiscard]] bool enabled() const noexcept
    {
#if defined(TERMOLE_ENABLE_SMOKE)
        return result_path_.has_value();
#else
        return false;
#endif
    }

    SmokeState start(Session& session);
    SmokeState poll(Session& session);

#if defined(TERMOLE_ENABLE_SMOKE)
private:
    void write_result(const std::string& text);

    std::optional<std::filesystem::path> result_path_;
    std::chrono::steady_clock::time_point deadline_{};
    bool started_ = false;
    bool finished_ = false;
    bool failed_ = false;
#endif
};

} // namespace termole::app
