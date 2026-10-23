#include "hw_live_control.hpp"
#include "hw_interface.hpp"
bool HardwareLiveControl::start(bool enabled, bool automatic_frequency) {
    enabled_ = enabled;
    return !enabled_ || (!automatic_frequency && channel_.start());
}
bool HardwareLiveControl::poll(ChiaDriver *driver, uint8_t running_mask) {
    if (!enabled_ || turns_++ % 256)
        return true;
    return channel_.poll([&](timelord_control::Point target) {
        return set_hw_operating_point(driver, running_mask, target);
    });
}
