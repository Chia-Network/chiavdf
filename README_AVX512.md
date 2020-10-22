# AVX512 synthetic performance

The synthetic benchmark utility avx512_test will run various AVX-512 operations in a loop.

```
cd src
cp Makefile.vdf-client Makefile
make clean
make avx512_test
```

The benchmark is performed for 512 bit and 1024 bit integers. The results are the number of clock cycles for 1000 iterations.

There might be multiple values for a single benchmark if there was high variance in the time measurement.

For example:

benchmark_512_to_avx512, :

```
99%: 11719
0%: 22699
0%: 40319
```

99% of the runs had an average of 11719 cycles for 1000 iterations. The other results should be ignored.

The avx512_test benchmark is run on a HP Notebook 14-dq1045cl laptop with turbo boost disabled. The CPU on this laptop only has one AVX-512 execution port instead of two ports, so the performance might increase if Intel releases a CPU with two execution ports.

The GMP integers need to be converted into AVX-512 format (52 bits per limb) before they can be operated on. This takes 12 cycles for a 512-bit integer:

benchmark_512_to_avx512, :

```
99%: 11719
0%: 22699
0%: 40319
```

For an addition, this is done two times and the inputs can then be added with the AVX-512 code, which takes 29 cycles:

benchmark_512_add_avx512, :

```
99%: 28838
0%: 36706
```

Finally, the result is converted back to GMP format, which takes 19 cycles:

benchmark_512_add_to_gmp, :

```
100%: 19118
```

Using GMP for the addition instead of AVX-512 takes 28 cycles:

benchmark_512_add_gmp, :

```
99%: 28084
0%: 37884
```

GMP's addition routine takes the same amount of time as the AVX-512 code, so the AVX-512 code would be slower if the integers are in GMP format. The AVX-512 addition code might still be useful if the integers are stored in AVX-512 format.

For 1024 bits, conversion to AVX-512 format takes 17 cycles, AVX-512 addition takes 42 cycles, and conversion back to GMP format takes 24 cycles. GMP's addition routine takes 37 cycles.

benchmark_1024_to_avx512, :

```
99%: 16981
0%: 46808
```

benchmark_1024_add_avx512, :

```
99%: 41645
0%: 72434
```

benchmark_1024_add_to_gmp, :

```
99%: 24406
0%: 55040
```

benchmark_1024_add_gmp, :

```
99%: 37362
0%: 66657
```

For multiplication of two 512-bit integers with a 1024-bit result, the AVX-512 code takes 67 cycles and 24 cycles are required to converT the 1024-bit result back to GMP format:

benchmark_512_mul_avx512, :

```
100%: 67262
```

benchmark_512_mul_to_gmp, :

```
99%: 24376
0%: 35036
```

The cycle counts for converting two integers from GMP to AVX-512 format, multiplying them, and converting back to GMP format are 17+17+67+24 = 125 cycles. However, performing these operations in a single loop takes 180 cycles:

benchmark_512_mul_avx512_gmp, :

```
100%: 180744
```

The AVX-512 multiplication with both the inputs and output in GMP format is still faster than the GMP multiplication, which takes 207 cycles:

benchmark_512_mul_gmp, :

```
100%: 207191
```

For 1024 bits, the speedup of the AVX-512 code over the GMP code is higher. The AVX-512 code takes 300 cycles with the inputs and outputs in GMP format, but the GMP code takes 676 cycles:

benchmark_1024_mul_avx512, :

```
100%: 175146
```

benchmark_1024_mul_avx512_gmp, :

```
100%: 300046
```

benchmark_1024_mul_gmp, :

```
100%: 676242
```

benchmark_1024_mul_to_gmp, :

```
99%: 36454
0%: 69371
```

## Performance of the VDF with AVX-512 operations

GMP multiplications are replaced with AVX-512 multiplications if "enable_avx512_ifma" is set to true in "parameters.h". The default is false. All integers are stored in GMP format.

Additions were not replaced because the AVX-512 implementation is slower than the GMP implementation.

The benchmark was performed with this command: "taskset -c 0,1 ./vdf_bench square_asm 1000000"

AVX-512 disabled:

```
Time: 15208 ms; n_slow: 213; speed: 65.7K ips
Time: 14981 ms; n_slow: 213; speed: 66.7K ips
Time: 15160 ms; n_slow: 213; speed: 65.9K ips
Time: 14919 ms; n_slow: 213; speed: 67.0K ips
Time: 15095 ms; n_slow: 213; speed: 66.2K ips
```

AVX-512 enabled:

```
Time: 15966 ms; n_slow: 213; speed: 62.6K ips
Time: 15986 ms; n_slow: 213; speed: 62.5K ips
Time: 15910 ms; n_slow: 213; speed: 62.8K ips
Time: 16001 ms; n_slow: 213; speed: 62.4K ips
Time: 15803 ms; n_slow: 213; speed: 63.2K ips
```

Enabling AVX-512 causes a slight reduction in the overall performance, even though the synthetic benchmark showed an increase in performance. This could be due to instruction cache misses, since the AVX-512 code doesn't have loops and the GMP code does.

## Documentation for newly added code

The new AVX-512 integer implementation is in "asm_avx512_ifma.h" and "avx512_integer.h".

In "asm_avx512_ifma.h", the "to_avx512_integer" and "to_gmp_integer" functions are used to convert between AVX-512 and GMP integer representations. These use a different, more efficient algorithm than the original entry to perform the required bit shifts. Converting to AVX-512 format also requires that the uninitialized limbs of the GMP integer be zeroed out, and converting to GMP format requires calculating the number of nonzero limbs.

The "add" function is used to add two AVX-512 integers. The original entry used two's complement representation but this entry uses a sign-magnitude representation. This avoids performance issues with the "apply_carry" function if the sign of the result changes. However, it also requires calculating which of the input integers is larger in magnitude. Also, either an addition or subtraction might be required depending on the signs of the inputs.

The "multiply" and "apply_carry" functions use similar algorithms to the implementation from the original entry.

"avx512_integer.h" contains a class which will call the assembly implementations. Only the operand sizes that are required are compiled.
