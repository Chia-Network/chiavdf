#pragma once
// Platform-independent operating-point transaction and control protocol.
#include "hw_util.hpp"
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace timelord_control {
struct Limits {
    static constexpr double min_frequency = 200., max_frequency = 2200.;
    static constexpr double min_voltage = .7, max_voltage = 1.;
    static constexpr double max_temperature = 55.;
    static constexpr double frequency_tolerance = 1., voltage_tolerance = .0063;
    static constexpr unsigned readback_attempts = 3;
    static constexpr uint64_t settle_us = 20000;
    static constexpr size_t frame_bytes = 256;
    static constexpr int failure_exit = 70;
};
struct Point {
    double frequency;
    double voltage;
};
inline bool readable(Point p) {
    return std::isfinite(p.frequency) && std::isfinite(p.voltage) && p.frequency > 0 &&
           p.voltage >= Limits::min_voltage && p.voltage <= Limits::max_voltage;
}
inline bool valid(Point p) {
    return readable(p) && p.frequency >= Limits::min_frequency &&
           p.frequency <= Limits::max_frequency;
}
inline void check_temperature(std::optional<double> temperature) {
    if (!temperature || !std::isfinite(*temperature))
        throw std::runtime_error("temperature_unavailable");
    if (*temperature >= Limits::max_temperature)
        throw std::runtime_error("temperature_limit");
}
// One ceiling rule for startup, live changes and capability advertising.
template <class Entry> size_t frequency_index(double target, const Entry *entries, size_t count) {
    size_t index = 0;
    while (index < count && entries[index].freq < target)
        ++index;
    return index;
}
template <class Entry> bool supported_frequency(double target, const Entry *entries, size_t count) {
    const auto index = frequency_index(target, entries, count);
    return index < count && std::abs(entries[index].freq - target) <= Limits::frequency_tolerance;
}
template <class Entry> void print_frequencies(const Entry *entries, size_t count) {
    bool first = true;
    fputs("[", stdout);
    for (int f = int(Limits::min_frequency); f <= int(Limits::max_frequency); f += 10) {
        if (!supported_frequency(f, entries, count))
            continue;
        printf("%s%d", first ? "" : ",", f);
        first = false;
    }
    puts("]");
}
struct Request {
    uint64_t id;
    Point target;
};
// Parsing is pure: sequence/replay checks belong to the channel.
inline std::optional<Request> parse_tlset(std::string_view line) {
    constexpr std::string_view prefix = "TLSET 1 ";
    if (line.size() > Limits::frame_bytes || line.substr(0, prefix.size()) != prefix ||
        line.find('\0') != line.npos || line.find('\n') != line.npos ||
        line.find('\r') != line.npos)
        return std::nullopt;
    line.remove_prefix(prefix.size());
    Request request{};
    const char *end = line.data() + line.size();
    auto id = std::from_chars(line.data(), end, request.id);
    if (id.ec != std::errc{} || !request.id || id.ptr == end || *id.ptr != ' ')
        return std::nullopt;
    std::string fields(id.ptr + 1, end);
    char *next = nullptr;
    errno = 0;
    request.target.frequency = std::strtod(fields.c_str(), &next);
    if (errno == ERANGE || next == fields.c_str() || *next != ' ')
        return std::nullopt;
    const char *voltage = next + 1;
    errno = 0;
    request.target.voltage = std::strtod(voltage, &next);
    if (errno == ERANGE || next == voltage || *next || !valid(request.target))
        return std::nullopt;
    return request;
}
// Read-only diagnostics are a separate snapshot, never a substitute for
// readback.
struct Register {
    const char *name;
    uint32_t address;
};
template <class ReadRegister>
std::string clock_registers(ReadRegister read, std::initializer_list<Register> registers) {
    std::string result;
    for (const auto &reg : registers) {
        uint32_t value = 0;
        const int error = read(reg.address, value);
        char field[96];
        if (error)
            snprintf(field, sizeof(field), "%s=error(%d)", reg.name, error);
        else
            snprintf(field, sizeof(field), "%s=0x%08x", reg.name, unsigned(value));
        if (!result.empty())
            result += " ";
        result += field;
    }
    return result;
}

template <class Hardware, class Accept>
Point checked_readback(const char *phase, Hardware &hardware, Accept accept) {
    for (unsigned attempt = 1; attempt <= Limits::readback_attempts; ++attempt) {
        const Point actual = hardware.read();
        if (accept(actual))
            return actual;
        const auto registers = hardware.diagnostics();
        fprintf(stderr,
                "Live control readback rejected phase=%s attempt=%u/%u "
                "frequency=%.6f voltage=%.6f registers=[%s]\n",
                phase, attempt, Limits::readback_attempts, actual.frequency, actual.voltage,
                registers.c_str());
        if (attempt == Limits::readback_attempts)
            throw std::runtime_error(std::string(phase) + " attempts=" + std::to_string(attempt) +
                                     " frequency=" + std::to_string(actual.frequency) +
                                     " voltage=" + std::to_string(actual.voltage) + " registers=[" +
                                     registers + "]");
        hardware.delay();
    }
    throw std::runtime_error("readback_unreachable");
}
// Both legacy auto-frequency and supervised control use this transaction.
// Legacy auto-frequency retains its existing full PLL-table range.
template <class Hardware>
Point apply(Point target, Hardware &hardware, uint8_t mask, bool supervised = true) {
    auto acceptable = [supervised](Point p) { return supervised ? valid(p) : readable(p); };
    if (!acceptable(target))
        throw std::runtime_error("limits");
    hardware.control(mask, false);
    const Point old = checked_readback("initial_readback", hardware, acceptable);
    if (target.voltage > old.voltage)
        hardware.voltage(target.voltage);
    hardware.frequency(target.frequency);
    if (target.voltage < old.voltage)
        hardware.voltage(target.voltage);
    const Point actual = checked_readback("readback", hardware, [&](Point p) {
        return acceptable(p) &&
               std::abs(p.frequency - target.frequency) <= Limits::frequency_tolerance &&
               std::abs(p.voltage - target.voltage) <= Limits::voltage_tolerance;
    });
    hardware.control(mask, true);
    return actual;
}
struct ShutdownResult {
    bool paused = false;
    bool restored = false;
};
// Failure recovery never resumes engines and never raises the clock. If USB is
// unavailable, report the failure rather than claim that shutdown was
// confirmed.
template <class Hardware> ShutdownResult shutdown(Hardware &hardware, uint8_t mask) noexcept {
    ShutdownResult result;
    try {
        hardware.control(mask, false);
        result.paused = true;
    } catch (...) {
    }
    if (!result.paused) {
        try {
            result.paused = hardware.reset();
        } catch (...) {
        }
    }
    try {
        hardware.frequency(Limits::min_frequency);
        hardware.voltage(Limits::min_voltage);
        checked_readback("safe_readback", hardware, [](Point p) {
            return valid(p) &&
                   std::abs(p.frequency - Limits::min_frequency) <= Limits::frequency_tolerance &&
                   std::abs(p.voltage - Limits::min_voltage) <= Limits::voltage_tolerance;
        });
        result.restored = true;
    } catch (...) {
        // The clock/voltage state is uncertain. Assert board reset as a last
        // resort.
        try {
            result.paused = hardware.reset() || result.paused;
        } catch (...) {
        }
    }
    return result;
}
// SIGPIPE is masked only on the writing thread and only for this write.
std::ptrdiff_t write_no_sigpipe(int fd, const void *data, size_t size);
class Channel {
    int input_, output_;
    std::string pending_;
    uint64_t last_id_ = 0;
    bool reply(uint64_t id, const char *error, Point actual = {});

  public:
    explicit Channel(int input = 0, int output = 1) : input_(input), output_(output) {}
    static bool supported();
    bool start();
    bool poll(const std::function<Point(Point)> &apply);
};
} // namespace timelord_control
