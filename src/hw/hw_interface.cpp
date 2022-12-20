#include "hw_interface.hpp"
#include "chia_driver.hpp"
//#include "chia_vdf.hpp"

//#include "verifier.h"
//#include "create_discriminant.h"
#include "vdf_base.hpp"

#include "libft4222.h"

//#include <cstdint>
#include <stdexcept>
#include <cstring>
#include <unistd.h>

#define REG_BYTES 4
#define CHIA_VDF_JOB_SIZE (CHIA_VDF_CMD_START_REG_OFFSET - \
        CHIA_VDF_CMD_JOB_ID_REG_OFFSET + REG_BYTES)
#define CHIA_VDF_JOB_CSR_MULT 0x10000
#define CHIA_VDF_JOB_ID_OFFSET 0x1000
#define CHIA_VDF_JOB_STATUS_OFFSET 0x2000

void prepare_job(ChiaDriver *drv, uint64_t n_iters, uint8_t *buf, mpz_t d)
{
    uint32_t job_id = 0xaa;
    //AsicForm *af = ChiaAllocForm();
    //AsicForm *af = AsicForm::InitRandom(seed);
    //std::vector<uint8_t> seed_v;
    //for (int i = 0; i < 4; i++) {
        //seed_v.push_back((seed >> (i * 8)) & 0xFF);
    //}
    //integer D = CreateDiscriminant(seed_v);
    integer D;
    mpz_set(D.impl, d);
    form y = form::generator(D);
    integer L(root(-D, 4));

    drv->SerializeJob(buf, job_id, n_iters,
            y.a.impl, y.b.impl, D.impl, L.impl);
    //mpz_swap(d, D.impl);
}

ChiaDriver *init_hw(void)
{
    FtdiDriver ftdi;
    unsigned sys_clk = SYS_CLK_24;
    unsigned clk_div = CLK_DIV_2;
    unsigned device = 0;

    // Initialize the FTDI driver
    if (ftdi.OpenClk(sys_clk, clk_div, device)) {
        throw std::runtime_error("Failed to open device");
    }
    ftdi.ToggleGPIO(FtdiDriver::GPIO2);

    return new ChiaDriver(ftdi);
}

struct vdf_state;
void add_vdf_value(struct vdf_state *vdf, mpz_t a, mpz_t f, uint64_t n_iters);


int start_hw_vdf(ChiaDriver *drv, mpz_t d, uint64_t n_iters, int idx)
{
    //ChiaDriver *drv = init_hw();
    uint8_t job[CHIA_VDF_JOB_SIZE];
    uint32_t base_addr = CHIA_VDF_CONTROL_REG_OFFSET + CHIA_VDF_JOB_CSR_MULT * idx;
    //uint32_t job_id_addr = base_addr + CHIA_VDF_JOB_ID_OFFSET;

    prepare_job(drv, n_iters, job, d);

    // Enable the engine and write in the job
    drv->EnableEngine(base_addr);
    drv->ftdi.Write(base_addr + CHIA_VDF_JOB_ID_OFFSET, job, sizeof(job));

    // Provide time to clear any stale data in status registers
    //usleep(150000);
    return 0;
}

int read_hw_status(ChiaDriver *drv, struct vdf_state *vdfs[N_HW_VDFS], uint8_t idx_mask)
{
    mpz_t a, f;
    uint8_t burst_read_regs[HW_VDF_STATUS_SIZE * N_HW_VDFS];
    uint32_t job_id;
    uint64_t done_iters = 0;

    mpz_inits(a, f, NULL);

    memset(burst_read_regs, 0xa5, sizeof(burst_read_regs));
    drv->ftdi.Read(HW_VDF_BURST_START, burst_read_regs, sizeof(burst_read_regs));
    for (int i = 0; i < N_HW_VDFS; i++) {
        uint8_t *job = burst_read_regs + i * HW_VDF_STATUS_SIZE;
        if (!(idx_mask & (1 << i))) {
            continue;
        }
        drv->DeserializeJob(job, job_id, done_iters, a, f);

        fprintf(stderr, "VDF %d: Got iters=%lu\n", i, done_iters);
        //add_intermediate_form(a, f, d, done_iters);
        add_vdf_value(vdfs[i], a, f, done_iters);
    }

    //usleep(100000);
    mpz_clears(a, f, NULL);
    return 0;
}
