#include "chia_driver.hpp"
//#include "verifier.h"
//#include "../src/chia/chia_registers.hpp"
//#include "alloc.hpp"
#include "vdf_base.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <mutex>

#include <unistd.h>

#define LOG(msg, ...) fprintf(stderr, msg "\n", ##__VA_ARGS__)

#define N_VDFS 3

struct job_regs {
	uint32_t id;
	uint32_t iters[2];
	uint32_t a[21], f[21], d[41], l[11];
	uint32_t start_flag;
	uint32_t padding[30];
};

static struct job_regs regs[N_VDFS];

struct job_status {
	uint32_t id;
	uint32_t iters[2];
	uint32_t a[21], f[21];
	//uint32_t padding[19];
};

//static struct job_status status;
//struct burst_regs {
	//uint32_t pvt[5];
	//struct job_status vdfs[N_VDFS];
	//uint32_t pvt2[5];
//};
//static struct burst_regs burst;
static struct job_status g_status_regs[N_VDFS];

struct job_state {
	uint64_t cur_iter;
	uint64_t target_iter;
	integer d, l;
	form qf;
	bool init_done;
	ChiaDriver *drv;
	std::mutex mtx;
};

static struct job_state states[N_VDFS];

void init_state(struct job_state *st, struct job_regs *r)
{
	//size_t a_size = CHIA_VDF_CMD_A_MULTIREG_COUNT * 4;
	//size_t f_size = CHIA_VDF_CMD_F_MULTIREG_COUNT * 4;
	//size_t d_size = CHIA_VDF_CMD_D_MULTIREG_COUNT * 4;
	//size_t l_size = CHIA_VDF_CMD_L_MULTIREG_COUNT * 4;
	integer a, f;
	FtdiDriver ftdi;
	st->drv = new ChiaDriver(ftdi);

	st->cur_iter = 0;
	st->drv->read_bytes(sizeof(r->iters), 0, (uint8_t *)r->iters, st->target_iter);

	st->drv->read_bytes(sizeof(r->a), 0, (uint8_t *)r->a, a.impl, st->drv->NUM_2X_COEFFS);
	st->drv->read_bytes(sizeof(r->f), 0, (uint8_t *)r->f, f.impl, st->drv->NUM_2X_COEFFS);
	st->drv->read_bytes(sizeof(r->d), 0, (uint8_t *)r->d, st->d.impl, st->drv->NUM_4X_COEFFS);
	st->drv->read_bytes(sizeof(r->l), 0, (uint8_t *)r->l, st->l.impl, st->drv->NUM_1X_COEFFS);

	st->qf = form::from_abd(a, f, st->d);
}

void run_job(int i)
{
	struct job_state *st = &states[i];
	PulmarkReducer reducer;
	form qf2;

	LOG("Emu %d: Starting run for %lu iters", i, st->target_iter);
	st->init_done = true;

	while (st->cur_iter < st->target_iter) {
		nudupl_form(qf2, st->qf, st->d, st->l);
		reducer.reduce(qf2);

		st->mtx.lock();
		st->cur_iter++;
		st->qf = qf2;
		//LOG("iters=%lu", st->cur_iter);
		st->mtx.unlock();
	}
	//st->running = false;
	LOG("Emu %d: job ended", i);
}

void job_thread(int i)
{
	init_state(&states[i], &regs[i]);
	run_job(i);
	//LOG("Job %d running", i);
}

static void start_job(int i)
{
	LOG("Emu %d: Starting job", i);
	std::thread(job_thread, i).detach();
	//usleep(100000);
}

void update_status(struct job_status *stat, struct job_state *st)
{
	st->mtx.lock();
	st->drv->write_bytes(sizeof(stat->iters), 0, (uint8_t *)stat->iters, st->cur_iter);
	st->drv->write_bytes(sizeof(stat->a), 0, (uint8_t *)stat->a, st->qf.a.impl, st->drv->NUM_2X_COEFFS);
	st->drv->write_bytes(sizeof(stat->f), 0, (uint8_t *)stat->f, st->qf.b.impl, st->drv->NUM_2X_COEFFS);
	st->mtx.unlock();
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
	//uint32_t job_status_size = job_status_end - job_status;

	uint32_t burst_start = CHIA_VDF_BURST_START;
	uint32_t burst_end = CHIA_VDF_BURST_START + sizeof(g_status_regs);

	//if ((addr >= job_status && addr < job_status_end) ||
			//(addr >= burst_start && addr < burst_end))
	//{
	for (int i = 0; i < N_VDFS; i++) {
		uint32_t job_id_addr = CHIA_VDF_BURST_START +
			sizeof(g_status_regs[0]) * i;
		uint32_t job_id_addr2 = job_status_base +
			CHIA_VDF_JOB_CSR_MULT * i;

		if (!states[i].init_done) {
			continue;
		}
		/* Reading the job ID register triggers job status update. */
		if ((job_id_addr >= addr && job_id_addr < addr + size) ||
			(job_id_addr2 >= addr && job_id_addr2 < addr + size))
		{
			LOG("Emu: Updating vdf %d", i);
			update_status(&g_status_regs[i], &states[i]);
		}
	}
	//}
	if (addr >= job_status_base && addr < job_status_end) {
		uint32_t i = (addr - job_status_base) / CHIA_VDF_JOB_CSR_MULT;
		int32_t offset = addr - job_status_base - CHIA_VDF_JOB_CSR_MULT * i;
		copy_regs(buf, g_status_regs, size, sizeof(g_status_regs[0]), offset);
	} else if (addr >= burst_start && addr < burst_end) {
		int32_t offset = addr - burst_start;
		copy_regs(buf, g_status_regs, size, sizeof(g_status_regs), offset);
	} else {
		memset(buf, 0, size);
		LOG("Emu: No data");
	}
}

#define CMD_SIZE 4
#define WAIT_CYCLES 4

int emu_do_io(uint8_t *buf_in, uint16_t size_in, uint8_t *buf_out, uint16_t size_out)
{
	uint32_t job_csr = CHIA_VDF_CMD_JOB_ID_REG_OFFSET;
	//uint32_t job_status = CHIA_VDF_STATUS_JOB_ID_REG_OFFSET;
	//uint32_t job_status_end = CHIA_VDF_STATUS_END_REG_OFFSET;
	uint32_t addr = (buf_in[1] << 16) | (buf_in[2] << 8) | buf_in[3];
	int i;

	addr <<= 2;
	// skip address bytes
	buf_in += CMD_SIZE;
	size_in -= CMD_SIZE;
	LOG("Emu: addr=0x%x in=%hu bytes out=%hu bytes", addr, size_in, size_out);
	for (i = 0; i < N_VDFS; i++) {
		if (addr >= job_csr && addr < job_csr + sizeof(regs[0])) {
			uint32_t offset = addr - job_csr;
			memcpy((uint8_t *)&regs[i] + offset, buf_in, size_in);
			LOG("Emu: offset=0x%x start_flag=0x%x", offset, regs[i].start_flag);

			if (regs[i].start_flag & (1 << 24)) {
				start_job(i);
			}
		}
		job_csr += CHIA_VDF_JOB_CSR_MULT;
	}

	if (!size_in && size_out) {
		//uint32_t offset = addr - job_status;
		//uint32_t size;
		size_out -= WAIT_CYCLES;
		buf_out += WAIT_CYCLES;

		//size = size_out;
		read_regs(addr, buf_out, size_out);
		//if (offset + size > job_status_end - job_status) {

		//}
		//update_status(&state);
		//memcpy(buf_out, (uint8_t *)&status + offset, size_out);
	}

	return 0;
}