#pragma once

#include <termole/termole.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

namespace termole::app {

enum class SmokeState {
    Disabled,
    Running,
    Passed,
    Failed,
};

// Test-only entry-point probe. When TERMOLE_SMOKE_RESULT is set, the app types
// a real command into its real default shell and records the screen snapshot.
// It exercises the same Session/View path as a user launch.
class SmokeProbe {
public:
    SmokeProbe();

    [[nodiscard]] bool enabled() const noexcept { return result_path_.has_value(); }
    SmokeState start(Session& session);
    SmokeState poll(Session& session);

private:
    void write_result(const std::string& text);

    std::optional<std::filesystem::path> result_path_;
    std::chrono::steady_clock::time_point deadline_{};
    bool started_ = false;
    bool finished_ = false;
    bool failed_ = false;
};

} // namespace termole::app
