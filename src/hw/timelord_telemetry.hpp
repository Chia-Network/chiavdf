#pragma once
#include <chrono>
#include <cstdint>
#include <optional>

namespace timelord_telemetry {
// Delta rate from already-read iteration counters: no additional USB I/O.
// Reset across idle, rollback, or a new challenge's elapsed-time origin.
struct Sample {
    bool running = false;
    uint64_t iterations = 0, elapsed = 0, session = 0;
};
class Speed {
    uint64_t iters_ = 0, elapsed_ = 0;
    bool active_ = false;
    uint64_t session_ = 0;

  public:
    std::optional<double> update(Sample sample) {
        const auto [running, iters, elapsed, session] = sample;
        if (!running) {
            if (!active_)
                return std::nullopt;
            active_ = false;
            return -1.;
        }
        if (!active_ || session != session_ || iters < iters_ || elapsed < elapsed_) {
            active_ = true;
            session_ = session;
            iters_ = iters;
            elapsed_ = elapsed;
            return -1.;
        }
        if (elapsed - elapsed_ < 1000000)
            return std::nullopt;
        const double rate = double(iters - iters_) * 1000000. / double(elapsed - elapsed_);
        iters_ = iters;
        elapsed_ = elapsed;
        return rate;
    }
};
// Keep the usual active polling cadence, but never let idle boards go silent.
class Poll {
  public:
    using Clock = std::chrono::steady_clock;
    Poll(bool keep_idle_alive, uint32_t active_period, Clock::time_point now = Clock::now())
        : keep_idle_alive_(keep_idle_alive), active_period_(active_period), last_(now) {}
    bool due(bool running, Clock::time_point now = Clock::now()) {
        const bool scheduled = turns_++ % active_period_ == 0;
        if (!(running && scheduled) &&
            !(keep_idle_alive_ && now - last_ >= std::chrono::seconds(5)))
            return false;
        last_ = now;
        return true;
    }

  private:
    bool keep_idle_alive_;
    uint32_t active_period_;
    uint64_t turns_ = 0;
    Clock::time_point last_;
};
} // namespace timelord_telemetry
