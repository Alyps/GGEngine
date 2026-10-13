#include "GGTime.h"
#include <cmath>
#include <limits>
#include <stdexcept>


TimeManager::FixedTime::FixedTime(double fixedInterval) : interval{ fixedInterval }
{
    if (!std::isfinite(interval) || interval <= 0.0)
        throw std::invalid_argument("Fixed time interval must be finite and positive");
}

void TimeManager::FixedTime::update(double elapsed)
{
    const double accumulated = accumulator + elapsed;
    const double stepCount = std::floor(accumulated / interval);

    // Check representability before conversion; this is not a catch-up limit.
    /*if (!std::isfinite(stepCount) ||
        stepCount >= std::ldexp(1.0, std::numeric_limits<std::uint64_t>::digits))
        throw std::overflow_error("Fixed step count exceeds uint64_t range");*/

    steps = static_cast<std::uint64_t>(stepCount);
    accumulator = accumulated - static_cast<double>(steps) * interval;
}

void TimeManager::FixedTime::reset() noexcept
{
    accumulator = 0.0;
    steps = 0;
}

TimeManager::TimeManager(double fixedInterval)
    : PrevTime{ Clock::now() }, fixedTime{ fixedInterval }
{
}

double TimeManager::update()
{
    const auto curTime = Clock::now();
    const double elapsed = std::chrono::duration<double>(curTime - PrevTime).count();
    fixedTime.update(elapsed);
    deltaTime = elapsed;
    PrevTime = curTime;
    totalTime += deltaTime;
    ++frames;
    ++fpsFrames;
    fpsElapsed += deltaTime;

    if (fpsElapsed >= 1.0)
    {
        fps = static_cast<double>(fpsFrames) / fpsElapsed;
        fpsFrames = 0;
        fpsElapsed = 0.0;
    }

    return deltaTime;
}

void TimeManager::reset() noexcept
{
    PrevTime = Clock::now();
    fixedTime.reset();
    deltaTime = 0.0;
    totalTime = 0.0;
    frames = 0;
    fpsFrames = 0;
    fpsElapsed = 0.0;
    fps = 0.0;
}
