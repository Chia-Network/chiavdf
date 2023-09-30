//#include "chia_driver.hpp"
//#include "chia_vdf.hpp"

//#include <algorithm>
//#include <cstdint>
//#include <cstring>
//#include <gmp.h>
//#include "alloc.hpp"
//#include "create_discriminant.h"

//#include "libft4222.h"

//#include <vector>
#include "hw_interface.hpp"
#include "hw_proof.hpp"
#include "vdf_base.hpp"
#include "bqfc.h"

#include <cfenv>
#include <thread>

#include <unistd.h>

static const char *discrs[] = {
    "-0xac566497f63870a7b661f5482f47336cd1aa85ab43914828b7998f255916729c2ad965bcf7fe231721d96706ea7d823ed4adf663a0263714bb80144aebafcdd2915b6c7ef68c2d19447be83e7f39b4a7442640914053d2e7d6a561aa29b9449c815717af7da97a823798f402d073901a1f2bd8cd879b8b1afe2496649197021f",
    "-0xc3657f850b3f2b659d70273704564bc69b849fe1d8c70b096933efdcf7143931b393676f500d79624da783d73e0c5303ae48fb9543502c4161586d8fdaf03709d2115df21aeeee4a58614050cbdfe74024063b9620de084d8ef46f474fa57983c4bebfa7e8a69efeb523a167558fe1487a086c11337e20b773ad3d4710671417",
    "-0xffbd9edace9d59e0a395f0358698e8cd68a3dcb90af740d1fe14f58a06678e11d47e19b4f9b9237689084b724db8a912bb32d64614bbb5c4df5c54e9b574b8f7b7c59c8ac2522aae4777220696ee4dd11942e9e85b07de0454a491db7b19baa4c2f1eb3ae8930c44984c767c664d85e337b8e90cf1c5d3d30a9ec7ddcc3b26d7"
};

void init_chia(void)
{
    VdfBaseInit();
    //init_gmp();
    //set_rounding_mode();
    //fesetround(FE_TOWARDZERO);
}

int hw_test_main(int argc, char **argv)
{
    uint64_t n_iters = 1000000;
    uint8_t n_vdfs = 3;
    uint8_t n_completed = 0;
    uint8_t vdfs_mask;
    uint8_t init_form[BQFC_FORM_SIZE] = { 0x08 };
    struct vdf_state vdfs[N_HW_VDFS];
    struct vdf_value values[N_HW_VDFS];
    struct vdf_proof proofs[N_HW_VDFS];
    std::thread proof_threads[N_HW_VDFS];
    //std::thread vdf_threads[N_HW_VDFS];
    ChiaDriver *drv;
    uint64_t read_cnt = 0;
    uint32_t temp_period = chia_vdf_is_emu ? 200 : 2000;

    if (argc > 1) {
        n_vdfs = strtoul(argv[1], NULL, 0);
        if (n_vdfs > 3 || n_vdfs < 1) {
            n_vdfs = 2;
        }
    }
    if (argc > 2) {
        n_iters = strtoul(argv[2], NULL, 0);
    }
    init_chia();
    drv = init_hw(HW_VDF_DEF_FREQ, HW_VDF_DEF_VOLTAGE);
    if (!drv) {
        return 1;
    }

    for (uint8_t i = 0; i < n_vdfs; i++) {
        struct vdf_state *vdf = &vdfs[i];

        init_vdf_state(vdf, NULL, discrs[i], init_form, n_iters, i);

        //run_hw(vdf->d, n_iters, vdf);
        //vdf_threads[i] = std::thread(start_vdf_job, vdf, i);
        start_hw_vdf(drv, vdf->D.impl, vdf->last_val.a, vdf->last_val.b, vdf->target_iters, i);
        init_vdf_value(&values[i]);
    }

    vdfs_mask = (1 << n_vdfs) - 1;
    while (vdfs_mask) {
        uint8_t temp_flag = read_cnt % temp_period ? 0 : HW_VDF_TEMP_FLAG;
        read_hw_status(drv, vdfs_mask | temp_flag, values);
        for (uint8_t i = 0; i < n_vdfs; i++) {
            if (vdfs_mask & (1 << i)) {
                hw_proof_add_value(&vdfs[i], &values[i]);
            }

            if (vdfs[i].completed && (vdfs_mask & (1 << i))) {
                stop_hw_vdf(drv, i);
                vdfs_mask &= ~(1 << i);
                n_completed++;
                proofs[i].iters = n_iters;
                proofs[i].seg_iters = n_iters;
                proof_threads[i] = std::thread(hw_compute_proof, &vdfs[i], SIZE_MAX, &proofs[i], 255);
            }
        }
        //if (i == n_vdfs) {
            //fprintf(stderr, "Proofs completed: %d\n", (int)i);
            //break;
        //}
        if (chia_vdf_is_emu) {
            usleep(50000);
        }
        read_cnt++;
    }

    for (uint8_t i = 0; i < n_vdfs; i++) {
        proof_threads[i].join();
        clear_vdf_state(&vdfs[i]);
        clear_vdf_value(&values[i]);
    }
    stop_hw(drv);
    return 0;
}

int main(int argc, char **argv)
{
    init_chia();
    return hw_test_main(argc, argv);
}
