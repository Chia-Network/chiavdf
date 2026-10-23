#pragma once
#include "timelord_live_control.hpp"
class ChiaDriver;
// Owns opt-in control cadence; a disabled controller performs no pipe I/O.
class HardwareLiveControl {
    timelord_control::Channel channel_;
    bool enabled_ = false;
    unsigned turns_ = 0;

  public:
    bool start(bool enabled, bool automatic_frequency);
    bool poll(ChiaDriver *driver, uint8_t running_mask);
};
