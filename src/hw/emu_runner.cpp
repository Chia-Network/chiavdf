#include "chia_driver.hpp"
#include "vdf_base.hpp"
#include "clock.hpp"
#include "hw_util.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <mutex>

#include <arpa/inet.h>
#include <unistd.h>

#define N_VDFS 3

struct job_reg_set {
    uint32_t id;
    uint32_t iters[2];
    uint32_t a[21], f[21], d[41], l[11];
    uint32_t start_flag;
    uint32_t padding[30];
};

static struct job_reg_set g_job_regs[N_VDFS];

struct job_status {
    uint32_t id;
    uint32_t iters[2];
    uint32_t a[21], f[21];
    //uint32_t padding[19];
};

static struct job_status g_status_regs[N_VDFS];
static uint32_t g_pll_regs[8];

static int g_error_prob = -1;

struct job_state {
    uint64_t cur_iter;
    uint64_t target_iter;
    integer d, l;
    form qf;
    bool init_done;
    bool stopping;
    bool running;
    bool error;
    ChiaDriver *drv;
    std::mutex mtx;
};

static struct job_state *states[N_VDFS];

void init_state(struct job_state *st, struct job_reg_set *r)
{
    integer a, f;
    st->drv = new ChiaDriver();

    st->cur_iter = 0;
    st->drv->read_bytes(sizeof(r->iters), 0, (uint8_t *)r->iters, st->target_iter);

    st->drv->read_bytes(sizeof(r->a), 0, (uint8_t *)r->a, a.impl, st->drv->NUM_2X_COEFFS);
    st->drv->read_bytes(sizeof(r->f), 0, (uint8_t *)r->f, f.impl, st->drv->NUM_2X_COEFFS);
    st->drv->read_bytes(sizeof(r->d), 0, (uint8_t *)r->d, st->d.impl, st->drv->NUM_4X_COEFFS);
    st->drv->read_bytes(sizeof(r->l), 0, (uint8_t *)r->l, st->l.impl, st->drv->NUM_1X_COEFFS);

    st->qf = form::from_abd(a, f, st->d);
    st->init_done = true;
    st->stopping = false;
}

void clear_state(struct job_state *st)
{
    if (st->init_done) {
        delete st->drv;
        st->init_done = false;
    }
}

void run_job(int i)
{
    struct job_state *st = states[i];
    PulmarkReducer reducer;
    form qf2;

    LOG_INFO("Emu %d: Starting run for %lu iters", i, st->target_iter);

    st->error = false;
    st->running = true;
    while (!st->stopping && st->cur_iter < st->target_iter) {
        nudupl_form(qf2, st->qf, st->d, st->l);
        reducer.reduce(qf2);

        if (!(st->cur_iter % 4096)) {
            usleep(10);
        }

        st->mtx.lock();
        st->cur_iter++;
        st->qf = qf2;
        st->mtx.unlock();
    }
    st->running = false;
    LOG_INFO("Emu %d: job ended", i);
}

void job_thread(int i)
{
    run_job(i);
}

static void start_job(int i)
{
    while (states[i]->running) {
        states[i]->stopping = true;
        LOG_INFO("Emu %d: Waiting for the old thread to finish", i);
        usleep(1000);
    }
    clear_state(states[i]);

    LOG_INFO("Emu %d: Starting job", i);
    init_state(states[i], &g_job_regs[i]);
    std::thread(job_thread, i).detach();
    //usleep(100000);
}

static void disable_engine(int i)
{
    if (states[i] && states[i]->init_done && !states[i]->stopping) {
        LOG_INFO("Emu %d: Disabling engine", i);
        states[i]->stopping = true;
    }
}

static void enable_engine(int i)
{
    if (!states[i]) {
        states[i] = new job_state;
        states[i]->init_done = false;
        states[i]->stopping = true;
        states[i]->running = false;
        srand48(1);
    }
    if (states[i]->stopping) {
        states[i]->stopping = false;
        LOG_INFO("Emu %d: Enabling engine", i);
    }
}

void inject_error(struct job_status *stat, struct job_state *st)
{
    int p = g_error_prob;
    if (p == -1) {
        char *prob = getenv("EMU_ERROR_PROB");
        p = prob ? atoi(prob) : 0;
        g_error_prob = p;
    }
    if (p != 0 && (st->error || (uint32_t)mrand48() % p == 0)) {
        // Inject error by messing up 'a' register
        stat->a[10] = ~stat->a[10];
        st->error = true;
    }
}

void update_status(struct job_status *stat, struct job_state *st)
{
    if (!st->stopping) {
        st->mtx.lock();
        st->drv->write_bytes(sizeof(stat->iters), 0, (uint8_t *)stat->iters, st->cur_iter);
        st->drv->write_bytes(sizeof(stat->a), 0, (uint8_t *)stat->a, st->qf.a.impl, st->drv->NUM_2X_COEFFS);
        st->drv->write_bytes(sizeof(stat->f), 0, (uint8_t *)stat->f, st->qf.b.impl, st->drv->NUM_2X_COEFFS);
        st->mtx.unlock();
    } else {
        memset(stat, 0, sizeof(*stat));
    }

    inject_error(stat, st);
}

void copy_regs(void *dst, void *src, uint32_t size, uint32_t max_size, int32_t offset)
{
    if (offset < 0) {
        memset(dst, 0, -offset);
        size += offset;
        dst = (uint8_t *)dst - offset;
        offset = 0;
    }
    if (size + offset <= max_size) {
        memcpy(dst, (uint8_t *)src + offset, size);
    } else {
        memcpy(dst, (uint8_t *)src + offset, max_size - offset);
        memset((uint8_t *)dst + max_size - offset, 0, offset);
    }
}

