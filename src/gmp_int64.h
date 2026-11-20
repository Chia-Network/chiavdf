/* 64-bit integer helpers for GMP's *_si / *_ui functions.
 *
 * GMP's *_si / *_ui functions take `long` / `unsigned long`. On Windows
 * (LLP64) those are 32 bits, so passing an int64 / uint64 silently truncates
 * values outside the 32-bit range. MPIR redefines these functions to take
 * 64-bit integers on Win64, which is why the MPIR builds never hit this.
 * See also #419, which fixed the same issue in Reducer.h and xgcd_partial.c.
 *
 * Where `long` is 64 bits (or with MPIR) every helper is a direct call.
 * Otherwise values that fit in a `long` still take the fast *_si / *_ui path,
 * and larger ones are wrapped in a read-only mpz backed by a single stack
 * limb (mpz_roinit_n, no allocation) and use the mpz_t variant.
 *
 * Do not cast the value to long / unsigned long in the direct calls: with
 * MPIR on Windows long is 32 bits but the functions take 64-bit integers,
 * so a cast would reintroduce the truncation. Let it convert implicitly to
 * whatever parameter type the library declares.
 */
#ifndef CHIAVDF_GMP_INT64_H
#define CHIAVDF_GMP_INT64_H

#include <gmp.h>
#include <limits.h>
#include <stdint.h>

#if !defined(__MPIR_VERSION) && (LONG_MAX < INT64_MAX)
#define CHIAVDF_GMP_NARROW_LONG 1
#endif

#if defined(CHIAVDF_GMP_NARROW_LONG)

#if GMP_LIMB_BITS != 64 || GMP_NAIL_BITS != 0
#error "gmp_int64.h needs 64-bit limbs without nails"
#endif

/* Read-only mpz equal to v, stored in *limb. Valid while *limb is alive. */
static inline mpz_srcptr chiavdf_mpz_view_u64(mpz_ptr view, mp_limb_t* limb, uint64_t v) {
    *limb = (mp_limb_t)v;
    return mpz_roinit_n(view, limb, 1);
}

static inline mpz_srcptr chiavdf_mpz_view_s64(mpz_ptr view, mp_limb_t* limb, int64_t v) {
    *limb = (mp_limb_t)(v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v);
    return mpz_roinit_n(view, limb, v < 0 ? -1 : 1);
}

