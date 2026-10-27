#include "timelord_telemetry.hpp"
#include <gtest/gtest.h>

#include <limits>
#include <cstring>

TEST(HardwareTelemetry, IdleTemperaturePollingKeepsFiveSecondDeadline) {
    using Poll = timelord_telemetry::Poll;
    const auto start = Poll::Clock::time_point{};
    Poll poll(true, 1, start);
    int samples = 0;
    for (int second = 0; second <= 180; ++second) {
        if (poll.due(false, start + std::chrono::seconds(second)))
            ++samples;
        EXPECT_EQ(samples, second / 5);
    }
    EXPECT_TRUE(poll.due(true, start + std::chrono::seconds(181)));
    EXPECT_FALSE(poll.due(false, start + std::chrono::seconds(185)));
    EXPECT_TRUE(poll.due(false, start + std::chrono::seconds(186)));
}

TEST(HardwareTelemetry, RawRatesResetAcrossIdleRollbackAndNewSessions) {
    timelord_telemetry::Speed speed;
    EXPECT_FALSE(speed.update({false, 0, 0, 0}));
    EXPECT_EQ(speed.update({true, 100, 100, 1}), -1.);
    EXPECT_FALSE(speed.update({true, 500100, 500100, 1}));
    EXPECT_EQ(speed.update({true, 1000100, 1000100, 1}), 1000000.);
    EXPECT_EQ(speed.update({true, 500, 2000100, 1}), -1.);
    EXPECT_EQ(speed.update({true, 600, 100, 1}), -1.);
    // Both counters can be larger after a challenge switch between polls.
    EXPECT_EQ(speed.update({true, 2000000, 3000000, 2}), -1.);
    EXPECT_EQ(speed.update({true, 3000000, 4000000, 2}), 1000000.);
    EXPECT_EQ(speed.update({false, 0, 0, 0}), -1.);
    EXPECT_FALSE(speed.update({false, 0, 0, 0}));
}

#include "timelord_live_control.hpp"
#include <vector>
#ifndef _WIN32
#include <csignal>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
using timelord_control::Point;
struct HardwareControlFixture {
    Point current{1450., .95};
    std::vector<std::string> actions;
    unsigned reads = 0, diagnostics_count = 0, waits = 0;
    bool fail_voltage = false, fail_frequency = false, fail_control = false, fail_reset = false;
    uint8_t selected_mask = 0;
    std::function<Point()> reader;
    Point read() {
        if (reader)
            return reader();
        ++reads;
        return current;
    }
    void control(uint8_t mask, bool resume) {
        selected_mask = mask;
        actions.push_back(resume ? "resume" : "pause");
        if (fail_control)
            throw std::runtime_error("engine_control");
    }
    void voltage(double value) {
        actions.push_back("voltage");
        if (fail_voltage)
            throw std::runtime_error("voltage_write");
        current.voltage = value;
    }
    void frequency(double value) {
        actions.push_back("frequency");
        if (fail_frequency)
            throw std::runtime_error("pll_write");
        current.frequency = value;
    }
    std::string diagnostics() {
        ++diagnostics_count;
        return "control=0x00000008";
    }
    void delay() { ++waits; }
    bool reset() {
        actions.push_back("reset");
        return !fail_reset;
    }
    Point apply(Point target) { return timelord_control::apply(target, *this, 7); }
    template <class Read> Point apply(Point target, Read read) {
        reader = read;
        return apply(target);
    }
};
#ifndef _WIN32
struct HardwareControlPipes {
    int input[2]{-1, -1}, output[2]{-1, -1};
    HardwareControlPipes() {
        if (pipe(input) || pipe(output))
            throw std::runtime_error("fixture pipe failed");
        if (fcntl(output[0], F_SETFL, O_NONBLOCK))
            throw std::runtime_error("fixture fcntl failed");
    }
    ~HardwareControlPipes() {
        for (int fd : {input[0], input[1], output[0], output[1]})
            if (fd >= 0)
                close(fd);
    }
    void send(const std::string &request) {
        ASSERT_EQ(write(input[1], request.data(), request.size()),
                  static_cast<ssize_t>(request.size()));
    }
    std::string reply() {
        char buffer[1024];
        const auto size = read(output[0], buffer, sizeof(buffer));
        return size > 0 ? std::string(buffer, static_cast<size_t>(size)) : std::string{};
    }
};
#endif
} // namespace

