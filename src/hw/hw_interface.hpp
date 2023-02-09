#ifndef HW_INTERFACE_H
#define HW_INTERFACE_H

#include <gmp.h>
#include <cstdint>

#define N_HW_VDFS 3
#define HW_VDF_STATUS_SIZE 0xb4
#define HW_VDF_BURST_START 0x300014
#define HW_VDF_TEMP_FLAG (1 << 3)

struct vdf_value {
    uint64_t iters;
    mpz_t a, b;
};

extern int chia_vdf_is_emu;

class ChiaDriver;
ChiaDriver *init_hw(void);
void stop_hw(ChiaDriver *drv);

struct vdf_state;
//int run_hw(mpz_t d, uint64_t n_iters, struct vdf_state *vdf_p, int idx);
void init_vdf_value(struct vdf_value *val);
void clear_vdf_value(struct vdf_value *val);
void copy_vdf_value(struct vdf_value *dst, struct vdf_value *src);
int start_hw_vdf(ChiaDriver *drv, mpz_t d, mpz_t a, mpz_t b, uint64_t n_iters, int idx);
void stop_hw_vdf(ChiaDriver *drv, int idx);
int read_hw_status(ChiaDriver *drv, uint8_t idx_mask, struct vdf_value *values);

#endif // HW_INTERFACE_H
