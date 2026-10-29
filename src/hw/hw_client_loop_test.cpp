// Exercise the actual client loop with emulated USB and an intercepted control
// boundary. No node, control pipe, hardware, or production-only test hooks.
#define main hw_vdf_client_entry_for_test
#include "hw_vdf_client.cpp"
#undef main

#include <string>
#include <vector>

namespace {
struct EndClientLoop {};
uint8_t observed_running_mask = 0;
double observed_frequency = 0.;
bool standard_failure = false;
} // namespace

bool HardwareLiveControl::start(bool, bool) { return true; }
bool HardwareLiveControl::poll(ChiaDriver *driver, uint8_t running_mask) {
    observed_running_mask = running_mask;
    observed_frequency = driver->GetPLLFrequency();
    if (standard_failure)
        throw std::runtime_error("fixture_exception");
    throw EndClientLoop{};
}

static int parse_segment_args(std::vector<std::string> args, vdf_client_opts &opts) {
    std::vector<char *> argv;
    argv.reserve(args.size());
    for (auto &arg : args)
        argv.push_back(&arg[0]);
    return parse_opts(static_cast<int>(argv.size()), argv.data(), &opts);
}

static void check_segment_threads_option() {
    const std::vector<std::string> base = {
        "prog", "--freq", "1100", "--voltage", "0.88"
    };
    {
        vdf_client_opts opts{};
        auto args = base;
        args.push_back("5000");
        if (parse_segment_args(std::move(args), opts) != 0
                || opts.vpo.segment_threads != 2)
            throw std::runtime_error("segment_threads_default");
    }
    struct Accepted {
        const char *option;
        const char *value;
        uint8_t expected;
    };
    const Accepted accepted[] = {
        {"--segment-threads", "1", 1},
        {"--segment-threads", "3", 3},
        {"--segment-threads", "8", 8},
        {"--segment-threads=1", nullptr, 1},
        {"--segment-threads=8", nullptr, 8},
    };
    for (const Accepted &c : accepted) {
        vdf_client_opts opts{};
        auto args = base;
        args.push_back(c.option);
        if (c.value) args.push_back(c.value);
        args.push_back("5000");
        if (parse_segment_args(std::move(args), opts) != 0)
            throw std::runtime_error("segment_threads_accepted_value_rejected");
        if (opts.vpo.segment_threads != c.expected)
            throw std::runtime_error("segment_threads_value_not_stored");
    }
    const char *rejected[] = {
        "0", "9", "255", "256", "257", "4294967297",
        "-1", "+3", "", "abc", "3x", " 3"
    };
    for (const char *value : rejected) {
        vdf_client_opts opts{};
        auto args = base;
        args.push_back("--segment-threads");
        args.push_back(value);
        args.push_back("5000");
        if (parse_segment_args(std::move(args), opts) != -1)
            throw std::runtime_error("segment_threads_invalid_value_accepted");
    }
    {
        vdf_client_opts opts{};
        auto args = base;
        args.push_back("5000");
        args.push_back("--segment-threads");
        if (parse_segment_args(std::move(args), opts) != -1)
            throw std::runtime_error("segment_threads_missing_value_accepted");
    }
}