TEST(HardwareLiveControl, DistinguishesMissingSensorFromTemperatureLimit) {
    EXPECT_NO_THROW(timelord_control::check_temperature(40.));
    EXPECT_NO_THROW(timelord_control::check_temperature(0.));
    EXPECT_NO_THROW(timelord_control::check_temperature(-10.));
    EXPECT_THROW(timelord_control::check_temperature(std::nullopt), std::runtime_error);
    for (double value :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        try {
            timelord_control::check_temperature(value);
            FAIL() << "Missing temperature was accepted";
        } catch (const std::runtime_error &error) {
            EXPECT_STREQ(error.what(), "temperature_unavailable");
        }
    }
    for (double value : {55., 60.}) {
        try {
            timelord_control::check_temperature(value);
            FAIL() << "Temperature limit was accepted";
        } catch (const std::runtime_error &error) {
            EXPECT_STREQ(error.what(), "temperature_limit");
        }
    }
}

TEST(HardwareLiveControl, UsesTheSameCeilingRuleAsStartup) {
    struct Entry {
        double freq;
    };
    const Entry entries[]{{1490.}, {1500.}, {1511.111111}, {1520.},       {1525.}, {1533.333333},
                          {1540.}, {1560.}, {1566.666667}, {1571.428571}, {1580.}, {1600.}};
    EXPECT_EQ(entries[timelord_control::frequency_index(1510., entries, 12)].freq, 1511.111111);
    EXPECT_EQ(timelord_control::frequency_index(100., entries, 12), 0u);
    EXPECT_EQ(timelord_control::frequency_index(1601., entries, 12), 12u);
    EXPECT_TRUE(timelord_control::supported_frequency(1511., entries, 12));
    for (double frequency : {1510., 1530., 1570., 1590.})
        EXPECT_FALSE(timelord_control::supported_frequency(frequency, entries, 12));
    for (double frequency : {1500., 1520., 1540., 1560., 1580., 1600.})
        EXPECT_TRUE(timelord_control::supported_frequency(frequency, entries, 12));
    const Entry upper[]{{1600.}, {1620.}, {2200.}};
    EXPECT_TRUE(timelord_control::supported_frequency(2200., upper, 3));
    EXPECT_FALSE(timelord_control::supported_frequency(2190., upper, 3));
    EXPECT_FALSE(timelord_control::supported_frequency(2200., upper, 0));
}

TEST(HardwareLiveControl, OrdersVoltageAndClockWritesWhilePaused) {
    HardwareControlFixture fixture;
    fixture.apply({1460., 1.});
    EXPECT_EQ(fixture.actions,
              (std::vector<std::string>{"pause", "voltage", "frequency", "resume"}));
    fixture.actions.clear();
    fixture.apply({1440., .95});
    EXPECT_EQ(fixture.actions,
              (std::vector<std::string>{"pause", "frequency", "voltage", "resume"}));
    fixture.actions.clear();
    EXPECT_THROW(fixture.apply({2201., 1.}), std::runtime_error);
    EXPECT_THROW(fixture.apply({1450., 1.01}), std::runtime_error);
    EXPECT_TRUE(fixture.actions.empty());
}

TEST(HardwareLiveControl, InitialReadbackRetriesBeforeAnyWrite) {
    HardwareControlFixture fixture;
    fixture.current = {1900., 1.};
    const auto result = fixture.apply({1880., 1.}, [&] {
        EXPECT_EQ(fixture.actions.front(), "pause");
        if (++fixture.reads == 1) {
            EXPECT_EQ(fixture.actions.size(), 1u);
            return Point{100., 1.};
        }
        return fixture.current;
    });
    EXPECT_EQ(result.frequency, 1880.);
    EXPECT_EQ(fixture.reads, 3u);
    EXPECT_EQ(fixture.diagnostics_count, 1u);
    EXPECT_EQ(fixture.waits, 1u);
    EXPECT_EQ(fixture.actions, (std::vector<std::string>{"pause", "frequency", "resume"}));
}

