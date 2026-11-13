// Exercise the actual client loop with emulated USB and an intercepted control
// boundary. Real control pipes use isolated fixtures; no live node or USB hardware.
#define main hw_vdf_client_entry_for_test
#include "hw_vdf_client.cpp"
#undef main

#include "emu_runner.hpp"

#include <string>
#include <vector>

namespace {
struct EndClientLoop {};
uint8_t observed_running_mask = 0;
double observed_frequency = 0.;
bool standard_failure = false;
bool normal_shutdown = false;
bool real_pipe_control = false;
} // namespace

bool HardwareLiveControl::start(bool enabled, bool) {
    if (!real_pipe_control) return true;
    enabled_ = enabled;
    return !enabled_ || channel_.start();
}
bool HardwareLiveControl::poll(ChiaDriver *driver, uint8_t running_mask) {
    if (real_pipe_control) return channel_.poll([](timelord_control::Point p) { return p; });
    if (normal_shutdown) return true;
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

static void check_normal_supervised_shutdown_policy() {
#ifndef _WIN32
    char name[] = "emu_hw_vdf_client", live[] = "--live-control", port[] = "18110", count[] = "1";
    char *args[] = {name, live, port, count};
    // No connection is attempted: the loop starts with all connections CLOSED
    // and a pending normal termination request. USB is fully emulated.
    for (bool stuck : {false, true}) {
        g_stopping = true; normal_shutdown = true; emu_i2c_reads_zero = stuck;
        const int rc = hw_vdf_client_main(4,args);
        g_stopping = false; normal_shutdown = false; emu_i2c_reads_zero = false;
        ChiaDriver probe;
        if (rc != (stuck ? timelord_control::Limits::failure_exit : 0) || probe.ftdi.Open() ||
            std::abs(probe.GetPLLFrequency()-timelord_control::Limits::min_frequency)>1.)
            throw std::runtime_error("normal_shutdown_exit_policy");
    }
#endif
}

#ifndef _WIN32
#include <poll.h>
#include <sys/wait.h>
#endif
static void check_pipe_shutdown_through_main() {
#ifndef _WIN32
    // Only the operating-point command boundary is intercepted. TLSTOP, ACK
    // framing, the real signal handler and main's exit policy run unchanged.
    struct Listener {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        ~Listener() { if (fd >= 0) close(fd); }
    } listener;
    sockaddr_in address{};
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof(address);
    if (listener.fd < 0 || bind(listener.fd, reinterpret_cast<sockaddr *>(&address), size) ||
        listen(listener.fd, 8) || getsockname(listener.fd, reinterpret_cast<sockaddr *>(&address), &size))
        throw std::runtime_error("shutdown_fixture_listener");
    std::string port = std::to_string(ntohs(address.sin_port));
    struct Case { const char *stop; bool stuck; const char *ack; int rc; };
    const Case cases[] = {
        {"TLSTOP 1 7\n", false, "TLSTOPACK 1 7 1 1\n", 0},
        {"TLSTOP 1 7\n", true, "TLSTOPACK 1 7 1 0\n", timelord_control::Limits::failure_exit},
        {"TLSTOP 1 5\n", false, "TLSTOPACK 1 0 1 1\n", timelord_control::Limits::failure_exit},
        {nullptr, false, "TLSTOPACK 1 0 1 1\n", 0}, // SIGTERM
        {nullptr, true, "TLSTOPACK 1 0 1 0\n", timelord_control::Limits::failure_exit},
    };
    for (const Case &c : cases) {
        int input[2], output[2];
        if (pipe(input) || pipe(output)) throw std::runtime_error("shutdown_fixture_pipe");
        const pid_t pid = fork();
        if (pid < 0) throw std::runtime_error("shutdown_fixture_fork");
        if (!pid) {
            close(input[1]); close(output[0]);
            if (dup2(input[0], STDIN_FILENO) < 0 || dup2(output[1], STDOUT_FILENO) < 0) _exit(99);
            close(input[0]); close(output[1]);
            close(listener.fd);
            real_pipe_control = true; g_stopping = false; emu_i2c_reads_zero = c.stuck;
            char name[] = "emu_hw_vdf_client", live[] = "--live-control", count[] = "1";
            char *args[] = {name, live, port.data(), count};
            _exit(hw_vdf_client_main(4,args));
        }
        close(input[0]); close(output[1]);
        const auto receive = [&]() {
            std::string line;
            while (line.size() < 128) {
                pollfd ready{output[0], POLLIN, 0};
                if (::poll(&ready, 1, 5000) <= 0) throw std::runtime_error("shutdown_fixture_timeout");
                char byte;
                if (read(output[0], &byte, 1) != 1) throw std::runtime_error("shutdown_fixture_eof");
                line += byte;
                if (byte == '\n') return line;
            }
            throw std::runtime_error("shutdown_fixture_oversized_ack");
        };
        bool reaped = false;
        try {
            const char barrier[] = "TLSET 1 5 1450 1\n";
            if (timelord_control::write_no_sigpipe(input[1], barrier, sizeof(barrier)-1) != ssize_t(sizeof(barrier)-1)) throw std::runtime_error("shutdown_fixture_write");
            const std::string ready = receive();
            if (ready != "TLACK 1 5 1450.000000 1.000000\n") throw std::runtime_error("shutdown_fixture_barrier: " + ready);
            if (!c.stop) {
                kill(pid, SIGTERM);
            } else if (timelord_control::write_no_sigpipe(input[1], c.stop, strlen(c.stop)) != ssize_t(strlen(c.stop))) {
                throw std::runtime_error("shutdown_fixture_stop");
            }
            if (receive() != c.ack) throw std::runtime_error("shutdown_fixture_hardware_flags");
            // Bound process cleanup independently from the ACK deadline.
            int status = 0;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            pid_t waited;
            while ((waited = waitpid(pid, &status, WNOHANG)) == 0) {
                if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("shutdown_fixture_cleanup_timeout");
                usleep(1000);
            }
            if (waited != pid) throw std::runtime_error("shutdown_fixture_waitpid");
            reaped = true;
            if (!WIFEXITED(status) || WEXITSTATUS(status) != c.rc)
                throw std::runtime_error("shutdown_exit_policy");
        } catch (...) {
            if (!reaped) { kill(pid, SIGKILL); waitpid(pid, nullptr, 0); }
            close(input[1]); close(output[0]);
            throw;
        }
        close(input[1]); close(output[0]);
    }
#endif
}

#ifndef _WIN32
static int identity_listener(sockaddr_in &address) {
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    address = {}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof(address);
    if (listener < 0 || bind(listener, reinterpret_cast<sockaddr *>(&address), size) || listen(listener, 3) ||
        getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size)) throw std::runtime_error("identity_listener");
    return listener;
}

