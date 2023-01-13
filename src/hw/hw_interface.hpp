#ifndef HW_INTERFACE_H
#define HW_INTERFACE_H

#include <gmp.h>
#include <cstdint>

#define N_HW_VDFS 3
#define HW_VDF_STATUS_SIZE 0xb4
#define HW_VDF_BURST_START 0x300014

class ChiaDriver;
ChiaDriver *init_hw(void);

struct vdf_state;
//int run_hw(mpz_t d, uint64_t n_iters, struct vdf_state *vdf_p, int idx);
int start_hw_vdf(ChiaDriver *drv, mpz_t d, uint64_t n_iters, int idx);
void stop_hw_vdf(ChiaDriver *drv, int idx);
int read_hw_status(ChiaDriver *drv, struct vdf_state *vdfs[N_HW_VDFS], uint8_t idx_mask);

#endif // HW_INTERFACE_H
