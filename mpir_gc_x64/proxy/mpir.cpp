#include <shlwapi.h>
#include <intrin.h>

#define MPIRPATHLEN 4096

HINSTANCE mHinst = 0, mHinstDLL = 0;
WCHAR modelstr[100];
LPCSTR mImportNames[] = {
    "",
    "",
    "",
    "",
    "",
    "",
    "__combine_limbs",
    "__fermat_to_mpz",
    "__gmp_0",
    "__gmp_allocate_func",
    "__gmp_asprintf",
    "__gmp_asprintf_final",
    "__gmp_asprintf_memory",
    "__gmp_asprintf_reps",
    "__gmp_assert_fail",
    "__gmp_assert_header",
    "__gmp_bits_per_limb",
    "__gmp_default_allocate",
    "__gmp_default_fp_limb_precision",
    "__gmp_default_free",
    "__gmp_default_reallocate",
    "__gmp_digit_value_tab",
    "__gmp_divide_by_zero",
    "__gmp_doprnt",
    "__gmp_doprnt_integer",
    "__gmp_doprnt_mpf2",
    "__gmp_doscan",
    "__gmp_errno",
    "__gmp_exception",
    "__gmp_extract_double",
    "__gmp_fib_table",
    "__gmp_fprintf",
    "__gmp_free_func",
    "__gmp_fscanf",
    "__gmp_get_memory_functions",
    "__gmp_init_primesieve",
    "__gmp_invalid_operation",
    "__gmp_jacobi_table",
    "__gmp_junk",
    "__gmp_modlimb_invert_table",
    "__gmp_nextprime",
    "__gmp_primesieve",
    "__gmp_printf",
    "__gmp_randclear",
    "__gmp_randinit_default",
    "__gmp_randinit_lc_2exp",
    "__gmp_randinit_lc_2exp_size",
    "__gmp_randinit_mt",
    "__gmp_randinit_mt_noseed",
    "__gmp_randinit_set",
    "__gmp_rands",
    "__gmp_rands_initialized",
    "__gmp_randseed",
    "__gmp_randseed_ui",
    "__gmp_reallocate_func",
    "__gmp_replacement_vsnprintf",
    "__gmp_scanf",
    "__gmp_set_memory_functions",
    "__gmp_snprintf",
    "__gmp_sprintf",
    "__gmp_sqrt_of_negative",
    "__gmp_sscanf",
    "__gmp_tmp_reentrant_alloc",
    "__gmp_tmp_reentrant_free",
    "__gmp_urandomb_ui",
    "__gmp_urandomm_ui",
    "__gmp_vasprintf",
    "__gmp_version",
    "__gmp_vfprintf",
    "__gmp_vfscanf",
    "__gmp_vprintf",
    "__gmp_vscanf",
    "__gmp_vsnprintf",
    "__gmp_vsprintf",
    "__gmp_vsscanf",
    "__gmpf_abs",
    "__gmpf_add",
    "__gmpf_add_ui",
    "__gmpf_ceil",
    "__gmpf_clear",
    "__gmpf_clears",
    "__gmpf_cmp",
    "__gmpf_cmp_d",
    "__gmpf_cmp_si",
    "__gmpf_cmp_ui",
    "__gmpf_cmp_z",
    "__gmpf_div",
    "__gmpf_div_2exp",
    "__gmpf_div_ui",
    "__gmpf_dump",
    "__gmpf_eq",
    "__gmpf_fits_si_p",
    "__gmpf_fits_sint_p",
    "__gmpf_fits_slong_p",
    "__gmpf_fits_sshort_p",
    "__gmpf_fits_ui_p",
    "__gmpf_fits_uint_p",
    "__gmpf_fits_ulong_p",
    "__gmpf_fits_ushort_p",
    "__gmpf_floor",
    "__gmpf_get_2exp_d",
    "__gmpf_get_d",
    "__gmpf_get_d_2exp",
    "__gmpf_get_default_prec",
    "__gmpf_get_prec",
    "__gmpf_get_si",
    "__gmpf_get_str",
    "__gmpf_get_ui",
    "__gmpf_init",
    "__gmpf_init2",
    "__gmpf_init_set",
    "__gmpf_init_set_d",
    "__gmpf_init_set_si",
    "__gmpf_init_set_str",
    "__gmpf_init_set_ui",
    "__gmpf_inits",
    "__gmpf_inp_str",
    "__gmpf_integer_p",
    "__gmpf_mul",
    "__gmpf_mul_2exp",
    "__gmpf_mul_ui",
    "__gmpf_neg",
    "__gmpf_out_str",
    "__gmpf_pow_ui",
    "__gmpf_random2",
    "__gmpf_reldiff",
    "__gmpf_rrandomb",
    "__gmpf_set",
    "__gmpf_set_d",
    "__gmpf_set_default_prec",
    "__gmpf_set_prec",
    "__gmpf_set_prec_raw",
    "__gmpf_set_q",
    "__gmpf_set_si",
    "__gmpf_set_str",
    "__gmpf_set_ui",
    "__gmpf_set_z",
    "__gmpf_size",
    "__gmpf_sqrt",
    "__gmpf_sqrt_ui",
    "__gmpf_sub",
    "__gmpf_sub_ui",
    "__gmpf_swap",
    "__gmpf_trunc",
    "__gmpf_ui_div",
    "__gmpf_ui_sub",
    "__gmpf_urandomb",
    "__gmpn_add",
    "__gmpn_add_1",
    "__gmpn_add_err1_n",
    "__gmpn_add_err2_n",
    "__gmpn_add_n",
    "__gmpn_addadd_n",
    "__gmpn_addmul_1",
    "__gmpn_addmul_2",
    "__gmpn_addsub_n",
    "__gmpn_and_n",
    "__gmpn_andn_n",
    "__gmpn_bases",
    "__gmpn_bc_set_str",
    "__gmpn_bdivmod",
    "__gmpn_binvert",
    "__gmpn_binvert_itch",
    "__gmpn_clz_tab",
    "__gmpn_cmp",
    "__gmpn_com_n",
    "__gmpn_copyd",
    "__gmpn_copyi",
    "__gmpn_dc_bdiv_q",
    "__gmpn_dc_bdiv_q_n",
    "__gmpn_dc_bdiv_qr",
    "__gmpn_dc_bdiv_qr_n",
    "__gmpn_dc_div_q",
    "__gmpn_dc_div_qr",
    "__gmpn_dc_div_qr_n",
    "__gmpn_dc_divappr_q",
    "__gmpn_dc_set_str",
    "__gmpn_div_2expmod_2expp1",
    "__gmpn_divexact",
    "__gmpn_divexact_1",
    "__gmpn_divexact_by3c",
    "__gmpn_divexact_byff",
    "__gmpn_divexact_byfobm1",
    "__gmpn_divisible_p",
    "__gmpn_divrem",
    "__gmpn_divrem_1",
    "__gmpn_divrem_2",
    "__gmpn_divrem_euclidean_qr_1",
    "__gmpn_divrem_euclidean_qr_2",
    "__gmpn_divrem_euclidean_r_1",
    "__gmpn_divrem_hensel_qr_1",
    "__gmpn_divrem_hensel_qr_1_1",
    "__gmpn_divrem_hensel_qr_1_2",
    "__gmpn_divrem_hensel_r_1",
    "__gmpn_divrem_hensel_rsh_qr_1",
    "__gmpn_divrem_hensel_rsh_qr_1_preinv",
    "__gmpn_dump",
    "__gmpn_fib2_ui",
    "__gmpn_gcd",
    "__gmpn_gcd_1",
    "__gmpn_gcd_subdiv_step",
    "__gmpn_gcdext",
    "__gmpn_gcdext_1",
    "__gmpn_gcdext_hook",
    "__gmpn_gcdext_lehmer_n",
    "__gmpn_get_d",
    "__gmpn_get_str",
    "__gmpn_hamdist",
    "__gmpn_hgcd",
    "__gmpn_hgcd2",
    "__gmpn_hgcd2_jacobi",
    "__gmpn_hgcd_appr",
    "__gmpn_hgcd_appr_itch",
    "__gmpn_hgcd_itch",
    "__gmpn_hgcd_jacobi",
    "__gmpn_hgcd_matrix_adjust",
    "__gmpn_hgcd_matrix_init",
    "__gmpn_hgcd_matrix_mul",
    "__gmpn_hgcd_matrix_mul_1",
    "__gmpn_hgcd_matrix_update_q",
    "__gmpn_hgcd_mul_matrix1_vector",
    "__gmpn_hgcd_reduce",
    "__gmpn_hgcd_reduce_itch",
    "__gmpn_hgcd_step",
    "__gmpn_inv_div_q",
    "__gmpn_inv_div_qr",
    "__gmpn_inv_div_qr_n",
    "__gmpn_inv_divappr_q",
    "__gmpn_inv_divappr_q_n",
    "__gmpn_invert",
    "__gmpn_invert_trunc",
    "__gmpn_ior_n",
    "__gmpn_iorn_n",
    "__gmpn_is_invert",
    "__gmpn_jacobi_2",
    "__gmpn_jacobi_base",
    "__gmpn_jacobi_n",
    "__gmpn_kara_mul_n",
    "__gmpn_kara_sqr_n",
    "__gmpn_lshift",
    "__gmpn_matrix22_mul",
    "__gmpn_matrix22_mul1_inverse_vector",
    "__gmpn_matrix22_mul_itch",
    "__gmpn_matrix22_mul_strassen",
    "__gmpn_mod_1",
    "__gmpn_mod_1_1",
    "__gmpn_mod_1_2",
    "__gmpn_mod_1_3",
    "__gmpn_mod_1_k",
    "__gmpn_mod_34lsub1",
    "__gmpn_modexact_1c_odd",
    "__gmpn_mul",
    "__gmpn_mul_1",
    "__gmpn_mul_2expmod_2expp1",
    "__gmpn_mul_basecase",
    "__gmpn_mul_fft",
    "__gmpn_mul_fft_main",
    "__gmpn_mul_mfa_trunc_sqrt2",
    "__gmpn_mul_n",
    "__gmpn_mul_trunc_sqrt2",
    "__gmpn_mulhigh_n",
    "__gmpn_mullow_basecase",
    "__gmpn_mullow_n",
    "__gmpn_mullow_n_basecase",
    "__gmpn_mulmid",
    "__gmpn_mulmid_basecase",
    "__gmpn_mulmid_n",
    "__gmpn_mulmod_2expm1",
    "__gmpn_mulmod_2expp1_basecase",
    "__gmpn_mulmod_Bexpp1",
    "__gmpn_mulmod_Bexpp1_fft",
    "__gmpn_mulmod_bnm1",
    "__gmpn_nand_n",
    "__gmpn_nior_n",
    "__gmpn_normmod_2expp1",
    "__gmpn_nsumdiff_n",
    "__gmpn_perfect_square_p",
    "__gmpn_popcount",
    "__gmpn_pow_1",
    "__gmpn_powlo",
    "__gmpn_powm",
    "__gmpn_preinv_divrem_1",
    "__gmpn_preinv_mod_1",
    "__gmpn_random",
    "__gmpn_random2",
    "__gmpn_randomb",
    "__gmpn_redc_1",
    "__gmpn_redc_2",
    "__gmpn_redc_n",
    "__gmpn_rootrem",
    "__gmpn_rootrem_basecase",
    "__gmpn_rrandom",
    "__gmpn_rsh_divrem_hensel_qr_1",
    "__gmpn_rsh_divrem_hensel_qr_1_1",
    "__gmpn_rsh_divrem_hensel_qr_1_2",
    "__gmpn_rshift",
    "__gmpn_sb_bdiv_q",
    "__gmpn_sb_bdiv_qr",
    "__gmpn_sb_div_q",
    "__gmpn_sb_div_qr",
    "__gmpn_sb_divappr_q",
    "__gmpn_scan0",
    "__gmpn_scan1",
    "__gmpn_set_str",
    "__gmpn_set_str_compute_powtab",
    "__gmpn_sizeinbase",
    "__gmpn_sqr",
    "__gmpn_sqr_basecase",
    "__gmpn_sqrtrem",
    "__gmpn_sub",
    "__gmpn_sub_1",
    "__gmpn_sub_err1_n",
    "__gmpn_sub_err2_n",
    "__gmpn_sub_n",
    "__gmpn_subadd_n",
    "__gmpn_submul_1",
    "__gmpn_sumdiff_n",
    "__gmpn_tdiv_q",
    "__gmpn_tdiv_qr",
    "__gmpn_toom32_mul",
    "__gmpn_toom3_interpolate",
    "__gmpn_toom3_mul",
    "__gmpn_toom3_mul_n",
    "__gmpn_toom3_sqr_n",
    "__gmpn_toom42_mul",
    "__gmpn_toom42_mulmid",
    "__gmpn_toom4_interpolate",
    "__gmpn_toom4_mul",
    "__gmpn_toom4_mul_n",
    "__gmpn_toom4_sqr_n",
    "__gmpn_toom53_mul",
    "__gmpn_toom8_sqr_n",
    "__gmpn_toom8h_mul",
    "__gmpn_toom_couple_handling",
    "__gmpn_toom_eval_dgr3_pm1",
    "__gmpn_toom_eval_dgr3_pm2",
    "__gmpn_toom_eval_pm1",
    "__gmpn_toom_eval_pm2",
    "__gmpn_toom_eval_pm2exp",
    "__gmpn_toom_eval_pm2rexp",
    "__gmpn_toom_interpolate_16pts",
    "__gmpn_urandomb",
    "__gmpn_urandomm",
    "__gmpn_xnor_n",
    "__gmpn_xor_n",
    "__gmpn_zero",
    "__gmpn_zero_p",
    "__gmpq_abs",
    "__gmpq_add",
    "__gmpq_canonicalize",
    "__gmpq_clear",
    "__gmpq_clears",
    "__gmpq_cmp",
    "__gmpq_cmp_si",
    "__gmpq_cmp_ui",
    "__gmpq_cmp_z",
    "__gmpq_div",
    "__gmpq_div_2exp",
    "__gmpq_equal",
    "__gmpq_get_d",
    "__gmpq_get_den",
    "__gmpq_get_num",
    "__gmpq_get_str",
    "__gmpq_init",
    "__gmpq_inits",
    "__gmpq_inp_str",
    "__gmpq_inv",
    "__gmpq_mul",
    "__gmpq_mul_2exp",
    "__gmpq_neg",
    "__gmpq_out_str",
    "__gmpq_set",
    "__gmpq_set_d",
    "__gmpq_set_den",
    "__gmpq_set_f",
    "__gmpq_set_num",
    "__gmpq_set_si",
    "__gmpq_set_str",
    "__gmpq_set_ui",
    "__gmpq_set_z",
    "__gmpq_sub",
    "__gmpq_swap",
    "__gmpz_2fac_ui",
    "__gmpz_abs",
    "__gmpz_add",
    "__gmpz_add_ui",
    "__gmpz_addmul",
    "__gmpz_addmul_ui",
    "__gmpz_and",
    "__gmpz_aorsmul_1",
    "__gmpz_array_init",
    "__gmpz_bin_ui",
    "__gmpz_bin_uiui",
    "__gmpz_cdiv_q",
    "__gmpz_cdiv_q_2exp",
    "__gmpz_cdiv_q_ui",
    "__gmpz_cdiv_qr",
    "__gmpz_cdiv_qr_ui",
    "__gmpz_cdiv_r",
    "__gmpz_cdiv_r_2exp",
    "__gmpz_cdiv_r_ui",
    "__gmpz_cdiv_ui",
    "__gmpz_clear",
    "__gmpz_clears",
    "__gmpz_clrbit",
    "__gmpz_cmp",
    "__gmpz_cmp_d",
    "__gmpz_cmp_si",
    "__gmpz_cmp_ui",
    "__gmpz_cmpabs",
    "__gmpz_cmpabs_d",
    "__gmpz_cmpabs_ui",
    "__gmpz_com",
    "__gmpz_combit",
    "__gmpz_congruent_2exp_p",
    "__gmpz_congruent_p",
    "__gmpz_congruent_ui_p",
    "__gmpz_divexact",
    "__gmpz_divexact_gcd",
    "__gmpz_divexact_ui",
    "__gmpz_divisible_2exp_p",
    "__gmpz_divisible_p",
    "__gmpz_divisible_ui_p",
    "__gmpz_dump",
    "__gmpz_export",
    "__gmpz_fac_ui",
    "__gmpz_fdiv_q",
    "__gmpz_fdiv_q_2exp",
    "__gmpz_fdiv_q_ui",
    "__gmpz_fdiv_qr",
    "__gmpz_fdiv_qr_ui",
    "__gmpz_fdiv_r",
    "__gmpz_fdiv_r_2exp",
    "__gmpz_fdiv_r_ui",
    "__gmpz_fdiv_ui",
    "__gmpz_fib2_ui",
    "__gmpz_fib_ui",
    "__gmpz_fits_si_p",
    "__gmpz_fits_sint_p",
    "__gmpz_fits_slong_p",
    "__gmpz_fits_sshort_p",
    "__gmpz_fits_ui_p",
    "__gmpz_fits_uint_p",
    "__gmpz_fits_ulong_p",
    "__gmpz_fits_ushort_p",
    "__gmpz_gcd",
    "__gmpz_gcd_ui",
    "__gmpz_gcdext",
    "__gmpz_get_2exp_d",
    "__gmpz_get_d",
    "__gmpz_get_d_2exp",
    "__gmpz_get_si",
    "__gmpz_get_str",
    "__gmpz_get_sx",
    "__gmpz_get_ui",
    "__gmpz_get_ux",
    "__gmpz_getlimbn",
    "__gmpz_hamdist",
    "__gmpz_import",
    "__gmpz_init",
    "__gmpz_init2",
    "__gmpz_init_set",
    "__gmpz_init_set_d",
    "__gmpz_init_set_si",
    "__gmpz_init_set_str",
    "__gmpz_init_set_sx",
    "__gmpz_init_set_ui",
    "__gmpz_init_set_ux",
    "__gmpz_inits",
    "__gmpz_inp_raw",
    "__gmpz_inp_str",
    "__gmpz_inp_str_nowhite",
    "__gmpz_invert",
    "__gmpz_ior",
    "__gmpz_jacobi",
    "__gmpz_kronecker_si",
    "__gmpz_kronecker_ui",
    "__gmpz_lcm",
    "__gmpz_lcm_ui",
    "__gmpz_likely_prime_p",
    "__gmpz_limbs_finish",
    "__gmpz_limbs_modify",
    "__gmpz_limbs_read",
    "__gmpz_limbs_write",
    "__gmpz_lucnum2_ui",
    "__gmpz_lucnum_ui",
    "__gmpz_mfac_uiui",
    "__gmpz_miller_rabin",
    "__gmpz_millerrabin",
    "__gmpz_mod",
    "__gmpz_mul",
    "__gmpz_mul_2exp",
    "__gmpz_mul_si",
    "__gmpz_mul_ui",
    "__gmpz_n_pow_ui",
    "__gmpz_neg",
    "__gmpz_next_prime_candidate",
    "__gmpz_nextprime",
    "__gmpz_nthroot",
    "__gmpz_oddfac_1",
    "__gmpz_out_raw",
    "__gmpz_out_str",
    "__gmpz_perfect_power_p",
    "__gmpz_perfect_square_p",
    "__gmpz_popcount",
    "__gmpz_pow_ui",
    "__gmpz_powm",
    "__gmpz_powm_ui",
    "__gmpz_primorial_ui",
    "__gmpz_probab_prime_p",
    "__gmpz_probable_prime_p",
    "__gmpz_prodlimbs",
    "__gmpz_realloc",
    "__gmpz_realloc2",
    "__gmpz_remove",
    "__gmpz_roinit_n",
    "__gmpz_root",
    "__gmpz_rootrem",
    "__gmpz_rrandomb",
    "__gmpz_scan0",
    "__gmpz_scan1",
    "__gmpz_set",
    "__gmpz_set_d",
    "__gmpz_set_f",
    "__gmpz_set_q",
    "__gmpz_set_si",
    "__gmpz_set_str",
    "__gmpz_set_sx",
    "__gmpz_set_ui",
    "__gmpz_set_ux",
    "__gmpz_setbit",
    "__gmpz_si_kronecker",
    "__gmpz_size",
    "__gmpz_sizeinbase",
    "__gmpz_sqrt",
    "__gmpz_sqrtrem",
    "__gmpz_sub",
    "__gmpz_sub_ui",
    "__gmpz_submul",
    "__gmpz_submul_ui",
    "__gmpz_swap",
    "__gmpz_tdiv_q",
    "__gmpz_tdiv_q_2exp",
    "__gmpz_tdiv_q_ui",
    "__gmpz_tdiv_qr",
    "__gmpz_tdiv_qr_ui",
    "__gmpz_tdiv_r",
    "__gmpz_tdiv_r_2exp",
    "__gmpz_tdiv_r_ui",
    "__gmpz_tdiv_ui",
    "__gmpz_trial_division",
    "__gmpz_tstbit",
    "__gmpz_ui_kronecker",
    "__gmpz_ui_pow_ui",
    "__gmpz_ui_sub",
    "__gmpz_urandomb",
    "__gmpz_urandomm",
    "__gmpz_xor",
    "__mpir_butterfly_lshB",
    "__mpir_butterfly_rshB",
    "__mpir_fft_adjust",
    "__mpir_fft_adjust_limbs",
    "__mpir_fft_adjust_sqrt2",
    "__mpir_fft_butterfly",
    "__mpir_fft_butterfly_sqrt2",
    "__mpir_fft_butterfly_twiddle",
    "__mpir_fft_combine_bits",
    "__mpir_fft_mfa_trunc_sqrt2",
    "__mpir_fft_mfa_trunc_sqrt2_inner",
    "__mpir_fft_mfa_trunc_sqrt2_outer",
    "__mpir_fft_mulmod_2expp1",
    "__mpir_fft_naive_convolution_1",
    "__mpir_fft_negacyclic",
    "__mpir_fft_radix2",
    "__mpir_fft_radix2_twiddle",
    "__mpir_fft_split_bits",
    "__mpir_fft_split_limbs",
    "__mpir_fft_trunc",
    "__mpir_fft_trunc1",
    "__mpir_fft_trunc1_twiddle",
    "__mpir_fft_trunc_sqrt2",
    "__mpir_ifft_butterfly",
    "__mpir_ifft_butterfly_sqrt2",
    "__mpir_ifft_butterfly_twiddle",
    "__mpir_ifft_mfa_trunc_sqrt2",
    "__mpir_ifft_mfa_trunc_sqrt2_outer",
    "__mpir_ifft_negacyclic",
    "__mpir_ifft_radix2",
    "__mpir_ifft_radix2_twiddle",
    "__mpir_ifft_trunc",
    "__mpir_ifft_trunc1",
    "__mpir_ifft_trunc1_twiddle",
    "__mpir_ifft_trunc_sqrt2",
    "__mpir_revbin",
    "__mpir_version",
    "mpir_is_likely_prime_BPSW",
    "mpir_sqrt",
    "mpz_inp_raw_m",
    "mpz_inp_raw_p",
    "mpz_out_raw_m",
};

