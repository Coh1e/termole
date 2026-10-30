#include "common/smoke.h"

namespace termole::app {

// Product build: the entry point has no test hook and no filesystem dependency.
SmokeProbe::SmokeProbe() = default;

SmokeState SmokeProbe::start(Session&)
{
    return SmokeState::Disabled;
}

SmokeState SmokeProbe::poll(Session&)
{
    return SmokeState::Disabled;
}

} // namespace termole::app