TEST(HardwareLiveControl, PersistentInitialReadbackNeverWritesOrResumes) {
    for (Point bad : {Point{100., 1.}, Point{0., 1.}, Point{1900., 1.1},
                      Point{std::numeric_limits<double>::quiet_NaN(), 1.}}) {
        HardwareControlFixture fixture;
        try {
            fixture.apply({1880., 1.}, [&] {
                ++fixture.reads;
                return bad;
            });
            FAIL() << "Unexpected initial state was accepted";
        } catch (const std::runtime_error &error) {
            const std::string reason = error.what();
            EXPECT_NE(reason.find("initial_readback attempts=3"), std::string::npos);
            EXPECT_NE(reason.find("control=0x00000008"), std::string::npos);
        }
        EXPECT_EQ(fixture.reads, 3u);
        EXPECT_EQ(fixture.waits, 2u);
        EXPECT_EQ(fixture.actions, (std::vector<std::string>{"pause"}));
    }
}

TEST(HardwareLiveControl, FinalReadbackRetriesReadsWithoutRepeatingWrites) {
    HardwareControlFixture fixture;
    fixture.current = {1900., 1.};
    EXPECT_NO_THROW(fixture.apply(
        {1880., 1.}, [&] { return ++fixture.reads == 2 ? Point{100., 1.} : fixture.current; }));
    EXPECT_EQ(fixture.reads, 3u);
    EXPECT_EQ(fixture.waits, 1u);
    EXPECT_EQ(fixture.actions, (std::vector<std::string>{"pause", "frequency", "resume"}));
}

TEST(HardwareLiveControl, PersistentFinalReadbackNeverResumes) {
    HardwareControlFixture fixture;
    fixture.current = {1900., 1.};
    EXPECT_THROW(
        fixture.apply({1880., 1.},
                      [&] { return ++fixture.reads == 1 ? fixture.current : Point{1800., 1.}; }),
        std::runtime_error);
    EXPECT_EQ(fixture.reads, 4u);
    EXPECT_EQ(fixture.diagnostics_count, 3u);
    EXPECT_EQ(fixture.waits, 2u);
    EXPECT_EQ(fixture.actions, (std::vector<std::string>{"pause", "frequency"}));
}

TEST(HardwareLiveControl, HardwareWriteFailureNeverResumes) {
    HardwareControlFixture fixture;
    fixture.fail_voltage = true;
    EXPECT_THROW(fixture.apply({1460., 1.}), std::runtime_error);
    EXPECT_EQ(fixture.actions, (std::vector<std::string>{"pause", "voltage"}));
}

TEST(HardwareLiveControl, RegisterDiagnosticsDistinguishErrorsFromZero) {
    const auto registers = timelord_control::clock_registers(
        [](uint32_t address, uint32_t &value) {
            value = address == 0 ? 8 : 0;
            return address == 4 ? 17 : 0;
        },
        {{"control", 0}, {"status", 4}, {"pre_divide", 8}});
    EXPECT_EQ(registers, "control=0x00000008 status=error(17) pre_divide=0x00000000");
}

TEST(HardwareLiveControl, RejectsMalformedFramesBeforeHardwareWrites) {
    for (const auto &request :
         {"TLSET 1 -1 1450 1\n", "TLSET 1 1 1450 1.1\n", "TLSET 1 1 nan 1\n", "TLSET 1 1 2201 1\n",
          "TLSET 1 1 1450 1 extra\n", "TLSET 1 18446744073709551616 1450 1\n", "TLSET 2 1 1450 1\n",
          "TLSET 1 0 1450 1\n"}) {
        EXPECT_FALSE(
            timelord_control::parse_tlset(std::string(request).substr(0, strlen(request) - 1)));
    }
}