extern "C" UINT_PTR mProcs[600] = {0};
extern "C" void PROXY___combine_limbs();
extern "C" void PROXY___fermat_to_mpz();
extern "C" void PROXY___gmp_0();
extern "C" void PROXY___gmp_allocate_func();
extern "C" void PROXY___gmp_asprintf();
extern "C" void PROXY___gmp_asprintf_final();
extern "C" void PROXY___gmp_asprintf_memory();
extern "C" void PROXY___gmp_asprintf_reps();
extern "C" void PROXY___gmp_assert_fail();
extern "C" void PROXY___gmp_assert_header();
extern "C" void PROXY___gmp_bits_per_limb();
extern "C" void PROXY___gmp_default_allocate();
extern "C" void PROXY___gmp_default_fp_limb_precision();
extern "C" void PROXY___gmp_default_free();
extern "C" void PROXY___gmp_default_reallocate();
extern "C" void PROXY___gmp_digit_value_tab();
extern "C" void PROXY___gmp_divide_by_zero();
extern "C" void PROXY___gmp_doprnt();
extern "C" void PROXY___gmp_doprnt_integer();
extern "C" void PROXY___gmp_doprnt_mpf2();
extern "C" void PROXY___gmp_doscan();
extern "C" void PROXY___gmp_errno();
extern "C" void PROXY___gmp_exception();
extern "C" void PROXY___gmp_extract_double();
extern "C" void PROXY___gmp_fib_table();
extern "C" void PROXY___gmp_fprintf();
extern "C" void PROXY___gmp_free_func();
extern "C" void PROXY___gmp_fscanf();
extern "C" void PROXY___gmp_get_memory_functions();
extern "C" void PROXY___gmp_init_primesieve();
extern "C" void PROXY___gmp_invalid_operation();
extern "C" void PROXY___gmp_jacobi_table();
extern "C" void PROXY___gmp_junk();
extern "C" void PROXY___gmp_modlimb_invert_table();
extern "C" void PROXY___gmp_nextprime();
extern "C" void PROXY___gmp_primesieve();
extern "C" void PROXY___gmp_printf();
extern "C" void PROXY___gmp_randclear();
extern "C" void PROXY___gmp_randinit_default();
extern "C" void PROXY___gmp_randinit_lc_2exp();
extern "C" void PROXY___gmp_randinit_lc_2exp_size();
extern "C" void PROXY___gmp_randinit_mt();
extern "C" void PROXY___gmp_randinit_mt_noseed();
extern "C" void PROXY___gmp_randinit_set();
extern "C" void PROXY___gmp_rands();
extern "C" void PROXY___gmp_rands_initialized();
extern "C" void PROXY___gmp_randseed();
extern "C" void PROXY___gmp_randseed_ui();
extern "C" void PROXY___gmp_reallocate_func();
extern "C" void PROXY___gmp_replacement_vsnprintf();
extern "C" void PROXY___gmp_scanf();
extern "C" void PROXY___gmp_set_memory_functions();
extern "C" void PROXY___gmp_snprintf();
extern "C" void PROXY___gmp_sprintf();
extern "C" void PROXY___gmp_sqrt_of_negative();
extern "C" void PROXY___gmp_sscanf();
extern "C" void PROXY___gmp_tmp_reentrant_alloc();
extern "C" void PROXY___gmp_tmp_reentrant_free();
extern "C" void PROXY___gmp_urandomb_ui();
extern "C" void PROXY___gmp_urandomm_ui();
extern "C" void PROXY___gmp_vasprintf();
extern "C" void PROXY___gmp_version();
extern "C" void PROXY___gmp_vfprintf();
extern "C" void PROXY___gmp_vfscanf();
extern "C" void PROXY___gmp_vprintf();
extern "C" void PROXY___gmp_vscanf();
extern "C" void PROXY___gmp_vsnprintf();
extern "C" void PROXY___gmp_vsprintf();
extern "C" void PROXY___gmp_vsscanf();
extern "C" void PROXY___gmpf_abs();
extern "C" void PROXY___gmpf_add();
extern "C" void PROXY___gmpf_add_ui();
extern "C" void PROXY___gmpf_ceil();
extern "C" void PROXY___gmpf_clear();
extern "C" void PROXY___gmpf_clears();
extern "C" void PROXY___gmpf_cmp();
extern "C" void PROXY___gmpf_cmp_d();
extern "C" void PROXY___gmpf_cmp_si();
extern "C" void PROXY___gmpf_cmp_ui();
extern "C" void PROXY___gmpf_cmp_z();
extern "C" void PROXY___gmpf_div();
extern "C" void PROXY___gmpf_div_2exp();
extern "C" void PROXY___gmpf_div_ui();
extern "C" void PROXY___gmpf_dump();
extern "C" void PROXY___gmpf_eq();
extern "C" void PROXY___gmpf_fits_si_p();
extern "C" void PROXY___gmpf_fits_sint_p();
extern "C" void PROXY___gmpf_fits_slong_p();
extern "C" void PROXY___gmpf_fits_sshort_p();
extern "C" void PROXY___gmpf_fits_ui_p();
extern "C" void PROXY___gmpf_fits_uint_p();
extern "C" void PROXY___gmpf_fits_ulong_p();
extern "C" void PROXY___gmpf_fits_ushort_p();
extern "C" void PROXY___gmpf_floor();
extern "C" void PROXY___gmpf_get_2exp_d();
extern "C" void PROXY___gmpf_get_d();
extern "C" void PROXY___gmpf_get_d_2exp();
extern "C" void PROXY___gmpf_get_default_prec();
extern "C" void PROXY___gmpf_get_prec();
extern "C" void PROXY___gmpf_get_si();
extern "C" void PROXY___gmpf_get_str();
extern "C" void PROXY___gmpf_get_ui();
extern "C" void PROXY___gmpf_init();
extern "C" void PROXY___gmpf_init2();
extern "C" void PROXY___gmpf_init_set();
extern "C" void PROXY___gmpf_init_set_d();
extern "C" void PROXY___gmpf_init_set_si();
extern "C" void PROXY___gmpf_init_set_str();
extern "C" void PROXY___gmpf_init_set_ui();
extern "C" void PROXY___gmpf_inits();
extern "C" void PROXY___gmpf_inp_str();
extern "C" void PROXY___gmpf_integer_p();
extern "C" void PROXY___gmpf_mul();
extern "C" void PROXY___gmpf_mul_2exp();
extern "C" void PROXY___gmpf_mul_ui();
extern "C" void PROXY___gmpf_neg();
extern "C" void PROXY___gmpf_out_str();
extern "C" void PROXY___gmpf_pow_ui();
extern "C" void PROXY___gmpf_random2();
extern "C" void PROXY___gmpf_reldiff();
extern "C" void PROXY___gmpf_rrandomb();
extern "C" void PROXY___gmpf_set();
extern "C" void PROXY___gmpf_set_d();
extern "C" void PROXY___gmpf_set_default_prec();
extern "C" void PROXY___gmpf_set_prec();
extern "C" void PROXY___gmpf_set_prec_raw();
extern "C" void PROXY___gmpf_set_q();
extern "C" void PROXY___gmpf_set_si();
extern "C" void PROXY___gmpf_set_str();
extern "C" void PROXY___gmpf_set_ui();
extern "C" void PROXY___gmpf_set_z();
extern "C" void PROXY___gmpf_size();
extern "C" void PROXY___gmpf_sqrt();
extern "C" void PROXY___gmpf_sqrt_ui();
extern "C" void PROXY___gmpf_sub();
extern "C" void PROXY___gmpf_sub_ui();
extern "C" void PROXY___gmpf_swap();
extern "C" void PROXY___gmpf_trunc();
extern "C" void PROXY___gmpf_ui_div();
extern "C" void PROXY___gmpf_ui_sub();
extern "C" void PROXY___gmpf_urandomb();
extern "C" void PROXY___gmpn_add();
extern "C" void PROXY___gmpn_add_1();
extern "C" void PROXY___gmpn_add_err1_n();
extern "C" void PROXY___gmpn_add_err2_n();
extern "C" void PROXY___gmpn_add_n();
extern "C" void PROXY___gmpn_addadd_n();
extern "C" void PROXY___gmpn_addmul_1();
extern "C" void PROXY___gmpn_addmul_2();
extern "C" void PROXY___gmpn_addsub_n();
extern "C" void PROXY___gmpn_and_n();
extern "C" void PROXY___gmpn_andn_n();
extern "C" void PROXY___gmpn_bases();
extern "C" void PROXY___gmpn_bc_set_str();
extern "C" void PROXY___gmpn_bdivmod();
extern "C" void PROXY___gmpn_binvert();
extern "C" void PROXY___gmpn_binvert_itch();
extern "C" void PROXY___gmpn_clz_tab();
extern "C" void PROXY___gmpn_cmp();
extern "C" void PROXY___gmpn_com_n();
extern "C" void PROXY___gmpn_copyd();
extern "C" void PROXY___gmpn_copyi();
extern "C" void PROXY___gmpn_dc_bdiv_q();
extern "C" void PROXY___gmpn_dc_bdiv_q_n();
extern "C" void PROXY___gmpn_dc_bdiv_qr();
extern "C" void PROXY___gmpn_dc_bdiv_qr_n();
extern "C" void PROXY___gmpn_dc_div_q();
extern "C" void PROXY___gmpn_dc_div_qr();
extern "C" void PROXY___gmpn_dc_div_qr_n();
extern "C" void PROXY___gmpn_dc_divappr_q();
extern "C" void PROXY___gmpn_dc_set_str();
extern "C" void PROXY___gmpn_div_2expmod_2expp1();
extern "C" void PROXY___gmpn_divexact();
extern "C" void PROXY___gmpn_divexact_1();
extern "C" void PROXY___gmpn_divexact_by3c();
extern "C" void PROXY___gmpn_divexact_byff();
extern "C" void PROXY___gmpn_divexact_byfobm1();
extern "C" void PROXY___gmpn_divisible_p();
extern "C" void PROXY___gmpn_divrem();
extern "C" void PROXY___gmpn_divrem_1();
extern "C" void PROXY___gmpn_divrem_2();
extern "C" void PROXY___gmpn_divrem_euclidean_qr_1();
extern "C" void PROXY___gmpn_divrem_euclidean_qr_2();
extern "C" void PROXY___gmpn_divrem_euclidean_r_1();
extern "C" void PROXY___gmpn_divrem_hensel_qr_1();
extern "C" void PROXY___gmpn_divrem_hensel_qr_1_1();
extern "C" void PROXY___gmpn_divrem_hensel_qr_1_2();
extern "C" void PROXY___gmpn_divrem_hensel_r_1();
extern "C" void PROXY___gmpn_divrem_hensel_rsh_qr_1();
extern "C" void PROXY___gmpn_divrem_hensel_rsh_qr_1_preinv();
extern "C" void PROXY___gmpn_dump();
extern "C" void PROXY___gmpn_fib2_ui();
extern "C" void PROXY___gmpn_gcd();
extern "C" void PROXY___gmpn_gcd_1();
extern "C" void PROXY___gmpn_gcd_subdiv_step();
extern "C" void PROXY___gmpn_gcdext();
extern "C" void PROXY___gmpn_gcdext_1();
extern "C" void PROXY___gmpn_gcdext_hook();
extern "C" void PROXY___gmpn_gcdext_lehmer_n();
extern "C" void PROXY___gmpn_get_d();
extern "C" void PROXY___gmpn_get_str();
extern "C" void PROXY___gmpn_hamdist();
extern "C" void PROXY___gmpn_hgcd();
extern "C" void PROXY___gmpn_hgcd2();
extern "C" void PROXY___gmpn_hgcd2_jacobi();
extern "C" void PROXY___gmpn_hgcd_appr();
extern "C" void PROXY___gmpn_hgcd_appr_itch();
extern "C" void PROXY___gmpn_hgcd_itch();
extern "C" void PROXY___gmpn_hgcd_jacobi();
extern "C" void PROXY___gmpn_hgcd_matrix_adjust();
extern "C" void PROXY___gmpn_hgcd_matrix_init();
extern "C" void PROXY___gmpn_hgcd_matrix_mul();
extern "C" void PROXY___gmpn_hgcd_matrix_mul_1();
extern "C" void PROXY___gmpn_hgcd_matrix_update_q();
extern "C" void PROXY___gmpn_hgcd_mul_matrix1_vector();
extern "C" void PROXY___gmpn_hgcd_reduce();
extern "C" void PROXY___gmpn_hgcd_reduce_itch();
extern "C" void PROXY___gmpn_hgcd_step();
extern "C" void PROXY___gmpn_inv_div_q();
extern "C" void PROXY___gmpn_inv_div_qr();
extern "C" void PROXY___gmpn_inv_div_qr_n();
extern "C" void PROXY___gmpn_inv_divappr_q();
extern "C" void PROXY___gmpn_inv_divappr_q_n();
extern "C" void PROXY___gmpn_invert();
extern "C" void PROXY___gmpn_invert_trunc();
extern "C" void PROXY___gmpn_ior_n();
extern "C" void PROXY___gmpn_iorn_n();
extern "C" void PROXY___gmpn_is_invert();
extern "C" void PROXY___gmpn_jacobi_2();
extern "C" void PROXY___gmpn_jacobi_base();
extern "C" void PROXY___gmpn_jacobi_n();
extern "C" void PROXY___gmpn_kara_mul_n();
extern "C" void PROXY___gmpn_kara_sqr_n();
extern "C" void PROXY___gmpn_lshift();
extern "C" void PROXY___gmpn_matrix22_mul();
extern "C" void PROXY___gmpn_matrix22_mul1_inverse_vector();
extern "C" void PROXY___gmpn_matrix22_mul_itch();
extern "C" void PROXY___gmpn_matrix22_mul_strassen();
extern "C" void PROXY___gmpn_mod_1();
extern "C" void PROXY___gmpn_mod_1_1();
extern "C" void PROXY___gmpn_mod_1_2();
extern "C" void PROXY___gmpn_mod_1_3();
extern "C" void PROXY___gmpn_mod_1_k();
extern "C" void PROXY___gmpn_mod_34lsub1();
extern "C" void PROXY___gmpn_modexact_1c_odd();
extern "C" void PROXY___gmpn_mul();
extern "C" void PROXY___gmpn_mul_1();
extern "C" void PROXY___gmpn_mul_2expmod_2expp1();
extern "C" void PROXY___gmpn_mul_basecase();
extern "C" void PROXY___gmpn_mul_fft();
extern "C" void PROXY___gmpn_mul_fft_main();
extern "C" void PROXY___gmpn_mul_mfa_trunc_sqrt2();
extern "C" void PROXY___gmpn_mul_n();
extern "C" void PROXY___gmpn_mul_trunc_sqrt2();
extern "C" void PROXY___gmpn_mulhigh_n();
extern "C" void PROXY___gmpn_mullow_basecase();
extern "C" void PROXY___gmpn_mullow_n();
extern "C" void PROXY___gmpn_mullow_n_basecase();
extern "C" void PROXY___gmpn_mulmid();
extern "C" void PROXY___gmpn_mulmid_basecase();
extern "C" void PROXY___gmpn_mulmid_n();
extern "C" void PROXY___gmpn_mulmod_2expm1();
extern "C" void PROXY___gmpn_mulmod_2expp1_basecase();
extern "C" void PROXY___gmpn_mulmod_Bexpp1();
extern "C" void PROXY___gmpn_mulmod_Bexpp1_fft();
extern "C" void PROXY___gmpn_mulmod_bnm1();
extern "C" void PROXY___gmpn_nand_n();
extern "C" void PROXY___gmpn_nior_n();
extern "C" void PROXY___gmpn_normmod_2expp1();
extern "C" void PROXY___gmpn_nsumdiff_n();
extern "C" void PROXY___gmpn_perfect_square_p();
extern "C" void PROXY___gmpn_popcount();
extern "C" void PROXY___gmpn_pow_1();
extern "C" void PROXY___gmpn_powlo();
extern "C" void PROXY___gmpn_powm();
extern "C" void PROXY___gmpn_preinv_divrem_1();
extern "C" void PROXY___gmpn_preinv_mod_1();
extern "C" void PROXY___gmpn_random();
extern "C" void PROXY___gmpn_random2();
extern "C" void PROXY___gmpn_randomb();
extern "C" void PROXY___gmpn_redc_1();
extern "C" void PROXY___gmpn_redc_2();
extern "C" void PROXY___gmpn_redc_n();
extern "C" void PROXY___gmpn_rootrem();
extern "C" void PROXY___gmpn_rootrem_basecase();
extern "C" void PROXY___gmpn_rrandom();
extern "C" void PROXY___gmpn_rsh_divrem_hensel_qr_1();
extern "C" void PROXY___gmpn_rsh_divrem_hensel_qr_1_1();
extern "C" void PROXY___gmpn_rsh_divrem_hensel_qr_1_2();
extern "C" void PROXY___gmpn_rshift();
extern "C" void PROXY___gmpn_sb_bdiv_q();
extern "C" void PROXY___gmpn_sb_bdiv_qr();
extern "C" void PROXY___gmpn_sb_div_q();
extern "C" void PROXY___gmpn_sb_div_qr();
extern "C" void PROXY___gmpn_sb_divappr_q();
extern "C" void PROXY___gmpn_scan0();
extern "C" void PROXY___gmpn_scan1();
extern "C" void PROXY___gmpn_set_str();
extern "C" void PROXY___gmpn_set_str_compute_powtab();
extern "C" void PROXY___gmpn_sizeinbase();
extern "C" void PROXY___gmpn_sqr();
extern "C" void PROXY___gmpn_sqr_basecase();
extern "C" void PROXY___gmpn_sqrtrem();
extern "C" void PROXY___gmpn_sub();
extern "C" void PROXY___gmpn_sub_1();
extern "C" void PROXY___gmpn_sub_err1_n();
extern "C" void PROXY___gmpn_sub_err2_n();
extern "C" void PROXY___gmpn_sub_n();
extern "C" void PROXY___gmpn_subadd_n();
extern "C" void PROXY___gmpn_submul_1();
extern "C" void PROXY___gmpn_sumdiff_n();
extern "C" void PROXY___gmpn_tdiv_q();
extern "C" void PROXY___gmpn_tdiv_qr();
extern "C" void PROXY___gmpn_toom32_mul();
extern "C" void PROXY___gmpn_toom3_interpolate();
extern "C" void PROXY___gmpn_toom3_mul();
extern "C" void PROXY___gmpn_toom3_mul_n();
extern "C" void PROXY___gmpn_toom3_sqr_n();
extern "C" void PROXY___gmpn_toom42_mul();
extern "C" void PROXY___gmpn_toom42_mulmid();
extern "C" void PROXY___gmpn_toom4_interpolate();
extern "C" void PROXY___gmpn_toom4_mul();
extern "C" void PROXY___gmpn_toom4_mul_n();
extern "C" void PROXY___gmpn_toom4_sqr_n();
extern "C" void PROXY___gmpn_toom53_mul();
extern "C" void PROXY___gmpn_toom8_sqr_n();
extern "C" void PROXY___gmpn_toom8h_mul();
extern "C" void PROXY___gmpn_toom_couple_handling();
extern "C" void PROXY___gmpn_toom_eval_dgr3_pm1();
extern "C" void PROXY___gmpn_toom_eval_dgr3_pm2();
extern "C" void PROXY___gmpn_toom_eval_pm1();
extern "C" void PROXY___gmpn_toom_eval_pm2();
extern "C" void PROXY___gmpn_toom_eval_pm2exp();
extern "C" void PROXY___gmpn_toom_eval_pm2rexp();
extern "C" void PROXY___gmpn_toom_interpolate_16pts();
extern "C" void PROXY___gmpn_urandomb();
extern "C" void PROXY___gmpn_urandomm();
extern "C" void PROXY___gmpn_xnor_n();
extern "C" void PROXY___gmpn_xor_n();
extern "C" void PROXY___gmpn_zero();
extern "C" void PROXY___gmpn_zero_p();
extern "C" void PROXY___gmpq_abs();
extern "C" void PROXY___gmpq_add();
extern "C" void PROXY___gmpq_canonicalize();
extern "C" void PROXY___gmpq_clear();
extern "C" void PROXY___gmpq_clears();
extern "C" void PROXY___gmpq_cmp();
extern "C" void PROXY___gmpq_cmp_si();
extern "C" void PROXY___gmpq_cmp_ui();
extern "C" void PROXY___gmpq_cmp_z();
extern "C" void PROXY___gmpq_div();
extern "C" void PROXY___gmpq_div_2exp();
extern "C" void PROXY___gmpq_equal();
extern "C" void PROXY___gmpq_get_d();
extern "C" void PROXY___gmpq_get_den();
extern "C" void PROXY___gmpq_get_num();
extern "C" void PROXY___gmpq_get_str();
extern "C" void PROXY___gmpq_init();
extern "C" void PROXY___gmpq_inits();
extern "C" void PROXY___gmpq_inp_str();
extern "C" void PROXY___gmpq_inv();
extern "C" void PROXY___gmpq_mul();
extern "C" void PROXY___gmpq_mul_2exp();
extern "C" void PROXY___gmpq_neg();
extern "C" void PROXY___gmpq_out_str();
extern "C" void PROXY___gmpq_set();
extern "C" void PROXY___gmpq_set_d();
extern "C" void PROXY___gmpq_set_den();
extern "C" void PROXY___gmpq_set_f();
extern "C" void PROXY___gmpq_set_num();
extern "C" void PROXY___gmpq_set_si();
extern "C" void PROXY___gmpq_set_str();
extern "C" void PROXY___gmpq_set_ui();
extern "C" void PROXY___gmpq_set_z();
extern "C" void PROXY___gmpq_sub();
extern "C" void PROXY___gmpq_swap();
extern "C" void PROXY___gmpz_2fac_ui();
extern "C" void PROXY___gmpz_abs();
extern "C" void PROXY___gmpz_add();
extern "C" void PROXY___gmpz_add_ui();
extern "C" void PROXY___gmpz_addmul();
extern "C" void PROXY___gmpz_addmul_ui();
extern "C" void PROXY___gmpz_and();
extern "C" void PROXY___gmpz_aorsmul_1();
extern "C" void PROXY___gmpz_array_init();
extern "C" void PROXY___gmpz_bin_ui();
extern "C" void PROXY___gmpz_bin_uiui();
extern "C" void PROXY___gmpz_cdiv_q();
extern "C" void PROXY___gmpz_cdiv_q_2exp();
extern "C" void PROXY___gmpz_cdiv_q_ui();
extern "C" void PROXY___gmpz_cdiv_qr();
extern "C" void PROXY___gmpz_cdiv_qr_ui();
extern "C" void PROXY___gmpz_cdiv_r();
extern "C" void PROXY___gmpz_cdiv_r_2exp();
extern "C" void PROXY___gmpz_cdiv_r_ui();
extern "C" void PROXY___gmpz_cdiv_ui();
extern "C" void PROXY___gmpz_clear();
extern "C" void PROXY___gmpz_clears();
extern "C" void PROXY___gmpz_clrbit();
extern "C" void PROXY___gmpz_cmp();
extern "C" void PROXY___gmpz_cmp_d();
extern "C" void PROXY___gmpz_cmp_si();
extern "C" void PROXY___gmpz_cmp_ui();
extern "C" void PROXY___gmpz_cmpabs();
extern "C" void PROXY___gmpz_cmpabs_d();
extern "C" void PROXY___gmpz_cmpabs_ui();
extern "C" void PROXY___gmpz_com();
extern "C" void PROXY___gmpz_combit();
extern "C" void PROXY___gmpz_congruent_2exp_p();
extern "C" void PROXY___gmpz_congruent_p();
extern "C" void PROXY___gmpz_congruent_ui_p();
extern "C" void PROXY___gmpz_divexact();
extern "C" void PROXY___gmpz_divexact_gcd();
extern "C" void PROXY___gmpz_divexact_ui();
extern "C" void PROXY___gmpz_divisible_2exp_p();
extern "C" void PROXY___gmpz_divisible_p();
extern "C" void PROXY___gmpz_divisible_ui_p();
extern "C" void PROXY___gmpz_dump();
extern "C" void PROXY___gmpz_export();
extern "C" void PROXY___gmpz_fac_ui();
extern "C" void PROXY___gmpz_fdiv_q();
extern "C" void PROXY___gmpz_fdiv_q_2exp();
extern "C" void PROXY___gmpz_fdiv_q_ui();
extern "C" void PROXY___gmpz_fdiv_qr();
extern "C" void PROXY___gmpz_fdiv_qr_ui();
extern "C" void PROXY___gmpz_fdiv_r();
extern "C" void PROXY___gmpz_fdiv_r_2exp();
extern "C" void PROXY___gmpz_fdiv_r_ui();
extern "C" void PROXY___gmpz_fdiv_ui();
extern "C" void PROXY___gmpz_fib2_ui();
extern "C" void PROXY___gmpz_fib_ui();
extern "C" void PROXY___gmpz_fits_si_p();
extern "C" void PROXY___gmpz_fits_sint_p();
extern "C" void PROXY___gmpz_fits_slong_p();
extern "C" void PROXY___gmpz_fits_sshort_p();
extern "C" void PROXY___gmpz_fits_ui_p();
extern "C" void PROXY___gmpz_fits_uint_p();
extern "C" void PROXY___gmpz_fits_ulong_p();
extern "C" void PROXY___gmpz_fits_ushort_p();
extern "C" void PROXY___gmpz_gcd();
extern "C" void PROXY___gmpz_gcd_ui();
extern "C" void PROXY___gmpz_gcdext();
extern "C" void PROXY___gmpz_get_2exp_d();
extern "C" void PROXY___gmpz_get_d();
extern "C" void PROXY___gmpz_get_d_2exp();
extern "C" void PROXY___gmpz_get_si();
extern "C" void PROXY___gmpz_get_str();
extern "C" void PROXY___gmpz_get_sx();
extern "C" void PROXY___gmpz_get_ui();
extern "C" void PROXY___gmpz_get_ux();
extern "C" void PROXY___gmpz_getlimbn();
extern "C" void PROXY___gmpz_hamdist();
extern "C" void PROXY___gmpz_import();
extern "C" void PROXY___gmpz_init();
extern "C" void PROXY___gmpz_init2();
extern "C" void PROXY___gmpz_init_set();
extern "C" void PROXY___gmpz_init_set_d();
extern "C" void PROXY___gmpz_init_set_si();
extern "C" void PROXY___gmpz_init_set_str();
extern "C" void PROXY___gmpz_init_set_sx();
extern "C" void PROXY___gmpz_init_set_ui();
extern "C" void PROXY___gmpz_init_set_ux();
extern "C" void PROXY___gmpz_inits();
extern "C" void PROXY___gmpz_inp_raw();
extern "C" void PROXY___gmpz_inp_str();
extern "C" void PROXY___gmpz_inp_str_nowhite();
extern "C" void PROXY___gmpz_invert();
extern "C" void PROXY___gmpz_ior();
extern "C" void PROXY___gmpz_jacobi();
extern "C" void PROXY___gmpz_kronecker_si();
extern "C" void PROXY___gmpz_kronecker_ui();
extern "C" void PROXY___gmpz_lcm();
extern "C" void PROXY___gmpz_lcm_ui();
extern "C" void PROXY___gmpz_likely_prime_p();
extern "C" void PROXY___gmpz_limbs_finish();
extern "C" void PROXY___gmpz_limbs_modify();
extern "C" void PROXY___gmpz_limbs_read();
extern "C" void PROXY___gmpz_limbs_write();
extern "C" void PROXY___gmpz_lucnum2_ui();
extern "C" void PROXY___gmpz_lucnum_ui();
extern "C" void PROXY___gmpz_mfac_uiui();
extern "C" void PROXY___gmpz_miller_rabin();
extern "C" void PROXY___gmpz_millerrabin();
extern "C" void PROXY___gmpz_mod();
extern "C" void PROXY___gmpz_mul();
extern "C" void PROXY___gmpz_mul_2exp();
extern "C" void PROXY___gmpz_mul_si();
extern "C" void PROXY___gmpz_mul_ui();
extern "C" void PROXY___gmpz_n_pow_ui();
extern "C" void PROXY___gmpz_neg();
extern "C" void PROXY___gmpz_next_prime_candidate();
extern "C" void PROXY___gmpz_nextprime();
extern "C" void PROXY___gmpz_nthroot();
extern "C" void PROXY___gmpz_oddfac_1();
extern "C" void PROXY___gmpz_out_raw();
extern "C" void PROXY___gmpz_out_str();
extern "C" void PROXY___gmpz_perfect_power_p();
extern "C" void PROXY___gmpz_perfect_square_p();
extern "C" void PROXY___gmpz_popcount();
extern "C" void PROXY___gmpz_pow_ui();
extern "C" void PROXY___gmpz_powm();
extern "C" void PROXY___gmpz_powm_ui();
extern "C" void PROXY___gmpz_primorial_ui();
extern "C" void PROXY___gmpz_probab_prime_p();
extern "C" void PROXY___gmpz_probable_prime_p();
extern "C" void PROXY___gmpz_prodlimbs();
extern "C" void PROXY___gmpz_realloc();
extern "C" void PROXY___gmpz_realloc2();
extern "C" void PROXY___gmpz_remove();
extern "C" void PROXY___gmpz_roinit_n();
extern "C" void PROXY___gmpz_root();
extern "C" void PROXY___gmpz_rootrem();
extern "C" void PROXY___gmpz_rrandomb();
extern "C" void PROXY___gmpz_scan0();
extern "C" void PROXY___gmpz_scan1();
extern "C" void PROXY___gmpz_set();
extern "C" void PROXY___gmpz_set_d();
extern "C" void PROXY___gmpz_set_f();
extern "C" void PROXY___gmpz_set_q();
extern "C" void PROXY___gmpz_set_si();
extern "C" void PROXY___gmpz_set_str();
extern "C" void PROXY___gmpz_set_sx();
extern "C" void PROXY___gmpz_set_ui();
extern "C" void PROXY___gmpz_set_ux();
extern "C" void PROXY___gmpz_setbit();
extern "C" void PROXY___gmpz_si_kronecker();
extern "C" void PROXY___gmpz_size();
extern "C" void PROXY___gmpz_sizeinbase();
extern "C" void PROXY___gmpz_sqrt();
extern "C" void PROXY___gmpz_sqrtrem();
extern "C" void PROXY___gmpz_sub();
extern "C" void PROXY___gmpz_sub_ui();
extern "C" void PROXY___gmpz_submul();
extern "C" void PROXY___gmpz_submul_ui();
extern "C" void PROXY___gmpz_swap();
extern "C" void PROXY___gmpz_tdiv_q();
extern "C" void PROXY___gmpz_tdiv_q_2exp();
extern "C" void PROXY___gmpz_tdiv_q_ui();
extern "C" void PROXY___gmpz_tdiv_qr();
extern "C" void PROXY___gmpz_tdiv_qr_ui();
extern "C" void PROXY___gmpz_tdiv_r();
extern "C" void PROXY___gmpz_tdiv_r_2exp();
extern "C" void PROXY___gmpz_tdiv_r_ui();
extern "C" void PROXY___gmpz_tdiv_ui();
extern "C" void PROXY___gmpz_trial_division();
extern "C" void PROXY___gmpz_tstbit();
extern "C" void PROXY___gmpz_ui_kronecker();
extern "C" void PROXY___gmpz_ui_pow_ui();
extern "C" void PROXY___gmpz_ui_sub();
extern "C" void PROXY___gmpz_urandomb();
extern "C" void PROXY___gmpz_urandomm();
extern "C" void PROXY___gmpz_xor();
extern "C" void PROXY___mpir_butterfly_lshB();
extern "C" void PROXY___mpir_butterfly_rshB();
extern "C" void PROXY___mpir_fft_adjust();
extern "C" void PROXY___mpir_fft_adjust_limbs();
extern "C" void PROXY___mpir_fft_adjust_sqrt2();
extern "C" void PROXY___mpir_fft_butterfly();
extern "C" void PROXY___mpir_fft_butterfly_sqrt2();
extern "C" void PROXY___mpir_fft_butterfly_twiddle();
extern "C" void PROXY___mpir_fft_combine_bits();
extern "C" void PROXY___mpir_fft_mfa_trunc_sqrt2();
extern "C" void PROXY___mpir_fft_mfa_trunc_sqrt2_inner();
extern "C" void PROXY___mpir_fft_mfa_trunc_sqrt2_outer();
extern "C" void PROXY___mpir_fft_mulmod_2expp1();
extern "C" void PROXY___mpir_fft_naive_convolution_1();
extern "C" void PROXY___mpir_fft_negacyclic();
extern "C" void PROXY___mpir_fft_radix2();
extern "C" void PROXY___mpir_fft_radix2_twiddle();
extern "C" void PROXY___mpir_fft_split_bits();
extern "C" void PROXY___mpir_fft_split_limbs();
extern "C" void PROXY___mpir_fft_trunc();
extern "C" void PROXY___mpir_fft_trunc1();
extern "C" void PROXY___mpir_fft_trunc1_twiddle();
extern "C" void PROXY___mpir_fft_trunc_sqrt2();
extern "C" void PROXY___mpir_ifft_butterfly();
extern "C" void PROXY___mpir_ifft_butterfly_sqrt2();
extern "C" void PROXY___mpir_ifft_butterfly_twiddle();
extern "C" void PROXY___mpir_ifft_mfa_trunc_sqrt2();
extern "C" void PROXY___mpir_ifft_mfa_trunc_sqrt2_outer();
extern "C" void PROXY___mpir_ifft_negacyclic();
extern "C" void PROXY___mpir_ifft_radix2();
extern "C" void PROXY___mpir_ifft_radix2_twiddle();
extern "C" void PROXY___mpir_ifft_trunc();
extern "C" void PROXY___mpir_ifft_trunc1();
extern "C" void PROXY___mpir_ifft_trunc1_twiddle();
extern "C" void PROXY___mpir_ifft_trunc_sqrt2();
extern "C" void PROXY___mpir_revbin();
extern "C" void PROXY___mpir_version();
extern "C" void PROXY_mpir_is_likely_prime_BPSW();
extern "C" void PROXY_mpir_sqrt();
extern "C" void PROXY_mpz_inp_raw_m();
extern "C" void PROXY_mpz_inp_raw_p();
extern "C" void PROXY_mpz_out_raw_m();

