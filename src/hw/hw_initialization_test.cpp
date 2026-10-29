#include "hw_initialization.hpp"
#include "chia_driver.hpp"
#include "emu_runner.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr uint32_t kReset = 1U << CHIA_VDF_CONTROL_RESET_BIT;
constexpr uint32_t kClock = 1U << CHIA_VDF_CONTROL_CLK_ENABLE_BIT;

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

struct Event {
    std::string operation;
    unsigned engine = N_HW_VDFS;
    uint32_t value = 0;
};

// Each initialization owns its trace/fault state; ordinary partial-mask work
// cannot contaminate it. Exercise the same sequence used by the real adapter.
struct Hardware {
    std::array<uint32_t, N_HW_VDFS> controls;
    std::vector<Event> trace;
    uint8_t failed_writes = 0;
    unsigned write_failures = std::numeric_limits<unsigned>::max();
    uint8_t failed_reads = 0;
    uint8_t running_readback = 0;
    uint8_t missing_reset_readback = 0;
    unsigned reset_failures = 0;
    unsigned reset_calls = 0;
    bool failed_frequency = false;
    bool failed_voltage = false;
    Hardware() { controls.fill(kClock); }

    bool engine_control(unsigned engine, uint32_t value) {
        trace.push_back({"control", engine, value});
        if ((failed_writes & (1U << engine)) && reset_calls <= write_failures) return false;
        controls[engine] = value;
        return true;
    }
    bool reset() {
        trace.push_back({"board_reset"});
        if (++reset_calls <= reset_failures) return false;
        controls.fill(0);
        return true;
    }
    bool engine_control_read(unsigned engine, uint32_t &value) {
        trace.push_back({"read", engine});
        value = controls[engine];
        if (running_readback & (1U << engine)) value |= kClock;
        if (missing_reset_readback & (1U << engine)) value &= ~kReset;
        return !(failed_reads & (1U << engine));
    }
    bool frequency(double) {
        trace.push_back({"frequency"});
        for (const auto value : controls)
            require((value & (kReset | kClock)) == kReset, "PLL before engine reset");
        return !failed_frequency;
    }
    bool voltage(double) {
        trace.push_back({"voltage"});
        return !failed_voltage;
    }
    unsigned count(const std::string &operation) const {
        unsigned result = 0;
        for (const auto &event : trace) result += event.operation == operation;
        return result;
    }
};

void check_order() {
    // Even the programmatic frequency-zero/index path must quiesce/reset first.
    for (const double frequency : {1100., 0.}) {
        Hardware hardware;
        require(hw_initialization::operating_point(hardware, frequency, 0.88), "initialization failed");
        size_t position = 0;
        require(hardware.trace.at(position++).operation == "board_reset", "missing initial board reset");
        for (unsigned engine = 0; engine < N_HW_VDFS; ++engine) {
            const auto &gate = hardware.trace.at(position++);
            const auto &reset = hardware.trace.at(position++);
            require(gate.operation == "control" && gate.engine == engine && gate.value == 0,
                    "clock gate order");
            require(reset.operation == "control" && reset.engine == engine && reset.value == kReset,
                    "engine reset order");
        }
        for (unsigned engine = 0; engine < N_HW_VDFS; ++engine) {
            const auto &read = hardware.trace.at(position++);
            require(read.operation == "read" && read.engine == engine, "missing post-reset readback");
        }
        require(hardware.trace.at(position++).operation == "frequency", "PLL sequence");
        require(hardware.trace.at(position++).operation == "voltage", "voltage sequence");
        require(position == hardware.trace.size(), "unexpected initialization action");
    }
}

void require_no_settings(Hardware &hardware) {
    require(!hw_initialization::operating_point(hardware, 1100., 0.88), "unconfirmed stop accepted");
    require(hardware.reset_calls == 2, "reset recovery not bounded to two attempts");
    require(hardware.count("frequency") == 0 && hardware.count("voltage") == 0,
            "settings changed after failed quiescence");
    require(hardware.count("control") == 4 * N_HW_VDFS && hardware.count("read") == 2 * N_HW_VDFS,
            "partial failure skipped other engines");
}