#ifndef _WIN32
TEST(HardwareLiveControl, FragmentedFramesAcknowledgeOnceAndRejectDuplicateIds) {
    HardwareControlPipes pipes;
    timelord_control::Channel channel(pipes.input[0], pipes.output[1]);
    ASSERT_TRUE(channel.start());
    int writes = 0;
    auto apply = [&](Point point) {
        ++writes;
        return point;
    };
    pipes.send("TLSET 1 1 1450");
    EXPECT_TRUE(channel.poll(apply));
    EXPECT_EQ(writes, 0);
    pipes.send(" 1\n");
    EXPECT_TRUE(channel.poll(apply));
    EXPECT_EQ(writes, 1);
    EXPECT_EQ(pipes.reply(), "TLACK 1 1 1450.000000 1.000000\n");
    pipes.send("TLSET 1 1 1450 1\n");
    EXPECT_FALSE(channel.poll(apply));
    EXPECT_EQ(writes, 1);
}

TEST(HardwareLiveControl, EofAndOversizedFramesStopWithoutApplying) {
    HardwareControlPipes pipes;
    timelord_control::Channel channel(pipes.input[0], pipes.output[1]);
    ASSERT_TRUE(channel.start());
    pipes.send(std::string(257, 'x'));
    int writes = 0;
    auto apply = [&](Point point) {
        ++writes;
        return point;
    };
    EXPECT_TRUE(channel.poll(apply)); // first bounded 256-byte read
    EXPECT_FALSE(channel.poll(apply));
    EXPECT_EQ(writes, 0);
    close(pipes.input[1]);
    pipes.input[1] = -1;
    EXPECT_FALSE(channel.poll(apply));
}

TEST(HardwareLiveControl, MissingParentAndFullReplyPipeDoNotBlockOwner) {
    HardwareControlPipes pipes;
    timelord_control::Channel channel(pipes.input[0], pipes.output[1]);
    ASSERT_TRUE(channel.start());
    ASSERT_NE(fcntl(pipes.output[1], F_GETFL) & O_NONBLOCK, 0);
    std::string fill(4096, 'x');
    size_t total = 0;
    while (write(pipes.output[1], fill.data(), fill.size()) > 0) {
        total += fill.size();
        ASSERT_LT(total, 16u * 1024 * 1024);
    }
    ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
    pipes.send("TLSET 1 1 1450 1\n");
    EXPECT_FALSE(channel.poll([](Point point) { return point; }));
    close(pipes.output[0]);
    pipes.output[0] = -1;
    pipes.send("TLSET 1 2 1450 1\n");
    EXPECT_FALSE(channel.poll([](Point point) { return point; }));
}

TEST(HardwareLiveControl, ReturnsBoundedHardwareFailureToParent) {
    HardwareControlPipes pipes;
    timelord_control::Channel channel(pipes.input[0], pipes.output[1]);
    ASSERT_TRUE(channel.start());
    pipes.send("TLSET 1 1 1450 1\n");
    EXPECT_FALSE(channel.poll([](Point) -> Point { throw std::runtime_error("pll_write"); }));
    EXPECT_EQ(pipes.reply(), "TLERR 1 1 pll_write\n");
}
#endif

