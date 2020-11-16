# Chia VDF

![Build](https://github.com/Chia-Network/chiavdf/workflows/Build/badge.svg)
![PyPI](https://img.shields.io/pypi/v/chiavdf?logo=pypi)
![PyPI - Format](https://img.shields.io/pypi/format/chiavdf?logo=pypi)
![GitHub](https://img.shields.io/github/license/Chia-Network/chiavdf?logo=Github)

[![Total alerts](https://img.shields.io/lgtm/alerts/g/Chia-Network/chiavdf.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/Chia-Network/chiavdf/alerts/)
[![Language grade: Python](https://img.shields.io/lgtm/grade/python/g/Chia-Network/chiavdf.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/Chia-Network/chiavdf/context:python)
[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/Chia-Network/chiavdf.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/Chia-Network/chiavdf/context:cpp)

## Building a wheel

Compiling chiavdf requires cmake, boost and GMP.

```bash
python3 -m venv venv
source venv/bin/activate

pip install wheel setuptools_scm pybind11
pip wheel .
```

The primary build process for this repository is to use GitHub Actions to
build binary wheels for MacOS, Linux (x64 and aarch64), and Windows and
publish them with a source wheel on PyPi. See `.github/workflows/build.yml`.
CMake uses
[FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html)
to download [pybind11](https://github.com/pybind/pybind11).
Building is then managed by
[cibuildwheel](https://github.com/joerick/cibuildwheel). Further installation
is then available via `pip install chiavdf` e.g.

## Building Timelord and related binaries

In addition to building the required binary and source wheels for Windows,
MacOS and Linux, chiavdf can be used to compile vdf_client and vdf_bench.
vdf_client is the core VDF process that completes the Proof of Time submitted
to it by the Timelord. The repo also includes a benchmarking tool to get a
sense of the iterations per second of a given CPU called vdf_bench. Try
`./vdf_bench square_asm 250000` for an ips estimate.

To build vdf_client set the environment variable BUILD_VDF_CLIENT to "Y".
`export BUILD_VDF_CLIENT=Y`.

Similarly, to build vdf_bench set the environment variable BUILD_VDF_BENCH to
"Y". `export BUILD_VDF_BENCH=Y`.

This is currently automated via pip in the
[install-timelord.sh](https://github.com/Chia-Network/chia-blockchain/blob/master/install-timelord.sh)
script in the
[chia-blockchain repository](https://github.com/Chia-Network/chia-blockchain)
which depends on this repository.

If you're running a timelord, the following tests are available, depending of which type of timelord you are running:

`./1weso_test`, in case you're running in sanitizer_mode.

`./2weso_test`, in case you're running a timelord that extends the chain and you're running the slow algorithm.

`./prover_test`, in case you're running a timelord that extends the chain and you're running the fast algorithm.

Those tests will simulate the vdf_client and verify for correctness the produced proofs.

## Contributing and workflow

Contributions are welcome and more details are available in chia-blockchain's
[CONTRIBUTING.md](https://github.com/Chia-Network/chia-blockchain/blob/master/CONTRIBUTING.md).

The master branch is the currently released latest version on PyPI. Note that
at times chiavdf will be ahead of the release version that chia-blockchain
requires in it's master/release version in preparation for a new chia-blockchain
release. Please branch or fork master and then create a pull request to the
master branch. Linear merging is enforced on master and merging requires a
completed review. PRs will kick off a ci build and analysis of chiavdf at
[lgtm.com](https://lgtm.com/projects/g/Chia-Network/chiavdf/?mode=list). Please
make sure your build is passing and that it does not increase alerts at lgtm.

## Background from prior VDF competitions

Copyright 2018 [Ilya Gorodetskov](http://www.sundersoft.com/)
generic@sundersoft.com

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0)

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Our VDF construction is described in classgroup.pdf. The implementation details about squaring and proving phrases are described below.

## Main VDF Loop

The main VDF loop produces repeated squarings of the generator form (i.e. calculates y(n) = g^(2^n)) as fast as possible, until the program is interrupted. Sundersoft's entry from [Chia's 2nd VDF contest](https://github.com/Chia-Network/vdfcontest2results) is used, together with the fast reducer used in Pulmark's entry. This approach is described below:

The NUDUPL algorithm is used. The equations are based on cryptoslava's equations from the 1st contest. They were modified slightly to increase the level of parallelism.

The GCD is a custom implementation with scalar integers. There are two base cases: one uses a lookup table with continued fractions and the other uses the euclidean algorithm with a division table. The division table algorithm is slightly faster even though it has about 2x as many iterations.

After the base case, there is a 128 bit GCD that generates 64 bit cofactor matricies with Lehmer's algorithm. This is required to make the long integer multiplications efficient (Flint's implementation doesn't do this).

The GCD also implements Flint's partial xgcd function, but the output is slightly different. This implementation will always return an A value which is > the threshold and a B value which is <= the threshold. For a normal GCD, the threshold is 0, B is 0, and A is the GCD. Also the interfaces are slightly different.

Scalar integers are used for the GCD. I don't expect any speedup for the SIMD integers that were used in the last implementation since the GCD only uses 64x1024 multiplications, which are too small and have too high of a carry overhead for the SIMD version to be faster. In either case, most of the time seems to be spent in the base case so it shouldn't matter too much.

If SIMD integers are used with AVX-512, doubles have to be used because the multiplier sizes for doubles are significantly larger than for integers. There is an AVX-512 extension to support larger integer multiplications but no processor implements it yet. It should be possible to do a 50 bit multiply-add into a 100 bit accumulator with 4 fused multiply-adds if the accumulators have a special nonzero initial value and the inputs are scaled before the multiplication. This would make AVX-512 about 2.5x faster than scalar code for 1024x1024 integer multiplications (assuming the scalar code is unrolled and uses ADOX/ADCX/MULX properly, and the CPU can execute this at 1 cycle per iteration which it probably can't).

The GCD is parallelized by calculating the cofactors in a separate slave thread. The master thread will calculate the cofactor matricies and send them to the slave thread. Other calculations are also parallelized.

The VDF implementation from the first contest is still used as a fallback and is called about once every 5000 iterations. The GCD will encounter large quotients about this often and these are not implemented. This has a negligible effect on performance. Also, the NUDUPL case where A<=L is not implemented; it will fall back to the old implementation in this case (this never happens outside of the first 20 or so iterations).

There is also corruption detection by calculating C with a non-exact division and making sure the remainder is 0. This detected all injected random corruptions that I tested. No corruptions caused by bugs were observed during testing. This cannot correct for the sign of B being wrong.

### GCD continued fraction lookup table

The is implemented in gcd_base_continued_fractions.h and asm_gcd_base_continued_fractions.h. The division table implementation is the same as the previous entry and was discussed there. Currently the division table is only used if AVX2 is enabled but it could be ported to SSE or scalar code easily. Both implementations have about the same performance.

The initial quotient sequence of gcd(a,b) is the same as the initial quotient sequence of gcd(a*2^n/b, 2^n) for any n. This is because the GCD quotients are the same as the continued fraction quotients of a/b, and the initial continued fraction quotients only depend on the initial bits of a/b. This makes it feasible to have a lookup table since it now only has one input.

a*2^n/b is calculated by doing a double precision division of a/b, and then truncating the lower bits. Some of the exponent bits are used in the table in addition to the fraction bits; this makes each slot of the table vary in size depending on what the exponent is. If the result is outside the table bounds, then the division result is floored to fall back to the euclidean algorithm (this is very rare).

The table is calculated by iterating all of the possible continued fractions that have a certain initial quotient sequence. Iteration ends when all of these fractions are either outside the table or they don't fully contain at least one slot of the table. Each slot that is fully contained by such a fraction is updated so that its quotient sequence equals the fraction's initial quotient sequence. Once this is complete, the cofactor matricies are calculated from the quotient sequences. Each cofactor matrix is 4 doubles.

The resulting code seems to have too many instructions so it doesn't perform very well. There might be some way to optimize it. It was written for SSE so that it would run on both processors.

This might work better on an FPGA possibly with low latency DRAM or SRAM (compared to the euclidean algorithm with a division table). There is no limit to the size of the table but doubling the latency would require the number of bits in the table to also be doubled to have the same performance.

### Other GCD code

The gcd_128 function calculates a 128 bit GCD using Lehmer's algorithm. It is pretty straightforward and uses only unsigned arithmetic. Each cofactor matrix can only have two possible signs: [+ -; - +] or [- +; + -]. The gcd_unsigned function uses unsigned arithmetic and a jump table to apply the 64-bit cofactor matricies to the A and B values. It uses ADOX/ADCX/MULX if they are available and falls back to ADC/MUL otherwise. It will track the last known size of A to speed up the bit shifts required to get the top 128 bits of A.

No attempt was made to try to do the A and B long integer multiplications on a separate thread; I wouldn't expect any performance improvement from this.

### Threads

There is a master thread and a slave thread. The slave thread only exists for each batch of 5000 or so squarings and is then destroyed and recreated for the next batch (this has no measurable overhead). If the original VDF is used as a fallback, the batch ends and the slave thread is destroyed.

Each thread has a 64-bit counter that only it can write to. Also, during a squaring iteration, it will not overwrite any value that it has previously written and transmitted to the other thread. Each squaring is split up into phases. Each thread will update its counter at the start of the phase (the counter can only be increased, not decreased). It can then wait on the other thread's counter to reach a certain value as part of a spin loop. If the spin loop takes too long, an error condition is raised and the batch ends; this should prevent any deadlocks from happening.

No CPU fences or atomics are required since each value can only be written to by one thread and since x86 enforces acquire/release ordering on all memory operations. Compiler memory fences are still required to prevent the compiler from caching or reordering memory operations.

The GCD master thread will increment the counter when a new cofactor matrix has been outputted. The slave thread will spin on this counter and then apply the cofactor matrix to the U or V vector to get a new U or V vector.

It was attempted to use modular arithmetic to calculate k directly but this slowed down the program due to GMP's modulo or integer multiply operations not having enough performance. This also makes the integer multiplications bigger.

The speedup isn't very high since most of the time is spent in the GCD base case and these can't be parallelized.

## Generating proofs

The nested wesolowski proofs (n-wesolowski) are used to check the correctness of a VDF result. (Simple) Wesolowski proofs are described in [A Survey of Two Verifiable Delay Functions](https://theory.stanford.edu/~dabo/papers/VDFsurvey.pdf). In order to prove h = g^(2^T), a n-wesolowski proof uses n intermediate simple wesolowski proofs. Given h, g, T, t1, t2, ..., tn, h1, h2, ..., hn, a correct n-wesolowski proof will verify the following:

```cpp
h1 = g^(2^t1)
h2 = h1^(2^t2)
h3 = h2^(2^t3)
...
hn = h(n-1)^(2^tn)
```

Additionally, we must have:

```cpp
t1 + t2 + ... + tn = T
hn = h
```

The algorithm will generate at most 64-wesolowski proofs. Some intermediates wesolowski proofs are stored in parallel with the main VDF loop. The goal is to have a n-wesolowski proof almost ready as soon as the main VDF loop finishes computing h = g^(2^T), for a T that we're interested in. We'll call a segment a tuple (y, x, T) for which we're interested in a simple wesolowski proof that y = x^(2^T). We'll call a segment finished when we've finished computing its proof.

### Segmenets stored

We'll store finished segments of length 2^x for x being multiples of 2 greater than or equal to 16. The current implementation limits the maximum segment size to 2^30, but this can be increased if needed. Let P = 16+2&ast;l. After each 2^P steps calculated by the main VDF loop, we'll store a segment proving that we've correctly done the 2^P steps. Formally, let x be the form after k&ast;2^P steps, y be the form after (k+1)&ast;2^P steps, for each k >= 0, for each P = 16+2&ast;l. Then, we'll store a segment (y, x, 2^P), together with a simple wesolowski proof.

### Segment threads

In order to finish a segment of length T=2^P, the number of iterations to run for is T/k + l&ast;2^(k+1) and the intermediate storage required is T/(k&ast;l), for some parameters k and l, as described in the paper. The squarings used to finish a segment are about 2 times as slow as the ones used by the main VDF loop. Even so, finishing a segment is much faster than producing its y value by the main VDF loop. This allows, by the time the main VDF loop finishes 2^16 more steps, to perform work on finishing multiple segments.

The parameters used in finishing segments, for T=2^16, are k=10 and l=1. Above that, parameters are k=12 and l=2^(P-18). Note that, for P >= 18, the intermediate storage needed for a segment is constant (i.e. 2^18/12 forms stored in memory).

Prover class is responsible to finish a segment. It implements pause/resume functionality, so its work can be paused, and later resumed from the point it stopped. For each unfinished segment generated by the main VDF loop, a Prover instance is created, which will eventually finish the segment.

Segment threads are responsible for deciding which Prover instance is currently running. In the current implementation, there are 3 segment threads (however the number is configurable), so at most 3 Prover instances will run at once, at different threads (other Provers will be paused). The segment threads will always pick the segments with the shortest length to run. In case of a tie, the segments received the earliest will have priority. Every time a new segment arrives, or a segment gets finished, some pausing/resuming of Provers is done, if needed. Pausing is done to have at most 3 Provers running at any time, whilst resuming is done if less than 3 Provers are working, but some Provers are paused.

All the segments of lengths 2^16, 2^18 and 2^20 will be finished relatively soon after the main VDF worker produced them, while the segments of length 2^22 and upwards will lag behind the main VDF worker a little. Eventually, all the higher size segments will be finished, the work on them being done repeatedly via pausing (when a smaller size segment arrives) and resuming (when all smaller size segments are finished).

Currently, 4 more segment threads are added after the main VDF loop finishes 500 million iterations (after about 1 hour of running). This is done to be completely sure even the very big sized segments will be finished. This optimisation is only allowed on machines supporting at least 16 concurrent threads.

### Generating n-wesolowski proof

Let T an iteration we are interested in. Firstly, the main VDF Loop will need to calculate at least T iterations. Then, in order to get fast a n-wesolowski proof, we'll concatenate finished segments. We want the proof to be as short as possible, so we'll always pick finished segments of the maximum length possible. If such segments aren't finished, we'll choose lower length segments. A segment of length 2^(16 + 2&ast;p) can always be replaced with 4 segments of length 2^(16 + 2&ast;p - 2). The proof will be created shortly after the main VDF loop produced the result, as the 2^16 length segments will always be up to date with the main VDF loop (and, at worst case, we can always concatenate 2^16 length segments, if bigger sizes are not finished yet). It's possible after the concatenation that we'll still need to prove up to 2^16 iterations (no segment is able to cover anything less than 2^16). This last work is done in parallel with the main VDF loop, as an optimisation.

The program limits the proof size to 64-wesolowski. If number of iterations is very large, it's possible the concatenation won't fit into this. In this case, the program will attempt again to prove every minute, until there are enough large segments to fit the 64-wesolowski limit. However, almost in all cases, the concatenation will fit the 64-wesolowski limit in the first try.

Since the maximum segment size is 2^30 and we can use at most 64 segments in a concatenation, the program will prove at most 2^36 iterations. This can be increased if needed.

### Intermediates storage

In order to finish segments, some intermediate values need to be stored for each segment. For each different possible segment length, we use a sliding window of length 20 to store those. Hence, for each segment length, we'll store only the intermediates values needed for the last 20 segments produced by the main VDF loop. Since finishing segments is faster than producing them by the main VDF loop, we assume the segment threads won't be behind by more than 20 segments from the main VDF loop, for each segment length. Thanks to the sliding window technique, the memory used will always be constant.

Generally, the main VDF loop performs all the storing, after computing a form we're interested in. However, since storing is very frequent and expensive (GMP operations), this will slow down the main VDF loop.

For the machines having at least 16 concurrent threads, an optimization is provided: the main VDF loop does only repeated squaring, without storing any form. After each 2^15 steps are performed, a new thread starts redoing the work for 2^15 more steps, this time storing the intermediate values as well. All the intermediates threads and the main VDF loop will work in parallel. The only purpose of the main VDF loop becomes now to produce the starting values for the intermediate threads, as fast as possible. The squarings used in the intermediates threads will be 2 times slower than the ones used in the main VDF loop. It's expected the intermediates will only lag behind the main VDF loop by 2^15 iterations, at any point: after 2^16 iterations are done by the main VDF loop, the first thread doing the first 2^15 intermediate values is already finished. Also, at that point, half of the work of the second thread doing the last 2^15 intermediates values should be already done.
