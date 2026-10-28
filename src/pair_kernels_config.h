#ifndef PAIR_KERNELS_CONFIG_H
#define PAIR_KERNELS_CONFIG_H

// The paired class-group kernels (pair_kernels.h) need unsigned __int128.
#if defined(__SIZEOF_INT128__)
#define CHIAVDF_PAIR_KERNELS 1
#endif

// Whether the ASIC client uses them by default. They are measured only on
// Apple Silicon so far; elsewhere they are built and tested but off unless
// vdf_proof_opts::pair_kernels turns them on.
#if defined(CHIAVDF_PAIR_KERNELS) && defined(__APPLE__) && defined(__aarch64__)
#define CHIAVDF_PAIR_KERNELS_DEFAULT true
#else
#define CHIAVDF_PAIR_KERNELS_DEFAULT false
#endif

#endif // PAIR_KERNELS_CONFIG_H
