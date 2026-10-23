#pragma once
// Optional POSIX anonymous-pipe control on the client's USB owner thread.
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
namespace timelord_control {
struct Point {
    double frequency;
    double voltage;
};
inline bool valid(Point p) {
    return std::isfinite(p.frequency) && std::isfinite(p.voltage) && p.frequency >= 200 &&
           p.frequency <= 2200 && p.voltage >= .7 && p.voltage <= 1.;
}
// A missing sensor read must stop this client, but is not proof of overheating.
inline void check_temperature(double temperature) {
    if (!std::isfinite(temperature) || temperature <= 0.)
        throw std::runtime_error("temperature_unavailable");
    if (temperature >= 55.)
        throw std::runtime_error("temperature_limit");
}
// Return the same floor entry used for live PLL writes.
template <class Entry> size_t frequency_index(double target, const Entry *entries, size_t count) {
    size_t index = 0;
    while (index + 1 < count && entries[index + 1].freq <= target)
        ++index;
    return index;
}
template <class Entry> bool supported_frequency(double target, const Entry *entries, size_t count) {
    return count && std::abs(entries[frequency_index(target, entries, count)].freq - target) <= 1.;
}
template <class Entry> void print_frequencies(const Entry *entries, size_t count) {
    bool first = true;
    fputs("[", stdout);
    for (int f = 200; f <= 2200; f += 10) {
        if (!supported_frequency(f, entries, count))
            continue;
        printf("%s%d", first ? "" : ",", f);
        first = false;
    }
    puts("]");
}
// Read-only diagnostics are a separate snapshot, never a substitute for readback.
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
struct NoDiagnostics {
    std::string operator()() const { return {}; }
};
struct RetryDelay {
    void operator()() const { usleep(20000); }
};
constexpr unsigned READBACK_ATTEMPTS = 3;
template <class Read, class Accept, class Diagnostics, class Delay>
Point checked_readback(const char *phase, Read read, Accept accept, Diagnostics diagnostics,
                       Delay delay) {
    for (unsigned attempt = 1; attempt <= READBACK_ATTEMPTS; ++attempt) {
        const Point actual = read();
        if (accept(actual)) {
            if (attempt > 1)
                fprintf(stderr,
                        "Live control readback recovered phase=%s attempt=%u frequency=%.6f "
                        "voltage=%.6f\n",
                        phase, attempt, actual.frequency, actual.voltage);
            return actual;
        }
        const auto registers = diagnostics();
        fprintf(stderr,
                "Live control readback rejected phase=%s attempt=%u/%u frequency=%.6f voltage=%.6f "
                "registers=[%s]\n",
                phase, attempt, READBACK_ATTEMPTS, actual.frequency, actual.voltage,
                registers.c_str());
        if (attempt == READBACK_ATTEMPTS)
            throw std::runtime_error(std::string(phase) + " attempts=" + std::to_string(attempt) +
                                     " frequency=" + std::to_string(actual.frequency) +
                                     " voltage=" + std::to_string(actual.voltage) + " registers=[" +
                                     registers + "]");
        delay();
    }
    throw std::runtime_error("readback_unreachable");
}
// The caller must leave engines paused on any exception and terminate the client.
// Retry reads only: never repeat a PLL/voltage write or resume after failed readback.
template <class Read, class Voltage, class Frequency, class Pause,
          class Diagnostics = NoDiagnostics, class Delay = RetryDelay>
Point apply(Point target, Read read, Voltage voltage, Frequency frequency, Pause pause,
            Diagnostics diagnostics = {}, Delay delay = {}) {
    if (!valid(target))
        throw std::runtime_error("limits");
    pause(false);
    const Point old = checked_readback(
        "initial_readback", read, [](Point p) { return valid(p); }, diagnostics, delay);
    if (target.voltage > old.voltage)
        voltage(target.voltage);
    frequency(target.frequency);
    if (target.voltage < old.voltage)
        voltage(target.voltage);
    const Point actual = checked_readback(
        "readback", read,
        [&](Point p) {
            return valid(p) && std::abs(p.frequency - target.frequency) <= 1. &&
                   std::abs(p.voltage - target.voltage) <= .0063;
        },
        diagnostics, delay);
    pause(true);
    return actual;
}
class Channel {
    int input, output;
    std::string pending;
    uint64_t last_id = 0;

  public:
    Channel(int in = STDIN_FILENO, int out = STDOUT_FILENO) : input(in), output(out) {}
    bool start() {
        struct stat a{}, b{};
        if (fstat(input, &a) || fstat(output, &b) || !S_ISFIFO(a.st_mode) || !S_ISFIFO(b.st_mode))
            return false;
        // A parent disappearing during acknowledgement must return EPIPE so
        // the event-loop failure path can quiesce engines before exiting.
        if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR)
            return false;
        const int flags = fcntl(input, F_GETFL);
        const int output_flags = fcntl(output, F_GETFL);
        return flags >= 0 && output_flags >= 0 && !fcntl(input, F_SETFL, flags | O_NONBLOCK) &&
               !fcntl(output, F_SETFL, output_flags | O_NONBLOCK);
    }
    template <class Apply> bool poll(Apply apply) {
        // Bounded input and one command per event-loop turn. No network listener.
        char data[256];
        const ssize_t n = read(input, data, sizeof(data));
        if (!n)
            return false;
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            return false;
        if (n > 0)
            pending.append(data, static_cast<size_t>(n));
        const auto end = pending.find('\n');
        if (pending.size() > 256)
            return false;
        if (end == std::string::npos)
            return true;
        const auto line = pending.substr(0, end);
        pending.erase(0, end + 1);
        if (line.rfind("TLSET 1 ", 0) || line.size() <= 8 || line[8] < '0' || line[8] > '9' ||
            line.find('\0') != std::string::npos)
            return false;
        char *id_end = nullptr;
        errno = 0;
        const auto parsed_id = std::strtoull(line.c_str() + 8, &id_end, 10);
        if (errno == ERANGE || !id_end || *id_end != ' ')
            return false;
        unsigned long long id = 0;
        Point target{};
        char extra = 0;
        if (sscanf(line.c_str(), "TLSET 1 %llu %lf %lf %c", &id, &target.frequency, &target.voltage,
                   &extra) != 3 ||
            !id || id != parsed_id || id <= last_id || !valid(target))
            return false;
        last_id = id;
        try {
            const Point actual = apply(target);
            char response[128];
            const int size = snprintf(response, sizeof(response), "TLACK 1 %llu %.6f %.6f\n", id,
                                      actual.frequency, actual.voltage);
            return size > 0 && write(output, response, static_cast<size_t>(size)) == size;
        } catch (const std::exception &error) {
            // Carry the operation failure over the control pipe before closing it.
            // The coordinator may be awaiting this reply and unable to drain stderr.
            char response[512];
            const int size =
                snprintf(response, sizeof(response), "TLERR 1 %llu %.400s\n", id, error.what());
            if (size > 0 && static_cast<size_t>(size) < sizeof(response))
                (void)write(output, response, static_cast<size_t>(size));
            fprintf(stderr, "Live control rejected request %llu: %s\n", id, error.what());
            return false;
        } catch (...) {
            return false;
        }
    }
};
} // namespace timelord_control
