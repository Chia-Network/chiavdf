#ifndef CHIAVDF_HW_INITIALIZATION_HPP
#define CHIAVDF_HW_INITIALIZATION_HPP

#include "hw_interface.hpp"
#include "chia_registers.hpp"
#include "hw_util.hpp"

namespace hw_initialization {

// Initialization discards the previous owner's jobs. Unlike a live tuning
// pause, it must hold every engine in reset as well as gate its clock.
template <class Hardware> bool quiesce_all(Hardware &hardware) {
    constexpr uint32_t reset_bit = 1U << CHIA_VDF_CONTROL_RESET_BIT;
    constexpr uint32_t clock_bit = 1U << CHIA_VDF_CONTROL_CLK_ENABLE_BIT;
    for (unsigned attempt = 1; attempt <= 2; ++attempt) {
        // Reset first: readback must verify our subsequent writes, not an
        // assumed power-on value of the control registers.
        const bool reset = hardware.reset();
        if (!reset)
            LOG_ERROR("Initialization board reset failed attempt=%u", attempt);
        uint8_t written = 0;
        for (unsigned engine = 0; engine < N_HW_VDFS; ++engine) {
            const bool gate = hardware.engine_control(engine, 0);
            const bool held = hardware.engine_control(engine, reset_bit);
            if (!gate)
                LOG_ERROR("Initialization clock gate failed engine=%u attempt=%u", engine, attempt);
            if (!held)
                LOG_ERROR("Initialization engine reset failed engine=%u attempt=%u", engine, attempt);
            if (gate && held) written |= static_cast<uint8_t>(1U << engine);
        }
        uint8_t confirmed = 0;
        for (unsigned engine = 0; engine < N_HW_VDFS; ++engine) {
            uint32_t control = 0;
            const bool read = hardware.engine_control_read(engine, control);
            if (read && (control & (reset_bit | clock_bit)) == reset_bit) {
                confirmed |= static_cast<uint8_t>(1U << engine);
                LOG_INFO("Initialization quiescence confirmed engine=%u attempt=%u control=0x%08x",
                        engine, attempt, control);
            } else
                LOG_ERROR("Initialization quiescence unconfirmed engine=%u attempt=%u read=%d control=0x%08x",
                        engine, attempt, read, control);
        }
        if (reset && written == HW_VDF_ALL_ENGINES && confirmed == HW_VDF_ALL_ENGINES)
            return true;
    }
    return false;
}

template <class Hardware> bool operating_point(Hardware &hardware, double frequency, double voltage) {
    return quiesce_all(hardware) && hardware.frequency(frequency) && hardware.voltage(voltage);
}

} // namespace hw_initialization
#endif
