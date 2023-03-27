#include "hw_interface.hpp"
#include "hw_proof.hpp"
#include "hw_util.hpp"
#include "bqfc.h"
#include "vdf_base.hpp"

#include <arpa/inet.h>
#include <cstdio>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <unistd.h>

enum conn_state {
    WAITING,
    RUNNING,
    IDLING,
    STOPPED,
    CLOSED
};

struct vdf_conn {
    struct vdf_state vdf;
    int sock;
    char read_buf[512];
    uint32_t buf_pos;
    enum conn_state state;
};

struct vdf_client_opts {
    double freq;
    double voltage;
    uint32_t ip;
    int port;
    int n_vdfs;
    struct vdf_proof_opts vpo;
};

struct vdf_client {
    struct vdf_conn conns[N_HW_VDFS];
    struct vdf_value values[N_HW_VDFS];
    struct vdf_client_opts opts;
    ChiaDriver *drv;
};

struct vdf_proof_segm {
    uint8_t iters[sizeof(uint64_t)];
    uint8_t B[HW_VDF_B_SIZE];
    uint8_t proof[BQFC_FORM_SIZE];
};

static volatile bool g_stopping = false;

void signal_handler(int sig)
{
    LOG_INFO("Interrupted");
    g_stopping = true;
}

void init_conn(struct vdf_conn *conn, uint32_t ip, int port)
{
    int ret;
    struct sockaddr_in sa = { AF_INET, htons(port), { htonl(ip) } };
    conn->sock = socket(AF_INET, SOCK_STREAM, 0);
    LOG_INFO("Connecting to %s", inet_ntoa(sa.sin_addr));
    ret = connect(conn->sock, (struct sockaddr *)&sa, sizeof(sa));
    if (ret < 0) {
        perror("connect");
        throw std::runtime_error("Failed to connect");
    }

    ret = fcntl(conn->sock, F_SETFL, O_NONBLOCK);
    if (ret < 0) {
        perror("fcntl");
        throw std::runtime_error("Failed to set O_NONBLOCK");
    }
    conn->state = WAITING;
    conn->buf_pos = 0;
    LOG_INFO("VDF %d: Connected to timelord, waiting for challenge", conn->vdf.idx);
}

void init_vdf_client(struct vdf_client *client)
{
    for (uint8_t i = 0; i < client->opts.n_vdfs; i++) {
        client->conns[i].vdf.idx = i;
        client->conns[i].state = CLOSED;
        client->conns[i].sock = -1;
        memset(client->conns[i].read_buf, 0, sizeof(client->conns[i].read_buf));
        client->conns[i].buf_pos = 0;

        init_vdf_value(&client->values[i]);
    }
}

ssize_t read_data(struct vdf_conn *conn)
{
    ssize_t bytes = read(conn->sock, conn->read_buf + conn->buf_pos,
            sizeof(conn->read_buf) - conn->buf_pos);
    if (bytes < 0 && errno != EAGAIN) {
        perror("read");
        throw std::runtime_error("Read error");
    }
    if (bytes >= 0) {
        conn->buf_pos += bytes;
        return conn->buf_pos;
    }
    return bytes;
}

void write_data(struct vdf_conn *conn, const char *buf, size_t size)
{
    ssize_t bytes = write(conn->sock, buf, size);
    if (bytes < 0) {
        perror("write");
        throw std::runtime_error("Write error");
    }
}

void stop_conn(struct vdf_client *client, struct vdf_conn *conn)
{
    LOG_INFO("VDF %d: Stop requested", conn->vdf.idx);
    if (conn->sock >= 0) {
        write_data(conn, "STOP", 4);
    }
    if (conn->vdf.init_done) {
        hw_stop_proof(&conn->vdf);
        clear_vdf_state(&conn->vdf);
        stop_hw_vdf(client->drv, conn->vdf.idx);
    }
    conn->state = STOPPED;
    LOG_INFO("VDF %d: Stopped at iters=%lu", conn->vdf.idx, conn->vdf.cur_iters);
}

void handle_iters(struct vdf_client *client, struct vdf_conn *conn)
{
    char *buf = conn->read_buf;
    char iters_size_buf[3] = {0}, iters_buf[16];
    uint64_t iters;
    uint32_t bytes = conn->buf_pos, iters_size;

    while (bytes) {
        memcpy(iters_size_buf, buf, 2);
        iters_size = strtoul(iters_size_buf, NULL, 10);
        if (iters_size > sizeof(iters_buf) || bytes < 2 + iters_size) {
            LOG_ERROR("Bad iters data size: %u", bytes);
            throw std::runtime_error("Bad data size");
        }
        //buf[2 + iters_size] = '\0';
        memcpy(iters_buf, &buf[2], iters_size);
        iters_buf[iters_size] = '\0';
        iters = strtoul(iters_buf, NULL, 10);

        if (iters) {
            LOG_DEBUG("VDF %d: Requested proof for iters=%lu", conn->vdf.idx, iters);
            hw_request_proof(&conn->vdf, iters, false);
        } else {
            stop_conn(client, conn);
            bytes -= 2 + iters_size;
            break;
        }

        bytes -= 2 + iters_size;
        buf += 2 + iters_size;
    }
    if (!bytes) {
        conn->buf_pos = 0;
    }
    if (!conn->vdf.req_proofs.empty()) {
        size_t n_proofs = conn->vdf.req_proofs.size();
        char iters_str[100];
        size_t pos = 0;

        for (size_t i = 0; i < n_proofs; i++) {
            pos += snprintf(&iters_str[pos], sizeof(iters_str) - pos, "%s%lu",
                    i ? ", " : "", conn->vdf.req_proofs[i].iters);
            if (pos >= sizeof(iters_str) - 1) {
                break;
            }
        }
        LOG_INFO("VDF %d: Queued proofs for iters: [%s]", conn->vdf.idx, iters_str);
    }
}