void GetCPU() {
    int cpuinfo[4];

    __cpuid(cpuinfo, 0);

    char vendor_string[13];

    vendor_string[0] = cpuinfo[1] & 0xff;
    vendor_string[1] = cpuinfo[1] >> 8 & 0xff;
    vendor_string[2] = cpuinfo[1] >> 16 & 0xff;
    vendor_string[3] = cpuinfo[1] >> 24 & 0xff;

    vendor_string[4] = cpuinfo[3] & 0xff;
    vendor_string[5] = cpuinfo[3] >> 8 & 0xff;
    vendor_string[6] = cpuinfo[3] >> 16 & 0xff;
    vendor_string[7] = cpuinfo[3] >> 24 & 0xff;

    vendor_string[8] = cpuinfo[2] & 0xff;
    vendor_string[9] = cpuinfo[2] >> 8 & 0xff;
    vendor_string[10] = cpuinfo[2] >> 16 & 0xff;
    vendor_string[11] = cpuinfo[2] >> 24 & 0xff;

    vendor_string[12] = 0;

    __cpuid(cpuinfo, 1);

    int family, model, stepping;

    family = ((cpuinfo[0] >> 8) & 15) + ((cpuinfo[0] >> 20) & 0xff);
    model = ((cpuinfo[0] >> 4) & 15) + ((cpuinfo[0] >> 12) & 0xf0);
    stepping = cpuinfo[0] & 15;

    const int AVX2 = 1 << 5;

    wcscpy_s(modelstr, sizeof(modelstr)/sizeof(modelstr[0]), L"gc");

    if (strcmp(vendor_string, "GenuineIntel") == 0) {
        switch (family) {
        case 6:
            switch (model) {
            case 42:
            case 45:
            case 58:
            case 62:
                wcscpy_s(modelstr, sizeof(modelstr)/sizeof(modelstr[0]), L"sandybridge");
                break;
            case 60:
            case 63:
            case 69:
            case 70:
                __cpuid(cpuinfo, 7);
                if ((cpuinfo[1] & AVX2) == AVX2)
                    wcscpy_s(modelstr, sizeof(modelstr)/sizeof(modelstr[0]), L"haswell");
                else
                    // Haswell non AVX broken - G1840 celeron with no AVX
                    wcscpy_s(modelstr, sizeof(modelstr)/sizeof(modelstr[0]), L"gc");
                break;
            case 61:
            case 71:
            case 79:
                wcscpy_s(modelstr, sizeof(modelstr)/sizeof(modelstr[0]), L"broadwell");
                __cpuid(cpuinfo, 7);
                if ((cpuinfo[1] & AVX2) == AVX2)
                    wcscat_s(modelstr, sizeof(modelstr)/sizeof(modelstr[0]), L"_avx");
                break;
            case 78:
            case 85:
            case 94:
            case 102:
            case 106:
            case 108:
            case 125:
            case 126:
            case 140:
            case 142:
            case 158:
            case 165:
                wcscpy_s(modelstr, sizeof(modelstr)/sizeof(modelstr[0]), L"skylake");
                __cpuid(cpuinfo, 7);
                if ((cpuinfo[1] & AVX2) == AVX2)
                    wcscat_s(modelstr, sizeof(modelstr)/sizeof(modelstr[0]), L"_avx");
                else
                    // Skylake non AVX broken https://github.com/wbhart/mpir/issues/274
                    wcscpy_s(modelstr, sizeof(modelstr)/sizeof(modelstr[0]), L"gc");
                break;
            }
        }
    }
    else if (strcmp(vendor_string, "AuthenticAMD") == 0) {
        switch (family) {
        case 21:
            switch (model) {
            case 1:
                wcscpy_s(modelstr, sizeof(modelstr)/sizeof(modelstr[0]), L"bulldozer");
                break;
            case 2:
            case 3:
            case 16:
            case 18:
            case 19:
                wcscpy_s(modelstr, sizeof(modelstr)/sizeof(modelstr[0]), L"piledriver");
                break;
            }
            break;
        }
    }
}

BOOL
WINAPI
DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        mHinst = hinstDLL;
        WCHAR RealDLL[MPIRPATHLEN + 1];
        GetModuleFileNameW(mHinst, RealDLL, MPIRPATHLEN);
        PathRemoveFileSpecW(RealDLL);
        GetCPU();
        wcscat_s(RealDLL, MPIRPATHLEN, L"\\mpir_");
        wcscat_s(RealDLL, MPIRPATHLEN, modelstr);
        wcscat_s(RealDLL, MPIRPATHLEN, L".dll");
        mHinstDLL = LoadLibraryW(RealDLL);
        if (!mHinstDLL) {
            return FALSE;
        }
        for (int i = 0; i < 600; ++i) {
            mProcs[i] = (UINT_PTR)GetProcAddress(mHinstDLL, mImportNames[i]);
        }
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        FreeLibrary(mHinstDLL);
    }
    return TRUE;
}
