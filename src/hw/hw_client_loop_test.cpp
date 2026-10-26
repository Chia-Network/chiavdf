// Exercise the actual client loop with emulated USB and an intercepted control
// boundary. No node, control pipe, hardware, or production-only test hooks.
#define main hw_vdf_client_entry_for_test
#include "hw_vdf_client.cpp"
#undef main

namespace {
struct EndClientLoop {};
uint8_t observed_running_mask = 0;
} // namespace

bool HardwareLiveControl::start(bool, bool) { return true; }
bool HardwareLiveControl::poll(ChiaDriver *, uint8_t running_mask) {
    observed_running_mask = running_mask;
    throw EndClientLoop{};
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