void tl_enc_hex(char *out_data, uint8_t *data, size_t size)
{
    // Hex encode proof data for timelord
    for (size_t i = 0; i < size; i++) {
        snprintf(&out_data[i * 2], 3, "%02hhx", data[i]);
    }
}

void handle_proofs(struct vdf_client *client, struct vdf_conn *conn)
{
    struct vdf_proof *proof;
    int i;
    while ((i = hw_retrieve_proof(&conn->vdf, &proof)) >= 0) {
        uint8_t data[8 + 8 + 1 + BQFC_FORM_SIZE * 2];
        char tl_data[sizeof(data) * 2 + 5] = {0};

        LOG_INFO("VDF %d: Proof retrieved for iters=%lu", conn->vdf.idx, proof->iters);

        Int64ToBytes(&data[0], proof->iters);
        Int64ToBytes(&data[8], BQFC_FORM_SIZE);
        memcpy(&data[16], proof->y, BQFC_FORM_SIZE);
        data[16 + BQFC_FORM_SIZE] = i;
        memcpy(&data[17 + BQFC_FORM_SIZE], proof->proof, BQFC_FORM_SIZE);

        tl_enc_hex(&tl_data[4], data, sizeof(data));
        Int32ToBytes((uint8_t *)tl_data, (sizeof(data) + i * sizeof(vdf_proof_segm)) * 2);
        write_data(conn, tl_data, sizeof(tl_data) - 1);
        while (i) {
            struct vdf_proof_segm *segm = (struct vdf_proof_segm *)data;

            i--;
            //proof = conn->vdf.chkp_proofs[i];
            proof = &conn->vdf.proofs[proof->prev];
            Int64ToBytes(segm->iters, proof->seg_iters);
            memcpy(segm->B, proof->B, sizeof(proof->B));
            memcpy(segm->proof, proof->proof, sizeof(proof->proof));

            tl_enc_hex(tl_data, data, sizeof(*segm));
            write_data(conn, tl_data, sizeof(*segm) * 2);
        }
    }
}

void handle_conn(struct vdf_client *client, struct vdf_conn *conn)
{
    ssize_t bytes;
    char *buf = conn->read_buf;
    struct vdf_state *vdf = &conn->vdf;

    if (g_stopping && conn->state != CLOSED && conn->state != STOPPED) {
        stop_conn(client, conn);
    }

    if (conn->state == WAITING) {
        uint64_t d_size;
        char d_str[350];
        uint64_t n_iters = 200 * 1000 * 1000;
        uint8_t *init_form;

        bytes = read_data(conn);
        if (bytes < 5) {
            /* Expecting discr size and discriminant */
            return;
        }

        if (buf[0] != 'S' && buf[0] != 'N' && buf[0] != 'T') {
            throw std::runtime_error("Bad initial data from timelord");
        }

        d_size = strtoul(&buf[1], NULL, 10);
        if ((uint64_t)bytes < 4 + d_size + 1) {
            /* Expecting initial form after discriminant */
            return;
        }
        memcpy(d_str, &buf[4], d_size);
        d_str[d_size] = '\0';
        if ((uint64_t)bytes != 4 + d_size + 1 + buf[4 + d_size]) {
            LOG_ERROR("Bad data size: %zd", bytes);
            throw std::runtime_error("Bad data size");
        }

        init_form = (uint8_t *)&buf[4 + d_size + 1];
        init_vdf_state(vdf, &client->opts.vpo, d_str, init_form, n_iters, vdf->idx);
        start_hw_vdf(client->drv, vdf->d, vdf->last_val.a, vdf->last_val.b,
                vdf->target_iters, vdf->idx);
        write_data(conn, "OK", 2);
        conn->state = RUNNING;
        conn->buf_pos = 0;
        LOG_INFO("VDF %d: Received challenge, running", vdf->idx);
    } else if (conn->state == RUNNING || conn->state == IDLING) {
        handle_proofs(client, conn);
        bytes = read_data(conn);
        if (bytes < 0) {
            return;
        }

        handle_iters(client, conn);
    }
    if (conn->state == STOPPED) {
        bytes = read_data(conn);
        if (bytes == 3 && !memcmp(buf, "ACK", 3)) {
            close(conn->sock);
            conn->sock = -1;
            conn->state = CLOSED;
            LOG_INFO("VDF %d: Connection closed", vdf->idx);
        } else if (bytes >= 0) {
            LOG_ERROR("Bad data size after stop: %zd", bytes);
            throw std::runtime_error("Bad data size");
        }
    } else if (conn->state == CLOSED && !g_stopping) {
        init_conn(conn, client->opts.ip, client->opts.port);
    }
}