static void check_completed_engine_control_mask() {
#ifndef _WIN32
    // Include a physical-engine-1-only board, all engines completing together,
    // and each mixed case where healthy peers must continue running.
    for (const uint8_t selected : {uint8_t(1), uint8_t(2), uint8_t(7)}) {
        for (uint8_t completed = 0; completed <= selected; ++completed) {
            if (completed & ~selected)
                continue;
            ChiaDriver driver;
            if (driver.ftdi.Open() != 0)
                throw std::runtime_error("emulator_open");
            driver.freq_idx = 0;
            vdf_client client{};
            client.drv = &driver;
            client.opts.vdfs_mask = selected;
            struct Cleanup {
                vdf_client &client;
                int peers[N_HW_VDFS]{-1, -1, -1};
                bool initialized[N_HW_VDFS]{};
                ~Cleanup() {
                    for (int i = 0; i < N_HW_VDFS; ++i) {
                        if (peers[i] >= 0) {
                            close(peers[i]);
                            close(client.conns[i].sock);
                        }
                        if (initialized[i]) {
                            hw_stop_proof(&client.conns[i].vdf);
                            clear_vdf_state(&client.conns[i].vdf);
                            clear_vdf_value(&client.values[i]);
                        }
                    }
                }
            } cleanup{client};
            for (int i = 0; i < N_HW_VDFS; ++i) {
                client.conns[i].state = CLOSED;
                if (!(selected & (1 << i)))
                    continue;
                int sockets[2];
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets))
                    throw std::runtime_error("fixture_socketpair");
                client.conns[i].sock = sockets[0];
                cleanup.peers[i] = sockets[1];
                if (vdf_socket_set_nonblock(sockets[0]))
                    throw std::runtime_error("fixture_nonblock");
                uint8_t initial[BQFC_FORM_SIZE] = {8};
                init_vdf_state(&client.conns[i].vdf, nullptr, "-23", initial, 4000, i);
                init_vdf_value(&client.values[i]);
                cleanup.initialized[i] = true;
                // Workers may publish completion before this loop turn consumes
                // it. The control callback must see the state after consumption.
                client.conns[i].state = RUNNING;
                client.conns[i].vdf.completed = completed & (1 << i);
            }
            HardwareLiveControl control;
            try {
                event_loop(&client, control);
                throw std::runtime_error("client_loop_did_not_reach_control");
            } catch (const EndClientLoop &) {
            }
            if (observed_running_mask != (selected & ~completed))
                throw std::runtime_error("control_received_completed_engine");
            for (int i = 0; i < N_HW_VDFS; ++i) {
                if (!(selected & (1 << i)))
                    continue;
                const auto expected = completed & (1 << i) ? IDLING : RUNNING;
                if (client.conns[i].state != expected)
                    throw std::runtime_error("unexpected_engine_state");
            }
        }
    }
#endif
}

// Drive both catch branches through the actual CLI entry point. Only the
// supervisor boundary is intercepted; startup, failure policy and cleanup are real.
static void check_client_exception_shutdown_policy() {
#ifndef _WIN32
    struct Listener {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        ~Listener() { if (fd >= 0) close(fd); }
    } listener;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof(address);
    if (listener.fd < 0 || bind(listener.fd, reinterpret_cast<sockaddr *>(&address), size) ||
        listen(listener.fd, 8) ||
        getsockname(listener.fd, reinterpret_cast<sockaddr *>(&address), &size))
        throw std::runtime_error("exception_fixture_listener");
    std::string port = std::to_string(ntohs(address.sin_port));
    for (bool supervised : {false, true}) {
        for (bool standard : {false, true}) {
            standard_failure = standard;
            char name[] = "emu_hw_vdf_client", live[] = "--live-control", count[] = "1";
            char *legacy_args[] = {name, port.data(), count};
            char *live_args[] = {name, live, port.data(), count};
            const int rc = hw_vdf_client_main(supervised ? 4 : 3,
                                              supervised ? live_args : legacy_args);
            standard_failure = false;
            if (rc != (supervised ? timelord_control::Limits::failure_exit : 1))
                throw std::runtime_error("client_exception_exit_status");
            ChiaDriver probe;
            if (probe.ftdi.Open())
                throw std::runtime_error("exception_fixture_probe");
            const double expected = supervised ? timelord_control::Limits::min_frequency
                                               : observed_frequency;
            if (std::abs(probe.GetPLLFrequency() - expected) > 1.)
                throw std::runtime_error("ordinary_client_changed_clock_on_exception");
            if (!supervised && observed_frequency <= timelord_control::Limits::min_frequency)
                throw std::runtime_error("exception_fixture_did_not_start_above_safe_clock");
        }
    }
#endif
}
