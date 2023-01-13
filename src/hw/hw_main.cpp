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

#include <cfenv>
#include <thread>

#include <unistd.h>

static const char *discrs[] = {
    "-0xc5fd4a84d8d9816e8f4d569ef7ddef699e63b941a0dbb0c3c1210a7c140db3c27a9f5b19fb2e4e4848c23eedde311342f715189195122f7e456a015828224d0bf8bc7bc4e2c0186f49db6f05689a8b8a79a2cd4560edd95279f4498faf009e8056a3d4aeeaf0a3bcf2f7da89239797c27b5ce76f75c9deb834b79fc4f9c5557f",
    "-0xd264543a9469e4941bca6c7b16fd1d39904a508164c75f720a0f7fd67b5339a649442c2cfd253f0931c5fe7fb9eeec556163bbfad6615bf6e317b633ccca0f5aaacbc7ee28ec24418c8648c059498b18e3836393772c4a4a5e4d49a38dfa7826bf4e06a6ae1dc5c53406d08d033c5594a3d5f4999191bf573dc9941a39e6c8f7",
    "-0xe03c07b6c9c1e24f8f3861e9ff17715a4e18501dee644ea0fcf10550787411c2f350eff666427ffd556557673928c5bc0fc088ac68da50aa39f62ff0e614ac0de4b2079b9b60b2087eb1779e9ba57ea2aafdf396871a530f4e7ff5c2fadb9050a54d4bf4ba3b25337b6ab12cc36bfd423274bf92c9307b4114cb1bbe87652eff"
};

void verify_vdf_value(struct vdf_state *vdf, struct vdf_value *val)
{
    mpz_mul(vdf->a2, val->b, val->b);
    mpz_sub(vdf->a2, vdf->a2, vdf->d);
    /* Verify that c could be computed as c = (b^2 - d) / (4 * a) */
    if (!mpz_divisible_p(vdf->a2, val->a) || mpz_scan1(vdf->a2, 0) < mpz_scan1(val->a, 0) + 2) {
        fprintf(stderr, "VDF %d: Bad VDF value at iters=%lu", vdf->idx, val->iters);
        abort();
    }
}

void add_vdf_value(struct vdf_state *vdf, mpz_t a, mpz_t f, uint64_t n_iters)
{
    struct vdf_value val;
    uint64_t last_iters = 0;

    if (!n_iters) {
        fprintf(stderr, "VDF %d: Skipping iters=0\n", vdf->idx);
        return;
    }

    if (vdf->last_val.iters) {
        last_iters = vdf->last_val.iters;
        if (n_iters == last_iters) {
            fprintf(stderr, "VDF %d: Skipping iters=%lu\n", vdf->idx, n_iters);
            return;
        }
    }
    mpz_inits(val.a, val.b, NULL);
    mpz_set(val.a, a);
    mpz_mul_2exp(vdf->a2, a, 1);
    mpz_mod(val.b, f, vdf->a2);
    val.iters = n_iters;
    verify_vdf_value(vdf, &val);
    //vdf->raw_values.push_back(val);
    //vdf->cur_iters = n_iters;
    hw_proof_add_value(vdf, &val);
}

void init_chia(void)
{
    VdfBaseInit();
    //init_gmp();
    //set_rounding_mode();
    //fesetround(FE_TOWARDZERO);
}

//static void start_vdf_job(struct vdf_state *vdf, int idx)
//{
    //run_hw(vdf->d, vdf->target_iters, vdf, idx);
//}

int main(int argc, char **argv)
{
    uint64_t n_iters = 100000;
    uint8_t n_vdfs = 2;
    uint8_t n_completed = 0;
    uint8_t vdfs_mask;
    struct vdf_state vdfs[N_HW_VDFS];
    struct vdf_state *vdf_ptrs[N_HW_VDFS];
    std::thread proof_threads[N_HW_VDFS];
    //std::thread vdf_threads[N_HW_VDFS];
    ChiaDriver *drv;

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
    drv = init_hw();

    for (uint8_t i = 0; i < n_vdfs; i++) {
        struct vdf_state *vdf = &vdfs[i];

        init_vdf_state(vdf, discrs[i], n_iters, i);

        //run_hw(vdf->d, n_iters, vdf);
        //vdf_threads[i] = std::thread(start_vdf_job, vdf, i);
        start_hw_vdf(drv, vdf->d, n_iters, i);
        vdf_ptrs[i] = vdf;
    }

    vdfs_mask = (1 << n_vdfs) - 1;
    while (vdfs_mask) {
        uint8_t i;
        read_hw_status(drv, vdf_ptrs, vdfs_mask);
        for (i = 0; i < n_vdfs; i++) {
            if (!vdfs[i].completed) {
                break;
            }

            if (vdfs[i].completed && (vdfs_mask & (1 << i))) {
                stop_hw_vdf(drv, i);
                vdfs_mask &= ~(1 << i);
                n_completed++;
                proof_threads[i] = std::thread(hw_get_proof, vdf_ptrs[i]);
            }
        }
        //if (i == n_vdfs) {
            //fprintf(stderr, "Proofs completed: %d\n", (int)i);
            //break;
        //}
        usleep(50000);
    }

    for (uint8_t i = 0; i < n_vdfs; i++) {
        proof_threads[i].join();
        clear_vdf_state(&vdfs[i]);
    }
    return 0;
}