#define CHIA_VDF_STATUS_SIZE 0xb4
#define CHIA_VDF_BURST_START 0x300014
//#define CHIA_VDF_BURST_END 0x300244
#define CHIA_VDF_JOB_CSR_MULT 0x10000

void read_regs(uint32_t addr, uint8_t *buf, uint32_t size)
{
    uint32_t job_status_base = CHIA_VDF_STATUS_JOB_ID_REG_OFFSET;
    uint32_t job_status_end = job_status_base + CHIA_VDF_JOB_CSR_MULT * N_VDFS;

    uint32_t burst_start = CHIA_VDF_BURST_START;
    uint32_t burst_end = CHIA_VDF_BURST_START + sizeof(g_status_regs);

    for (int i = 0; i < N_VDFS; i++) {
        uint32_t job_id_addr = CHIA_VDF_BURST_START +
            sizeof(g_status_regs[0]) * i;
        uint32_t job_id_addr2 = job_status_base +
            CHIA_VDF_JOB_CSR_MULT * i;

        if (!states[i] || !states[i]->init_done) {
            continue;
        }

        // Reading the job ID register triggers job status update
        if ((job_id_addr >= addr && job_id_addr < addr + size) ||
                (job_id_addr2 >= addr && job_id_addr2 < addr + size))
        {
            LOG_DEBUG("Emu: Updating vdf %d", i);
            update_status(&g_status_regs[i], states[i]);
        }
    }

    if (addr + size > job_status_base && addr < job_status_end) {
        uint32_t i = (addr - job_status_base) / CHIA_VDF_JOB_CSR_MULT;
        int32_t offset = addr - job_status_base - CHIA_VDF_JOB_CSR_MULT * i;
        copy_regs(buf, &g_status_regs[i], size, sizeof(g_status_regs[0]), offset);
    } else if (addr + size > burst_start && addr < burst_end) {
        int32_t offset = addr - burst_start;
        copy_regs(buf, g_status_regs, size, sizeof(g_status_regs), offset);
    } else if (addr < sizeof(g_pll_regs)) {
        copy_regs(buf, g_pll_regs, size, sizeof(g_pll_regs), addr);
    } else {
        memset(buf, 0, size);
        LOG_INFO("Emu: No data at addr=0x%x size=%u", addr, size);
    }
}

#define CMD_SIZE 4
#define WAIT_CYCLES 4

int emu_do_io(uint8_t *buf_in, uint16_t size_in, uint8_t *buf_out, uint16_t size_out)
{
    uint32_t job_control = CHIA_VDF_CONTROL_REG_OFFSET;
    uint32_t job_csr = CHIA_VDF_CMD_JOB_ID_REG_OFFSET;
    uint32_t addr = (buf_in[1] << 16) | (buf_in[2] << 8) | buf_in[3];
    int i;

    addr <<= 2;
    // skip address bytes
    buf_in += CMD_SIZE;
    size_in -= CMD_SIZE;
    LOG_DEBUG("Emu: addr=0x%x in=%hu bytes out=%hu bytes", addr, size_in, size_out);
    for (i = 0; i < N_VDFS; i++) {
        if (addr >= job_csr && addr < job_csr + sizeof(g_job_regs[0])) {
            uint32_t offset = addr - job_csr;
            memcpy((uint8_t *)&g_job_regs[i] + offset, buf_in, size_in);
            LOG_DEBUG("Emu: offset=0x%x start_flag=0x%x", offset, g_job_regs[i].start_flag);

            if (g_job_regs[i].start_flag & (1 << 24)) {
                start_job(i);
                g_job_regs[i].start_flag = 0;
            }
        }
        if (addr == job_control) {
            uint32_t data;
            memcpy(&data, buf_in, 4);
            data = ntohl(data);
            if (data & (1U << CHIA_VDF_CONTROL_CLK_ENABLE_BIT)) {
                enable_engine(i);
            } else {
                disable_engine(i);
            }
        }
        job_csr += CHIA_VDF_JOB_CSR_MULT;
        job_control += CHIA_VDF_JOB_CSR_MULT;
    }

    if (!size_in && size_out) {
        size_out -= WAIT_CYCLES;
        buf_out += WAIT_CYCLES;

        read_regs(addr, buf_out, size_out);
    } else if (!size_out && addr < sizeof(g_pll_regs)) {
        uint8_t *regs_addr = &((uint8_t *)g_pll_regs)[addr];
        uint32_t status_val = htonl((1U << CLOCK_STATUS_DIVACK_BIT) |
                (1U << CLOCK_STATUS_LOCK_BIT));

        copy_regs(regs_addr, buf_in, size_in, sizeof(g_pll_regs) - addr, 0);
        g_pll_regs[CLOCK_STATUS_REG_OFFSET / 4] = status_val;
    }

    return 0;
}

int emu_do_io_i2c(uint8_t *buf, uint16_t size, uint32_t addr, int is_out)
{
    if (is_out) {
        memset(buf, 0, size);
    }
    return 0;
}

__attribute__((destructor)) void emu_shutdown(void)
{
    for (int i = 0; i < N_VDFS; i++) {
        if (states[i]) {
            clear_state(states[i]);
            delete states[i];
            states[i] = NULL;
        }
    }
}
