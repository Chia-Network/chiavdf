#ifndef CHIAVDF_HW_INITIALIZATION_HPP
#define CHIAVDF_HW_INITIALIZATION_HPP

#include "hw_interface.hpp"
#include "chia_registers.hpp"
#include <cstdio>

namespace hw_initialization {

// Initialization discards the previous owner's jobs. Unlike a live tuning
// pause, it must hold every engine in reset as well as gate its clock.
template <class Hardware> bool quiesce_all(Hardware &hardware) {
    constexpr uint32_t reset_bit = 1U << CHIA_VDF_CONTROL_RESET_BIT;
    constexpr uint32_t clock_bit = 1U << CHIA_VDF_CONTROL_CLK_ENABLE_BIT;
    for (unsigned attempt = 1; attempt <= 2; ++attempt) {
        for (unsigned engine = 0; engine < N_HW_VDFS; ++engine) {
            // Attempt both writes and every engine even after a partial error.
            if (!hardware.engine_control(engine, 0))
                fprintf(stderr, "Initialization clock gate failed engine=%u attempt=%u\n", engine, attempt);
            if (!hardware.engine_control(engine, reset_bit))
                fprintf(stderr, "Initialization engine reset failed engine=%u attempt=%u\n", engine, attempt);
        }
        // Preserve the board reset formerly hidden inside SetPLLFrequency.
        // It also recovers a partial engine-control failure before readback.
        const bool reset = hardware.reset();
        if (!reset)
            fprintf(stderr, "Initialization board reset failed attempt=%u\n", attempt);
        uint8_t confirmed = 0;
        for (unsigned engine = 0; engine < N_HW_VDFS; ++engine) {
            uint32_t control = 0;
            const bool read = hardware.engine_control_read(engine, control);
            if (read && (control & (reset_bit | clock_bit)) == reset_bit)
                confirmed |= static_cast<uint8_t>(1U << engine);
            else
                fprintf(stderr, "Initialization quiescence unconfirmed engine=%u attempt=%u read=%d control=0x%08x\n",
                        engine, attempt, read, control);
        }
        if (reset && confirmed == HW_VDF_ALL_ENGINES)
            return true;
    }
    return false;
}

template <class Hardware> bool operating_point(Hardware &hardware, double frequency, double voltage) {
    return quiesce_all(hardware) && hardware.frequency(frequency) && hardware.voltage(voltage);
}

} // namespace hw_initialization
#endif