static int accept_engine(int listener) {
    pollfd ready{listener, POLLIN, 0};
    const int peer = ::poll(&ready, 1, 5000) > 0 ? accept(listener, nullptr, nullptr) : -1;
    if (peer < 0) throw std::runtime_error("identity_accept");
    return peer;
}

// Returns everything the client sent before going quiet for 100 ms.
static std::string engine_preamble(int peer) {
    std::string sent;
    char buf[64];
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (pollfd ready{peer, POLLIN, 0}; ::poll(&ready, 1, 100) > 0;) {
        if (sent.size() >= 128 || std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("identity_preamble_limit");
        const auto count = recv(peer, buf, sizeof(buf), 0);
        if (count <= 0) break;
        sent.append(buf, size_t(count));
    }
    return sent;
}
#endif

static void check_physical_engine_identity_on_reconnect() {
#ifndef _WIN32
    sockaddr_in address;
    const int listener = identity_listener(address);
    vdf_client client{};
    client.opts.ip = INADDR_LOOPBACK; client.opts.port = ntohs(address.sin_port);
    for (auto &conn : client.conns) conn.state = CLOSED;
    for (int i = 0; i < 3; i++) client.conns[i].vdf.idx = uint8_t(i);
    for (bool identity : {false, true, true}) { // the second identity round reconnects the same sockets
        client.opts.engine_identity = identity;
        for (unsigned engine : {2u, 0u, 1u}) {
            auto &conn = client.conns[engine];
            handle_conn(&client, &conn);
            const int peer = accept_engine(listener);
            const std::string expected = identity ? "TLENGINE 1 " + std::to_string(engine) + "\n" : "";
            if (engine_preamble(peer) != expected) throw std::runtime_error("physical_engine_identity");
            close(peer); close_conn(&conn);
        }
    }
    close(listener);
#endif
}

// A supervisor may drop a reserved engine without reading its identity. The
// unread bytes make the kernel reset the socket; only that engine reconnects.
static void check_engine_reset_after_identity() {
#ifndef _WIN32
    sockaddr_in address;
    const int listener = identity_listener(address);
    vdf_client client{};
    client.opts.ip = INADDR_LOOPBACK; client.opts.port = ntohs(address.sin_port);
    client.opts.engine_identity = true;
    auto &conn = client.conns[1]; conn.vdf.idx = 1; conn.state = CLOSED;
    handle_conn(&client, &conn);
    pollfd sent{accept_engine(listener), POLLIN, 0};
    if (::poll(&sent, 1, 5000) <= 0) throw std::runtime_error("identity_not_sent");
    close(sent.fd);
    for (int i = 0; i < 2 && conn.state != CLOSED; i++) {
        ::poll(nullptr, 0, 50);
        handle_conn(&client, &conn);
    }
    if (conn.state != CLOSED || conn.sock != kInvalidSocket) throw std::runtime_error("reset_identity_engine_kept");
    close(listener);
#endif
}

static void check_engine_identity_option() {
    vdf_client_opts opts{};
    if (parse_segment_args({"prog", "5000"}, opts) || opts.engine_identity ||
        parse_segment_args({"prog", "--engine-identity", "5000"}, opts) || !opts.engine_identity ||
        parse_segment_args({"prog", "--engine-identity=1", "5000"}, opts) != -1)
        throw std::runtime_error("engine_identity_option");
}

static void check_engine_identity_write_failures() {
#ifndef _WIN32
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) throw std::runtime_error("identity_socketpair");
    vdf_conn conn{}; conn.sock = sockets[0]; conn.vdf.idx = 2;
    close(sockets[1]);
    if (write_engine_identity(&conn)) throw std::runtime_error("closed_identity_socket_accepted");
    close(sockets[0]);
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) throw std::runtime_error("identity_socketpair");
    conn.sock = sockets[0];
    if (vdf_socket_set_nonblock(conn.sock)) throw std::runtime_error("identity_nonblock");
    char fill[4096]{};
    while (vdf_socket_write(conn.sock, fill, sizeof(fill)) > 0) {}
    if (!vdf_socket_would_block() || write_engine_identity(&conn)) throw std::runtime_error("full_identity_socket_accepted");
    close(sockets[0]); close(sockets[1]);
#endif
}

static void check_failed_connect_releases_socket() {
#ifndef _WIN32
    const int reserved = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof(address);
    // A bound, non-listening socket reserves this private port and refuses
    // connects without relying on an unused production/development port.
    if (reserved < 0 || bind(reserved, reinterpret_cast<sockaddr *>(&address), size) ||
        getsockname(reserved, reinterpret_cast<sockaddr *>(&address), &size))
        throw std::runtime_error("failed_connect_fixture");
    // init_conn's socket() takes the lowest free descriptor, which is this one.
    const int next = dup(reserved);
    if (next < 0) throw std::runtime_error("descriptor_fixture_dup");
    close(next);
    vdf_conn conn{}; conn.state = CLOSED;
    init_conn(&conn, INADDR_LOOPBACK, ntohs(address.sin_port), false);
    close(reserved);
    if (conn.sock != kInvalidSocket || conn.state != CLOSED || fcntl(next, F_GETFD) != -1 || errno != EBADF)
        throw std::runtime_error("failed_connect_retained_descriptor");
#endif
}
