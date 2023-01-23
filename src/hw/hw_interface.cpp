#include "hw_interface.hpp"
#include "chia_driver.hpp"

#include "vdf_base.hpp"

#include "libft4222.h"

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
    uint32_t job_id = 0xab;
    integer D;
    mpz_set(D.impl, d);
    form y = form::generator(D);
    integer L(root(-D, 4));

    drv->SerializeJob(buf, job_id, n_iters,
            y.a.impl, y.b.impl, D.impl, L.impl);
}

ChiaDriver *init_hw(void)
{
    FtdiDriver ftdi;
    if (ftdi.Open()) {
        throw std::runtime_error("Failed to open device");
    }

    ChiaDriver* drv = new ChiaDriver(ftdi);

    double freq = 1100.0;
    bool set_status;

    set_status = drv->SetPLLFrequency(freq);
    if (set_status == false) {
      fprintf(stderr, "Aborting since freq not set\n");
      abort();
    }

    // Check frequency
    double freq_read = drv->GetPLLFrequency();
    printf("Frequency is %lf MHz\n", freq_read);

    double brd_voltage = drv->GetBoardVoltage();
    printf("Board voltage is %1.3lf V\n", brd_voltage);

    double set_brd_voltage = 0.80;
    int ret_val = drv->SetBoardVoltage(set_brd_voltage);
    if (ret_val != 0) {
      fprintf(stderr, "Aborting since set voltage failed\n");
      abort();
    }

    brd_voltage = drv->GetBoardVoltage();
    printf("Board voltage is now %1.3lf V\n", brd_voltage);

    double brd_current = drv->GetBoardCurrent();
    printf("Board current is %2.3lf A\n", brd_current);

    double brd_power = drv->GetPower();
    printf("Board power is %2.3lf W\n", brd_power);

    // Enable PVT sensor
    drv->EnablePvt();

    // Set external temperature alarm in PVT sensor
    double external_alarm_temp = 100.0;
    set_status = drv->SetTempAlarmExternal(external_alarm_temp);
    if (set_status == false) {
      fprintf(stderr, "Aborting since temp alarm not set\n");
      abort();
    }

    // Set engine temperature alarm in PVT sensor
    double engine_alarm_temp = 100.0;
    set_status = drv->SetTempAlarmEngine(engine_alarm_temp);
    if (set_status == false) {
      fprintf(stderr, "Aborting since temp alarm not set\n");
      abort();
    }

    return drv;
}

struct vdf_state;
void add_vdf_value(struct vdf_state *vdf, mpz_t a, mpz_t f, uint64_t n_iters);


int start_hw_vdf(ChiaDriver *drv, mpz_t d, uint64_t n_iters, int idx)
{
    uint8_t job[CHIA_VDF_JOB_SIZE];
    uint32_t base_addr = CHIA_VDF_CONTROL_REG_OFFSET + CHIA_VDF_JOB_CSR_MULT * idx;

    prepare_job(drv, n_iters, job, d);

    // Enable the engine and write in the job
    drv->EnableEngine(base_addr);
    drv->ftdi.Write(base_addr + CHIA_VDF_JOB_ID_OFFSET, job, sizeof(job));

    // Provide time to clear any stale data in status registers
    //usleep(150000);
    return 0;
}

void stop_hw_vdf(ChiaDriver *drv, int idx)
{
    uint32_t base_addr = CHIA_VDF_CONTROL_REG_OFFSET + CHIA_VDF_JOB_CSR_MULT * idx;

    drv->DisableEngine(base_addr);
}

int read_hw_status(ChiaDriver *drv, struct vdf_state *vdfs[N_HW_VDFS], uint8_t idx_mask)
{
    mpz_t a, f;
    uint8_t read_status[HW_VDF_STATUS_SIZE + 20];
    uint32_t job_id;
    uint64_t done_iters = 0;

    mpz_inits(a, f, NULL);

    for (int i = 0; i < N_HW_VDFS; i++) {
        uint8_t *job;

        if (i == 0) {
            drv->ftdi.Read(0x300000, read_status, HW_VDF_STATUS_SIZE + 20);
            job = read_status + 20;

            uint32_t temp_code;
            drv->read_bytes(4, 0, read_status, temp_code);
            double temp = drv->ValueToTemp(temp_code);
            fprintf(stderr, "ASIC Temp = %3.2f C\n", temp);
        } else {
            drv->ftdi.Read(CHIA_VDF_STATUS_JOB_ID_REG_OFFSET + (0x10000 * i),
                           read_status, HW_VDF_STATUS_SIZE);
            job = read_status;
        }

        if (!(idx_mask & (1 << i))) {
            continue;
        }

        drv->DeserializeJob(job, job_id, done_iters, a, f);

        fprintf(stderr, "VDF %d: Got iters=%lu\n", i, done_iters);
        add_vdf_value(vdfs[i], a, f, done_iters);
    }

    //usleep(100000);
    mpz_clears(a, f, NULL);
    return 0;
}