void event_loop(struct vdf_client *client)
{
    uint64_t loop_cnt = 0;
    uint32_t temp_period = chia_vdf_is_emu ? 200 : 20000;
    while(true) {
        uint8_t vdfs_mask = 0;
        uint8_t temp_flag = loop_cnt % temp_period ? 0 : HW_VDF_TEMP_FLAG;

        for (uint8_t i = 0; i < client->opts.n_vdfs; i++) {
            handle_conn(client, &client->conns[i]);
            if (client->conns[i].state == RUNNING) {
                vdfs_mask |= 1 << i;
            }
        }

        if (vdfs_mask) {
            read_hw_status(client->drv, vdfs_mask | temp_flag, client->values);
        } else if (g_stopping) {
            uint8_t n_closed = 0;
            for (uint8_t i = 0; i < client->opts.n_vdfs; i++) {
                if (client->conns[i].state == CLOSED) {
                    n_closed++;
                }
            }
            if (n_closed == client->opts.n_vdfs) {
                LOG_INFO("All VDFs stopped, exiting");
                break;
            }
        }

        for (uint8_t i = 0; i < client->opts.n_vdfs; i++) {
            if (vdfs_mask & (1 << i)) {
                hw_proof_add_value(&client->conns[i].vdf, &client->values[i]);
                if (client->conns[i].vdf.completed) {
                    stop_hw_vdf(client->drv, i);
                    client->conns[i].state = IDLING;
                }
            }
        }

        if (chia_vdf_is_emu) {
            usleep(50000);
        }
        loop_cnt++;
    }
}

int parse_opts(int argc, char **argv, struct vdf_client_opts *opts)
{
    const struct option long_opts[] = {
        {"freq", required_argument, NULL, 1},
        {"voltage", required_argument, NULL, 1},
        {"ip", required_argument, NULL, 1},
        {"vdf-threads", required_argument, NULL, 1},
        {0}
    };
    int long_idx = -1;
    int ret;

    opts->voltage = 0.8;
    opts->freq = 1100.0;
    opts->ip = INADDR_LOOPBACK;
    opts->port = 0;
    opts->n_vdfs = 3;
    opts->vpo.max_aux_threads = HW_VDF_DEFAULT_MAX_AUX_THREADS;
    opts->vpo.max_proof_threads = 0;

    while ((ret = getopt_long(argc, argv, "", long_opts, &long_idx)) == 1) {
        if (long_idx == 0) {
            opts->freq = strtod(optarg, NULL);
        } else if (long_idx == 1) {
            opts->voltage = strtod(optarg, NULL);
        } else if (long_idx == 2) {
            opts->ip = ntohl(inet_addr(optarg));
        } else if (long_idx == 3) {
            opts->vpo.max_aux_threads = strtoul(optarg, NULL, 0);
        }
    }
    if (ret != -1) {
        LOG_ERROR("Invalid option");
        return -1;
    }
    if (opts->voltage == 0.0 || opts->freq == 0.0) {
        LOG_ERROR("Invalid freq or voltage specified");
        return -1;
    }
    if (opts->ip == INADDR_NONE) {
        LOG_ERROR("Invalid IP address specified");
        return -1;
    }
    if (opts->vpo.max_aux_threads < 2 || opts->vpo.max_aux_threads > HW_VDF_MAX_AUX_THREADS) {
        LOG_ERROR("Number of VDF threads must be between 2 and %d",
                HW_VDF_MAX_AUX_THREADS);
        return -1;
    }

    if (optind == argc) {
        return -1;
    }
    opts->port = atoi(argv[optind]);
    if (argc > optind + 1) {
        opts->n_vdfs = atoi(argv[optind + 1]);
    }
    if (!opts->port || opts->n_vdfs < 1 || opts->n_vdfs > 3) {
        LOG_ERROR("Invalid port or VDF count");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    struct vdf_client client;
    struct sigaction sa = {0};

    if (parse_opts(argc, argv, &client.opts) < 0) {
        LOG_INFO("Usage: %s [--freq N] [--voltage N] PORT [N_VDFS]", argv[0]);
        return 1;
    }

    VdfBaseInit();
    client.drv = init_hw(client.opts.freq, client.opts.voltage);

    init_vdf_client(&client);

    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);

    event_loop(&client);

    stop_hw(client.drv);
    return 0;
}
