#include "hw_interface.hpp"
#include "chia_driver.hpp"
#include "hw_util.hpp"
#include "pll_freqs.hpp"

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

void init_vdf_value(struct vdf_value *val)
{
    val->iters = 0;
    mpz_inits(val->a, val->b, NULL);
}

void clear_vdf_value(struct vdf_value *val)
{
    mpz_clears(val->a, val->b, NULL);
}

void copy_vdf_value(struct vdf_value *dst, struct vdf_value *src)
{
    dst->iters = src->iters;
    mpz_init_set(dst->a, src->a);
    mpz_init_set(dst->b, src->b);
}

int list_hw(void)
{
    FtdiDriver ftdi;
    return ftdi.List();
}

void prepare_job(ChiaDriver *drv, uint64_t n_iters, uint8_t *buf, mpz_t d, mpz_t a, mpz_t b)
{
    uint32_t job_id = 0xab;
    mpz_t L;
    mpz_init_set(L, d);
    mpz_neg(L, L);
    mpz_root(L, L, 4);

    drv->SerializeJob(buf, job_id, n_iters, a, b, d, L);
    mpz_clear(L);
}

ChiaDriver *init_hw(double freq, double set_brd_voltage)
{
    bool set_status;
    double freq_read, brd_voltage, brd_current, brd_power;
    double external_alarm_temp = 100.0, engine_alarm_temp = 100.0;

    ChiaDriver* drv = new ChiaDriver();
    if (drv->ftdi.Open()) {
        LOG_ERROR("Failed to open device");
        goto fail;
    }

    LOG_INFO("Setting frequency to %f MHz", freq);
    set_status = drv->SetPLLFrequency(freq, 0);
    if (set_status == false) {
        LOG_ERROR("Aborting since freq not set");
        goto fail;
    }

    // Check frequency
    freq_read = drv->GetPLLFrequency();
    LOG_INFO("Frequency is %f MHz", freq_read);

    brd_voltage = drv->GetBoardVoltage();
    LOG_INFO("Board voltage is %1.3f V", brd_voltage);

    LOG_INFO("Setting voltage to %1.3f V", set_brd_voltage);
    if (drv->SetBoardVoltage(set_brd_voltage) != 0) {
        LOG_ERROR("Aborting since set voltage failed");
        goto fail;
    }

    brd_voltage = drv->GetBoardVoltage();
    LOG_INFO("Board voltage is now %1.3f V", brd_voltage);

    brd_current = drv->GetBoardCurrent();
    LOG_INFO("Board current is %2.3f A", brd_current);

    brd_power = drv->GetPower();
    LOG_INFO("Board power is %2.3f W", brd_power);

    // Enable PVT sensor
    drv->EnablePvt();

    // Set external temperature alarm in PVT sensor
    set_status = drv->SetTempAlarmExternal(external_alarm_temp);
    if (set_status == false) {
        LOG_ERROR("Aborting since temp alarm not set");
        goto fail;
    }

    // Set engine temperature alarm in PVT sensor
    set_status = drv->SetTempAlarmEngine(engine_alarm_temp);
    if (set_status == false) {
        LOG_ERROR("Aborting since temp alarm not set");
        goto fail;
    }

    return drv;

fail:
    if (drv->ftdi.IsOpen()) {
        drv->ftdi.Close();
    }
    delete drv;
    return NULL;
}

void stop_hw(ChiaDriver *drv)
{
    delete drv;
}

void hw_vdf_control(ChiaDriver *drv, uint8_t idx_mask, uint32_t value)
{
    for (uint8_t i = 0; i < N_HW_VDFS; i++) {
        uint32_t addr = CHIA_VDF_CONTROL_REG_OFFSET + CHIA_VDF_JOB_CSR_MULT * i;
        if (idx_mask & (1 << i)) {
           drv->RegWrite(addr, value);
        }
    }
}

void adjust_hw_freq(ChiaDriver *drv, uint8_t idx_mask, int direction)
{
    double freq;

    hw_vdf_control(drv, idx_mask, 0);

    drv->SetPLLFrequency(0.0, drv->freq_idx + direction);
    freq = drv->GetPLLFrequency();
    LOG_INFO("Frequency is %s to %.1f MHz; freq_idx = %u",
            direction > 0 ? "increased" : "decreased", freq, drv->freq_idx);

    hw_vdf_control(drv, idx_mask, 1);
}

int start_hw_vdf(ChiaDriver *drv, mpz_t d, mpz_t a, mpz_t b, uint64_t n_iters, int idx)
{
    uint8_t job[CHIA_VDF_JOB_SIZE];
    uint32_t base_addr = CHIA_VDF_CONTROL_REG_OFFSET + CHIA_VDF_JOB_CSR_MULT * idx;

    prepare_job(drv, n_iters, job, d, a, b);

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

int read_hw_status(ChiaDriver *drv, uint8_t idx_mask, struct vdf_value *values)
{
    uint8_t read_status[HW_VDF_STATUS_SIZE + 20];
    uint32_t job_id;

    for (int i = 0; i < N_HW_VDFS; i++) {
        uint8_t *job;
        struct vdf_value *val = &values[i];

        if (i == 0 && idx_mask & (HW_VDF_TEMP_FLAG | (1 << i))) {
            drv->ftdi.Read(0x300000, read_status, HW_VDF_STATUS_SIZE + 20);
            job = read_status + 20;

            if (idx_mask & HW_VDF_TEMP_FLAG) {
                uint32_t temp_code;
                drv->read_bytes(4, 0, read_status, temp_code);
                double temp = drv->ValueToTemp(temp_code);
                LOG_INFO("ASIC Temp = %3.2f C; Frequency = %.1f MHz; freq_idx = %u",
                        temp, pll_entries[drv->freq_idx].freq, drv->freq_idx);
            }
        } else if (idx_mask & (1 << i)) {
            drv->ftdi.Read(CHIA_VDF_STATUS_JOB_ID_REG_OFFSET + (0x10000 * i),
                           read_status, HW_VDF_STATUS_SIZE);
            job = read_status;
        } else {
            continue;
        }

        if (idx_mask & (1 << i)) {
            drv->DeserializeJob(job, job_id, val->iters, val->a, val->b);
            LOG_DEBUG("VDF %d: Got iters=%lu", i, val->iters);
        }
    }

    //usleep(100000);
    return 0;
}
