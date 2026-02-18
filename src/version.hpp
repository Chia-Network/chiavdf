#ifndef CHIAVDF_VERSION_HPP
#define CHIAVDF_VERSION_HPP

#include <cstdio>

#ifndef CHIAVDF_VERSION
#define CHIAVDF_VERSION "dev"
#endif

static inline void PrintCliVersion(const char *prog)
{
    if (!prog || !prog[0]) {
        prog = "chiavdf";
    }
    std::fprintf(stdout, "%s %s\n", prog, CHIAVDF_VERSION);
}

#endif
