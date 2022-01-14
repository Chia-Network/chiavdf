#!/usr/bin/python3
import math
import sys


def dbg(*args):
    print(*args, file=sys.stderr)


primes = (
    2,
    3,
    5,
    7,
    11,
    13,
    17,
    19,
    23,
    29,
    31,
    37,
    41,
    43,
    47,
    53,
    59,
    61,
    67,
    71,
    73,
    79,
    83,
    89,
    97,
    101,
    103,
    107,
    109,
    113,
    127,
    131,
    137,
    139,
    149,
)


def is_prime(n):
    if n in primes:
        return True
    if n % 2 == 0:
        return False
    s = int(math.sqrt(n))
    for p in primes:
        if p > s:
            return True
        if n % p == 0:
            return False
    raise ValueError("Too big n=%d" % (n,))


def print_pprods_h(prods, factors):
    tab = "    "
    tmpl = """
#ifndef PPRODS_H
#define PPRODS_H

// This file is auto-generated by gen_pprods.py

// The array contains products of consecutive odd prime numbers grouped so that
// each product fits into 64 bits.
static const uint64_t pprods[] = {
%s
};

static const uint32_t pprods_max_prime = %d;
#endif // PPRODS_H
"""
    tmpl = tmpl.strip()
    prods_str = ""
    for i, pr in enumerate(prods):
        factors_str = "*".join(map(str, factors[i]))
        extra_spaces = " " * (16 - (pr.bit_length() + 3) // 4)
        prods_str += tab + "0x%x,  %s// %s\n" % (pr, extra_spaces, factors_str)
    print(tmpl % (prods_str.rstrip(), factors[-1][-1]))


n = int(sys.argv[1])
cnt = 0
pr = 1
arr = []
prods = []
factors = []
for i in range(3, n):
    if is_prime(i):
        # print(i, '', end='')
        cnt += 1
        pr *= i
        if pr.bit_length() > 64:
            pr //= i
            # print('%s: %s' % (hex(pr), arr))
            prods.append(pr)
            factors.append(arr)
            pr = i
            arr = []
        arr.append(i)

print_pprods_h(prods, factors)

dbg()
dbg("n_primes=%d n_prods=%d last_prime=%d" % (cnt, len(prods), factors[-1][-1]))
# dbg(*list('%s,' % (hex(i),) for i in prods))
