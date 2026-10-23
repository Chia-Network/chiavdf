#include "timelord_telemetry.hpp"
#include <gtest/gtest.h>

#include <limits>

TEST(HardwareTelemetry, IdleTemperaturePollingKeepsFiveSecondDeadline) {
    using Poll = timelord_telemetry::Poll;
    const auto start = Poll::Clock::time_point{};
    Poll poll(start);
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
    EXPECT_FALSE(speed.update(false, 0, 0, 0));
    EXPECT_EQ(speed.update(true, 100, 100, 1), -1.);
    EXPECT_FALSE(speed.update(true, 500100, 500100, 1));
    EXPECT_EQ(speed.update(true, 1000100, 1000100, 1), 1000000.);
    EXPECT_EQ(speed.update(true, 500, 2000100, 1), -1.);
    EXPECT_EQ(speed.update(true, 600, 100, 1), -1.);
    // Both counters can be larger after a challenge switch between polls.
    EXPECT_EQ(speed.update(true, 2000000, 3000000, 2), -1.);
    EXPECT_EQ(speed.update(true, 3000000, 4000000, 2), 1000000.);
    EXPECT_EQ(speed.update(false, 0, 0, 0), -1.);
    EXPECT_FALSE(speed.update(false, 0, 0, 0));
}

#ifndef _WIN32
#include "timelord_live_control.hpp"
#include <vector>

namespace {
using timelord_control::Point;
struct HardwareControlFixture {
    Point current{1450., .95};
    std::vector<std::string> actions;
    unsigned reads = 0, diagnostics = 0, waits = 0;
    template <class Read> Point apply(Point target, Read read) {
        return timelord_control::apply(
            target, read,
            [&](double voltage) {
                actions.push_back("voltage");
                current.voltage = voltage;
            },
            [&](double frequency) {
                actions.push_back("frequency");
                current.frequency = frequency;
            },
            [&](bool resume) { actions.push_back(resume ? "resume" : "pause"); },
            [&] {
                ++diagnostics;
                return std::string("control=0x00000008");
            },
            [&] { ++waits; });
    }
    Point apply(Point target) {
        return apply(target, [&] {
            ++reads;
            return current;
        });
    }
};
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
} // namespace

TEST(HardwareLiveControl, DistinguishesMissingSensorFromTemperatureLimit) {
    EXPECT_NO_THROW(timelord_control::check_temperature(40.));
    for (double value : {0., -1., std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity()}) {
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

TEST(HardwareLiveControl, AdvertisesOnlySupportedFloorPllEntries) {
    struct Entry {
        double freq;
    };
    const Entry entries[]{{1490.}, {1500.}, {1511.111111}, {1520.},       {1525.}, {1533.333333},
                          {1540.}, {1560.}, {1566.666667}, {1571.428571}, {1580.}, {1600.}};
    EXPECT_EQ(entries[timelord_control::frequency_index(1510., entries, 12)].freq, 1500.);
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
    EXPECT_EQ(fixture.diagnostics, 1u);
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
    EXPECT_EQ(fixture.diagnostics, 3u);
    EXPECT_EQ(fixture.waits, 2u);
    EXPECT_EQ(fixture.actions, (std::vector<std::string>{"pause", "frequency"}));
}

TEST(HardwareLiveControl, HardwareWriteFailureNeverResumes) {
    std::vector<bool> pauses;
    EXPECT_THROW(timelord_control::apply(
                     Point{1460., 1.}, [] { return Point{1450., .95}; },
                     [](double) { throw std::runtime_error("write failed"); }, [](double) {},
                     [&](bool resume) { pauses.push_back(resume); }),
                 std::runtime_error);
    EXPECT_EQ(pauses, std::vector<bool>{false});
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
        HardwareControlPipes pipes;
        timelord_control::Channel channel(pipes.input[0], pipes.output[1]);
        ASSERT_TRUE(channel.start());
        pipes.send(request);
        int writes = 0;
        EXPECT_FALSE(channel.poll([&](Point point) {
            ++writes;
            return point;
        }));
        EXPECT_EQ(writes, 0);
    }
}

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
