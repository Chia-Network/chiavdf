#pragma once
#include "timelord_live_control.hpp"
class ChiaDriver;
// Owns opt-in control cadence; a disabled controller performs no pipe I/O.
// This boundary also lets emulator tests intercept supervisor requests while
// exercising the production client loop and hardware interface.
class HardwareLiveControl {
    timelord_control::Channel channel_;
    bool enabled_ = false;
    timelord_control::Poll cadence_;

  public:
    bool start(bool enabled, bool automatic_frequency);
    bool poll(ChiaDriver *driver, uint8_t running_mask);
};
