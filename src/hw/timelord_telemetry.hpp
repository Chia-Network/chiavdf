#pragma once
#include <chrono>
#include <cstdint>
#include <optional>

namespace timelord_telemetry {
// Delta rate from already-read iteration counters: no additional USB I/O.
// Reset across idle, rollback, or a new challenge's elapsed-time origin.
class Speed {
    uint64_t iters_ = 0, elapsed_ = 0;
    bool active_ = false;
    uint64_t session_ = 0;

  public:
    std::optional<double> update(bool running, uint64_t iters, uint64_t elapsed, uint64_t session) {
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
    explicit Poll(Clock::time_point now = Clock::now()) : last_(now) {}
    bool due(bool scheduled, Clock::time_point now = Clock::now()) {
        if (!scheduled && now - last_ < std::chrono::seconds(5))
            return false;
        last_ = now;
        return true;
    }

  private:
    Clock::time_point last_;
};
} // namespace timelord_telemetry
