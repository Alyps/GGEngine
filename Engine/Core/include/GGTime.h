#pragma once
#include <chrono>
#include <cstdint>

class TimeManager {
public:
    class FixedTime {
    public:
        double get_interval() const noexcept { return interval; }
        double get_accumulator() const noexcept { return accumulator; }
        std::uint64_t get_steps() const noexcept { return steps; }
        double get_alpha() const noexcept { return accumulator / interval; }

    private:
        friend class TimeManager;
        explicit FixedTime(double fixedInterval);
        void update(double elapsed);
        void reset() noexcept;

        double interval; // Длительность одного фиксированного шага, в секундах.
        double accumulator = 0.0; // Остаток времени после расчёта целых шагов, в секундах.
        std::uint64_t steps = 0; // Количество фиксированных шагов для текущего кадра.
    };

    // Durations are in seconds. The fixed interval must be finite and positive.
    explicit TimeManager(double fixedInterval = 1.0 / 60.0);

    double get_deltaTime() const noexcept { return deltaTime; }
    double get_totalTime() const noexcept { return totalTime; }
    std::uint64_t get_frames() const noexcept { return frames; }
    double get_fps() const noexcept { return fps; }
    const FixedTime& get_fixedTime() const noexcept { return fixedTime; }

    // Call once per frame; execute all reported steps before the next update.
    double update();
    // Restart timing after loading, preserving the fixed interval.
    void reset() noexcept;

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point PrevTime; // Отметка часов при последнем update() или начале отсчёта.
    FixedTime fixedTime; 
    double deltaTime = 0.0; // Время между текущим и предыдущим update(), в секундах.
    double totalTime = 0.0; // Суммарное измеренное время с начала отсчёта, в секундах.
    std::uint64_t frames = 0; // Количество вызовов update() с начала отсчёта.
    std::uint64_t fpsFrames = 0; // Количество кадров в текущем интервале измерения FPS.
    double fpsElapsed = 0.0; // Длительность текущего интервала измерения FPS, в секундах.
    double fps = 0.0; // Последний рассчитанный FPS; до первого секундного замера равен нулю.
};