void check_failures() {
    for (unsigned engine = 0; engine < N_HW_VDFS; ++engine) {
        const auto bit = static_cast<uint8_t>(1U << engine);
        Hardware partial;
        partial.failed_writes = bit;
        partial.write_failures = 1;
        require(hw_initialization::operating_point(partial, 1100., 0.88) && partial.reset_calls == 2,
                "second attempt did not recover partial control write failure");
        Hardware permanent;
        permanent.failed_writes = bit;
        require_no_settings(permanent);
        Hardware unreadable;
        unreadable.failed_reads = bit;
        require_no_settings(unreadable);
        Hardware running;
        running.running_readback = bit;
        require_no_settings(running);
        Hardware only_gated;
        only_gated.missing_reset_readback = bit;
        require_no_settings(only_gated);
    }
    Hardware broken_reset;
    broken_reset.failed_writes = HW_VDF_ALL_ENGINES;
    broken_reset.reset_failures = std::numeric_limits<unsigned>::max();
    require_no_settings(broken_reset);
    Hardware reset_error;
    reset_error.reset_failures = std::numeric_limits<unsigned>::max();
    require_no_settings(reset_error);
    Hardware recovered;
    recovered.failed_writes = HW_VDF_ALL_ENGINES;
    recovered.reset_failures = 1;
    recovered.write_failures = 1;
    require(hw_initialization::operating_point(recovered, 1100., 0.88) && recovered.reset_calls == 2,
            "transient reset failure did not recover");
    Hardware pll_error;
    pll_error.failed_frequency = true;
    require(!hw_initialization::operating_point(pll_error, 1100., 0.88) && pll_error.count("voltage") == 0,
            "voltage changed after PLL failure");
    Hardware voltage_error;
    voltage_error.failed_voltage = true;
    require(!hw_initialization::operating_point(voltage_error, 1100., 0.88), "voltage failure hidden");
}

bool valid_initialization_trace(const std::vector<EmuOperation> &operations) {
    uint8_t verified = 0;
    bool reset_seen = false, pll_seen = false, voltage_seen = false;
    for (const auto &operation : operations) {
        if (operation.kind == EmuOperation::reset) {
            reset_seen = true;
            verified = 0;
        }
        for (unsigned engine = 0; engine < N_HW_VDFS; ++engine) {
            if (operation.kind == EmuOperation::read &&
                operation.address == CHIA_VDF_CONTROL_REG_OFFSET + HW_VDF_ENGINE_STRIDE * engine &&
                (operation.value & (kClock | kReset)) == kReset)
                verified |= static_cast<uint8_t>(1U << engine);
        }
        if (operation.kind == EmuOperation::write && operation.address < 0x20) {
            if (!reset_seen || verified != HW_VDF_ALL_ENGINES) return false;
            pll_seen = true;
        }
        if (operation.kind == EmuOperation::i2c_write && operation.address == 0x38 &&
            ((operation.value >> 8) == 7 || (operation.value >> 8) == 8)) {
            if (!pll_seen || verified != HW_VDF_ALL_ENGINES) return false;
            voltage_seen = true;
        }
    }
    return reset_seen && pll_seen && voltage_seen;
}

void check_real_adapter() {
    EmuTrace trace;
    std::unique_ptr<ChiaDriver, decltype(&stop_hw)> driver(init_hw(1100., 0.88, 0, "EMULATED"), stop_hw);
    require(driver != nullptr, "emulated init failed");
    for (unsigned engine = 0; engine < N_HW_VDFS; ++engine) {
        uint32_t control = 0;
        require(driver->RegRead(CHIA_VDF_CONTROL_REG_OFFSET + HW_VDF_ENGINE_STRIDE * engine, control) == 0 &&
                (control & (kReset | kClock)) == kReset, "real adapter did not leave every engine reset/gated");
    }
    require(valid_initialization_trace(trace.operations), "real adapter programmed settings before verified quiescence");
    auto reordered = trace.operations;
    reordered.insert(reordered.begin(), {EmuOperation::write, 0, 1});
    require(!valid_initialization_trace(reordered), "trace check missed an early PLL write");
    reordered = trace.operations;
    auto reset = std::find_if(reordered.begin(), reordered.end(), [](const EmuOperation &op) {
        return op.kind == EmuOperation::reset;
    });
    require(reset != reordered.end(), "trace fixture has no reset");
    reordered.insert(reset + 1, {EmuOperation::write, 0, 1});
    require(!valid_initialization_trace(reordered), "trace check missed a PLL write before readback");
    reordered = trace.operations;
    auto pll = std::find_if(reordered.begin(), reordered.end(), [](const EmuOperation &op) {
        return op.kind == EmuOperation::write && op.address < 0x20;
    });
    require(pll != reordered.end(), "trace fixture has no PLL write");
    reordered.insert(pll, {EmuOperation::reset, 0, 0});
    require(!valid_initialization_trace(reordered), "trace check reused readback from before a reset");
    require(driver->Reset(1000) == 0, "emulated board reset failed");
    for (unsigned engine = 0; engine < N_HW_VDFS; ++engine) {
        uint32_t control = ~0U;
        require(driver->RegRead(CHIA_VDF_CONTROL_REG_OFFSET + HW_VDF_ENGINE_STRIDE * engine, control) == 0 &&
                control == 0, "emulator reset preserved pre-reset control writes");
    }
}
} // namespace

int main() {
    try {
        check_order();
        check_failures();
        check_real_adapter();
        fprintf(stdout, "Hardware initialization tests passed\n");
        return 0;
    } catch (const std::exception &error) {
        fprintf(stderr, "Hardware initialization test failed: %s\n", error.what());
        return 1;
    }
}
