#include "hw_interface.hpp"
#include "hw_proof.hpp"
#include "hw_util.hpp"
#include "vdf_base.hpp"

#include <cstdio>
#include <fcntl.h>
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
    enum conn_state state;
};

struct vdf_client {
    struct vdf_conn conns[N_HW_VDFS];
    struct vdf_value values[N_HW_VDFS];
    ChiaDriver *drv;
    int port;
    uint8_t n_vdfs;
};


void init_conn(struct vdf_conn *conn, int port)
{
    int ret;
    struct sockaddr_in sa = { AF_INET, htons(port), { htonl(INADDR_LOOPBACK) } };
    conn->sock = socket(AF_INET, SOCK_STREAM, 0);
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
    LOG_INFO("VDF %d: Connected to timelord, waiting for challenge", conn->vdf.idx);
}

void init_vdf_client(struct vdf_client *client)
{
    for (uint8_t i = 0; i < client->n_vdfs; i++) {
        client->conns[i].vdf.idx = i;
        client->conns[i].state = CLOSED;
        client->conns[i].sock = -1;
        memset(client->conns[i].read_buf, 0, sizeof(client->conns[i].read_buf));

        init_vdf_value(&client->values[i]);
    }
}

ssize_t read_data(struct vdf_conn *conn)
{
    ssize_t bytes = read(conn->sock, conn->read_buf, sizeof(conn->read_buf));
    if (bytes < 0 && errno != EAGAIN) {
        perror("read");
        throw std::runtime_error("Read error");
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

void handle_conn(struct vdf_client *client, struct vdf_conn *conn)
{
    ssize_t bytes;
    char *buf = conn->read_buf;

    if (conn->state == WAITING) {
        uint64_t d_size;
        char d_str[350];
        uint64_t n_iters = 200 * 1000 * 1000;
        uint8_t *init_form;

        bytes = read_data(conn);
        if (bytes < 0) {
            return;
        }

        if (buf[0] != 'S' && buf[0] != 'N' && buf[0] != 'T') {
            throw std::runtime_error("Bad initial data from timelord");
        }

        d_size = strtoul(&buf[1], NULL, 10);
        memcpy(d_str, &buf[4], d_size);
        d_str[d_size] = '\0';
        if ((uint64_t)bytes != 4 + d_size + 1 + buf[4 + d_size]) {
            LOG_ERROR("Bad data size: %zd", bytes);
            throw std::runtime_error("Bad data size");
        }

        init_form = (uint8_t *)&buf[4 + d_size + 1];
        init_vdf_state(&conn->vdf, d_str, init_form, n_iters, conn->vdf.idx);
        start_hw_vdf(client->drv, conn->vdf.d, conn->vdf.target_iters, conn->vdf.idx);
        write_data(conn, "OK", 2);
        conn->state = RUNNING;
        LOG_INFO("VDF %d: Received challenge, running", conn->vdf.idx);
    } else if (conn->state == RUNNING || conn->state == IDLING) {
        char iters_size_buf[3] = {0};
        uint64_t iters_size, iters;

        bytes = read_data(conn);
        if (bytes < 0) {
            return;
        }

        memcpy(iters_size_buf, buf, 2);
        iters_size = strtoul(iters_size_buf, NULL, 10);
        if ((uint64_t)bytes != 2 + iters_size) {
            LOG_ERROR("Bad iters data size: %zd", bytes);
            throw std::runtime_error("Bad data size");
        }
        buf[2 + iters_size] = '\0';
        iters = strtoul(&buf[2], NULL, 10);

        if (iters) {
            // TODO: request proof with given iters
        } else {
            LOG_INFO("VDF %d: Stop requested", conn->vdf.idx);
            write_data(conn, "STOP", 4);
            hw_proof_stop(&conn->vdf);
            clear_vdf_state(&conn->vdf);
            stop_hw_vdf(client->drv, conn->vdf.idx);
            conn->state = STOPPED;
            LOG_INFO("VDF %d: Stopped at iters=%lu", conn->vdf.idx, conn->vdf.cur_iters);
        }
    }
    if (conn->state == STOPPED) {
        bytes = read_data(conn);
        if (bytes == 3 && !memcmp(buf, "ACK", 3)) {
            close(conn->sock);
            conn->sock = -1;
            conn->state = CLOSED;
            LOG_INFO("VDF %d: Connection closed", conn->vdf.idx);
        } else if (bytes >= 0) {
            LOG_ERROR("Bad data size after stop: %zd", bytes);
            throw std::runtime_error("Bad data size");
        }
    } else if (conn->state == CLOSED) {
        init_conn(conn, client->port);
    }
}

void event_loop(struct vdf_client *client)
{
    while(true) {
        uint8_t vdfs_mask = 0;

        for (uint8_t i = 0; i < client->n_vdfs; i++) {
            handle_conn(client, &client->conns[i]);
            if (client->conns[i].state == RUNNING) {
                vdfs_mask |= 1 << i;
            }
        }

        if (vdfs_mask) {
            read_hw_status(client->drv, vdfs_mask, client->values);
        }

        for (uint8_t i = 0; i < client->n_vdfs; i++) {
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
    }
}

int main(int argc, char **argv)
{
    struct vdf_client client;

    if (argc < 2) {
        LOG_INFO("Usage: %s PORT [N_VDFS]", argv[0]);
        return 1;
    }

    VdfBaseInit();
    client.drv = init_hw();

    client.port = atoi(argv[1]);
    client.n_vdfs = 3;
    if (argc > 2) {
        client.n_vdfs = atoi(argv[2]);
    }
    init_vdf_client(&client);

    event_loop(&client);

    //clear_conns(conns);
    return 0;
}