TEST(HardwareLiveControl, PureParserPreservesFullRequestIdsAndRejectsTrailingData) {
    const auto request = timelord_control::parse_tlset("TLSET 1 18446744073709551615 1450 1");
    ASSERT_TRUE(request);
    EXPECT_EQ(request->id, UINT64_MAX);
    EXPECT_EQ(request->target.frequency, 1450.);
    for (const auto &line : {"TLSET 1 +1 1450 1", "TLSET 1 1 1450 1 tail", "TLSET 1 1 1450 inf",
                             "TLSET 1 1 1450 1e400", "TLSET 1 1 1450 1\n"})
        EXPECT_FALSE(timelord_control::parse_tlset(line));
}
#ifndef _WIN32
TEST(HardwareLiveControl, UnknownExceptionsReturnAnErrorReply) {
    HardwareControlPipes pipes;
    timelord_control::Channel channel(pipes.input[0], pipes.output[1]);
    ASSERT_TRUE(channel.start());
    pipes.send("TLSET 1 12 1450 1\n");
    EXPECT_FALSE(channel.poll([](Point) -> Point { throw 42; }));
    EXPECT_EQ(pipes.reply(), "TLERR 1 12 unknown\n");
}
TEST(HardwareLiveControl, BrokenPipesAndSocketsDoNotChangeTheProcessSignalHandler) {
    struct sigaction before{}, after{};
    ASSERT_EQ(sigaction(SIGPIPE, nullptr, &before), 0);
    HardwareControlPipes pipes;
    timelord_control::Channel channel(pipes.input[0], pipes.output[1]);
    ASSERT_TRUE(channel.start());
    close(pipes.output[0]);
    pipes.output[0] = -1;
    pipes.send("TLSET 1 1 1450 1\n");
    EXPECT_FALSE(channel.poll([](Point p) { return p; }));
    int sockets[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    close(sockets[1]);
    EXPECT_EQ(timelord_control::write_no_sigpipe(sockets[0], "x", 1), -1);
    EXPECT_EQ(errno, EPIPE);
    close(sockets[0]);
    ASSERT_EQ(sigaction(SIGPIPE, nullptr, &after), 0);
    EXPECT_EQ(before.sa_handler, after.sa_handler);
}
TEST(HardwareLiveControl, BrokenPipePreservesAnAlreadyPendingSignal) {
    sigset_t mask, previous, pending;
    sigemptyset(&mask);
    sigaddset(&mask, SIGPIPE);
    ASSERT_EQ(pthread_sigmask(SIG_BLOCK, &mask, &previous), 0);
    raise(SIGPIPE);
    int descriptors[2];
    ASSERT_EQ(pipe(descriptors), 0);
    close(descriptors[0]);
    EXPECT_EQ(timelord_control::write_no_sigpipe(descriptors[1], "x", 1), -1);
    sigpending(&pending);
    EXPECT_EQ(sigismember(&pending, SIGPIPE), 1);
    int signal;
    sigwait(&mask, &signal);
    close(descriptors[1]);
    EXPECT_EQ(pthread_sigmask(SIG_SETMASK, &previous, nullptr), 0);
}
#endif
TEST(HardwareLiveControl, FailureShutdownRestoresSafePointWithoutResuming) {
    HardwareControlFixture fixture;
    fixture.current = {1900., 1.};
    const auto result = timelord_control::shutdown(fixture, 5);
    EXPECT_TRUE(result.paused);
    EXPECT_TRUE(result.restored);
    EXPECT_EQ(fixture.selected_mask, 5);
    EXPECT_EQ(fixture.current.frequency, timelord_control::Limits::min_frequency);
    EXPECT_EQ(fixture.current.voltage, timelord_control::Limits::min_voltage);
    EXPECT_EQ(fixture.actions, (std::vector<std::string>{"pause", "frequency", "voltage"}));
}
TEST(HardwareLiveControl, FailureShutdownChecksPauseAndFallsBackToBoardReset) {
    HardwareControlFixture fixture;
    fixture.fail_control = true;
    const auto result = timelord_control::shutdown(fixture, 7);
    EXPECT_TRUE(result.paused);
    EXPECT_TRUE(result.restored);
    EXPECT_EQ(fixture.actions,
              (std::vector<std::string>{"pause", "reset", "frequency", "voltage"}));
}
TEST(HardwareLiveControl, FailureShutdownReportsUnconfirmedHardwareState) {
    HardwareControlFixture fixture;
    fixture.fail_control = fixture.fail_reset = fixture.fail_frequency = true;
    const auto result = timelord_control::shutdown(fixture, 7);
    EXPECT_FALSE(result.paused);
    EXPECT_FALSE(result.restored);
    EXPECT_EQ(fixture.actions, (std::vector<std::string>{"pause", "reset", "frequency", "reset"}));
}
TEST(HardwareTelemetry, IdleCadenceIgnoresIterationSchedulingAndLegacyPollingStaysUnchanged) {
    using Poll = timelord_telemetry::Poll;
    const auto start = Poll::Clock::time_point{};
    Poll idle(true, 1, start); // The iteration trigger fires on every turn.
    for (int millisecond = 0; millisecond < 5000; ++millisecond)
        EXPECT_FALSE(idle.due(false, start + std::chrono::milliseconds(millisecond)));
    EXPECT_TRUE(idle.due(false, start + std::chrono::seconds(5)));
    EXPECT_FALSE(idle.due(false, start + std::chrono::milliseconds(5001)));
    Poll legacy(false, 2, start);
    EXPECT_TRUE(legacy.due(true, start));
    EXPECT_FALSE(legacy.due(true, start));
    EXPECT_TRUE(legacy.due(true, start));
    EXPECT_FALSE(legacy.due(false, start + std::chrono::seconds(60)));
    EXPECT_FALSE(legacy.due(false, start + std::chrono::seconds(120)));
}

TEST(HardwareLiveControl, ControlPollingUsesTimeInsteadOfLoopTurns) {
    using Poll = timelord_control::Poll;
    const auto start = Poll::Clock::time_point{};
    Poll poll;
    EXPECT_TRUE(poll.due(start));
    // Tight real-hardware loops cannot consume the next poll early.
    for (int turn = 0; turn < 10000; ++turn)
        EXPECT_FALSE(poll.due(start));
    // With the emulator's 50 ms turns, poll at 100 ms rather than 256 turns.
    EXPECT_FALSE(poll.due(start + std::chrono::milliseconds(50)));
    EXPECT_TRUE(poll.due(start + std::chrono::milliseconds(100)));
    EXPECT_FALSE(poll.due(start + std::chrono::milliseconds(150)));
    EXPECT_TRUE(poll.due(start + std::chrono::milliseconds(200)));
    // A delayed owner schedules the next deadline from now, without a catch-up burst.
    EXPECT_TRUE(poll.due(start + std::chrono::seconds(13)));
    EXPECT_FALSE(poll.due(start + std::chrono::milliseconds(13001)));
    EXPECT_TRUE(poll.due(start + std::chrono::milliseconds(13100)));
}
#ifndef _WIN32
TEST(HardwareLiveControl, ChannelRestoresBorrowedDescriptorFlagsOnDestruction) {
    for (bool already_nonblocking : {false, true}) {
        HardwareControlPipes pipes;
        if (already_nonblocking)
            ASSERT_EQ(fcntl(pipes.input[0], F_SETFL, O_NONBLOCK), 0);
        const int input_flags = fcntl(pipes.input[0], F_GETFL);
        const int output_flags = fcntl(pipes.output[1], F_GETFL);
        const int input_alias = dup(pipes.input[0]), output_alias = dup(pipes.output[1]);
        ASSERT_GE(input_alias, 0);
        ASSERT_GE(output_alias, 0);
        {
            timelord_control::Channel channel(pipes.input[0], pipes.output[1]);
            ASSERT_TRUE(channel.start());
            ASSERT_TRUE(channel.start()); // Must retain the flags from the first start.
            EXPECT_NE(fcntl(input_alias, F_GETFL) & O_NONBLOCK, 0);
            EXPECT_NE(fcntl(output_alias, F_GETFL) & O_NONBLOCK, 0);
        }
        EXPECT_EQ(fcntl(input_alias, F_GETFL), input_flags);
        EXPECT_EQ(fcntl(output_alias, F_GETFL), output_flags);
        close(input_alias);
        close(output_alias);
    }
}
TEST(HardwareLiveControl, ChannelStopRestoresFlagsAndRejectedStartupLeavesInputAlone) {
    HardwareControlPipes pipes;
    const int input_flags = fcntl(pipes.input[0], F_GETFL);
    const int output_flags = fcntl(pipes.output[1], F_GETFL);
    timelord_control::Channel rejected(pipes.input[0], -1);
    EXPECT_FALSE(rejected.start());
    EXPECT_EQ(fcntl(pipes.input[0], F_GETFL), input_flags);
    timelord_control::Channel channel(pipes.input[0], pipes.output[1]);
    ASSERT_TRUE(channel.start());
    channel.stop();
    channel.stop();
    EXPECT_EQ(fcntl(pipes.input[0], F_GETFL), input_flags);
    EXPECT_EQ(fcntl(pipes.output[1], F_GETFL), output_flags);
}
#endif