#define CHIAVDF_FITS_LONG(v) ((v) >= LONG_MIN && (v) <= LONG_MAX)
#define CHIAVDF_FITS_ULONG(v) ((v) <= ULONG_MAX)
#define CHIAVDF_VIEW_S64(name, v) \
    mpz_t name##_view; mp_limb_t name##_limb; \
    mpz_srcptr name = chiavdf_mpz_view_s64(name##_view, &name##_limb, (v))
#define CHIAVDF_VIEW_U64(name, v) \
    mpz_t name##_view; mp_limb_t name##_limb; \
    mpz_srcptr name = chiavdf_mpz_view_u64(name##_view, &name##_limb, (v))

#endif /* CHIAVDF_GMP_NARROW_LONG */

static inline void chiavdf_mpz_set_si64(mpz_ptr rop, int64_t v) {
#if defined(CHIAVDF_GMP_NARROW_LONG)
    if (!CHIAVDF_FITS_LONG(v)) { CHIAVDF_VIEW_S64(t, v); mpz_set(rop, t); return; }
#endif
    mpz_set_si(rop, v);
}

static inline void chiavdf_mpz_set_ui64(mpz_ptr rop, uint64_t v) {
#if defined(CHIAVDF_GMP_NARROW_LONG)
    if (!CHIAVDF_FITS_ULONG(v)) { CHIAVDF_VIEW_U64(t, v); mpz_set(rop, t); return; }
#endif
    mpz_set_ui(rop, v);
}

static inline void chiavdf_mpz_init_set_si64(mpz_ptr rop, int64_t v) {
    mpz_init(rop);
    chiavdf_mpz_set_si64(rop, v);
}

static inline void chiavdf_mpz_init_set_ui64(mpz_ptr rop, uint64_t v) {
    mpz_init(rop);
    chiavdf_mpz_set_ui64(rop, v);
}

static inline void chiavdf_mpz_add_ui64(mpz_ptr rop, mpz_srcptr a, uint64_t v) {
#if defined(CHIAVDF_GMP_NARROW_LONG)
    if (!CHIAVDF_FITS_ULONG(v)) { CHIAVDF_VIEW_U64(t, v); mpz_add(rop, a, t); return; }
#endif
    mpz_add_ui(rop, a, v);
}

static inline void chiavdf_mpz_sub_ui64(mpz_ptr rop, mpz_srcptr a, uint64_t v) {
#if defined(CHIAVDF_GMP_NARROW_LONG)
    if (!CHIAVDF_FITS_ULONG(v)) { CHIAVDF_VIEW_U64(t, v); mpz_sub(rop, a, t); return; }
#endif
    mpz_sub_ui(rop, a, v);
}

static inline void chiavdf_mpz_mul_si64(mpz_ptr rop, mpz_srcptr a, int64_t v) {
#if defined(CHIAVDF_GMP_NARROW_LONG)
    if (!CHIAVDF_FITS_LONG(v)) { CHIAVDF_VIEW_S64(t, v); mpz_mul(rop, a, t); return; }
#endif
    mpz_mul_si(rop, a, v);
}

static inline void chiavdf_mpz_mul_ui64(mpz_ptr rop, mpz_srcptr a, uint64_t v) {
#if defined(CHIAVDF_GMP_NARROW_LONG)
    if (!CHIAVDF_FITS_ULONG(v)) { CHIAVDF_VIEW_U64(t, v); mpz_mul(rop, a, t); return; }
#endif
    mpz_mul_ui(rop, a, v);
}

static inline void chiavdf_mpz_addmul_ui64(mpz_ptr rop, mpz_srcptr a, uint64_t v) {
#if defined(CHIAVDF_GMP_NARROW_LONG)
    if (!CHIAVDF_FITS_ULONG(v)) { CHIAVDF_VIEW_U64(t, v); mpz_addmul(rop, a, t); return; }
#endif
    mpz_addmul_ui(rop, a, v);
}

static inline void chiavdf_mpz_submul_ui64(mpz_ptr rop, mpz_srcptr a, uint64_t v) {
#if defined(CHIAVDF_GMP_NARROW_LONG)
    if (!CHIAVDF_FITS_ULONG(v)) { CHIAVDF_VIEW_U64(t, v); mpz_submul(rop, a, t); return; }
#endif
    mpz_submul_ui(rop, a, v);
}

static inline int chiavdf_mpz_cmp_si64(mpz_srcptr a, int64_t v) {
#if defined(CHIAVDF_GMP_NARROW_LONG)
    if (!CHIAVDF_FITS_LONG(v)) { CHIAVDF_VIEW_S64(t, v); return mpz_cmp(a, t); }
#endif
    return mpz_cmp_si(a, v);
}

static inline int chiavdf_mpz_cmp_ui64(mpz_srcptr a, uint64_t v) {
#if defined(CHIAVDF_GMP_NARROW_LONG)
    if (!CHIAVDF_FITS_ULONG(v)) { CHIAVDF_VIEW_U64(t, v); return mpz_cmp(a, t); }
#endif
    return mpz_cmp_ui(a, v);
}

static inline int chiavdf_mpz_cmpabs_ui64(mpz_srcptr a, uint64_t v) {
#if defined(CHIAVDF_GMP_NARROW_LONG)
    if (!CHIAVDF_FITS_ULONG(v)) { CHIAVDF_VIEW_U64(t, v); return mpz_cmpabs(a, t); }
#endif
    return mpz_cmpabs_ui(a, v);
}

static inline void chiavdf_mpz_powm_ui64(mpz_ptr rop, mpz_srcptr base, uint64_t exp, mpz_srcptr mod) {
#if defined(CHIAVDF_GMP_NARROW_LONG)
    if (!CHIAVDF_FITS_ULONG(exp)) { CHIAVDF_VIEW_U64(t, exp); mpz_powm(rop, base, t, mod); return; }
#endif
    mpz_powm_ui(rop, base, exp, mod);
}

#endif /* CHIAVDF_GMP_INT64_H */
