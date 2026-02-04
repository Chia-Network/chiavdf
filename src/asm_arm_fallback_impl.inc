/*
 * ARM C++ fallback implementation. Include this from exactly one .cpp per binary
 * when ARCH_ARM (after vdf.h so all types are available).
 */
#if defined(ARCH_ARM)

#include <atomic>

std::atomic<uint64_t> gcd_unsigned_arm_bad_order_fallbacks{0};
std::atomic<uint64_t> gcd_unsigned_arm_gcd128_fail_fallbacks{0};
std::atomic<uint64_t> gcd_unsigned_arm_exact_division_repairs{0};

namespace {

void load_limbs_to_fixed_integer(uint64* limbs, fixed_integer<uint64, gcd_size>& out) {
    // `fixed_integer<uint64, gcd_size>` stores little-endian 64-bit limbs in `out[0..gcd_size-1]`
    // (mapped to `data[1..]`). The `asm_func_gcd_unsigned_data` limb buffers are already in the
    // same format, so we can copy directly. Avoid GMP conversions in this hot path.
    out.set_negative(false);
    for (int i = 0; i < gcd_size; i++) {
        out[i] = limbs[i];
    }
}

void store_fixed_integer_to_limbs(const fixed_integer<uint64, gcd_size>& in, uint64* limbs) {
    for (int i = 0; i < gcd_size; i++) {
        limbs[i] = in[i];
    }
}

}  // namespace

namespace asm_code {

extern "C" int asm_arm_func_gcd_unsigned(asm_func_gcd_unsigned_data* data) {
    assert(data != nullptr);
    assert(data->a != nullptr && data->b != nullptr);
    assert(data->a_2 != nullptr && data->b_2 != nullptr);
    assert(data->threshold != nullptr);

    // Load inputs in caller order: data->a = original a, data->b = original b.
    fixed_integer<uint64, gcd_size> a, b, threshold;
    load_limbs_to_fixed_integer(data->a, a);
    load_limbs_to_fixed_integer(data->b, b);
    load_limbs_to_fixed_integer(data->threshold, threshold);

    // gcd_unsigned requires ab[0] >= ab[1]. If a < b, swap so (a,b) -> (b,a); we track
    // inputs_swapped so we can store results and UV matrices back in original (a, b) order.
    bool inputs_swapped = (a < b);
    if (inputs_swapped) {
        std::swap(a, b);
    }
    if (b <= threshold) {
        return -1;
    }

    array<fixed_integer<uint64, gcd_size>, 2> ab = {a, b};
    array<fixed_integer<uint64, gcd_size>, 2> uv;
    uv[0] = fixed_integer<uint64, gcd_size>(integer(1));
    uv[1] = fixed_integer<uint64, gcd_size>(integer(0));
    int parity = 1;

    // Emit the asm-style per-iteration UV stream directly (no callback/side-channel).
    gcd_unsigned_uv_stream_out stream;
    stream.out_uv_addr = data->out_uv_addr;
    if (data->out_uv_counter_addr) {
        stream.out_uv_counter = reinterpret_cast<std::atomic<uint64>*>(data->out_uv_counter_addr);
        stream.uv_counter_start = data->uv_counter_start;
    }
    stream.inputs_swapped = inputs_swapped;

    // Publish the iter=-1 entry (uv_entries[0]): only exit_flag is meaningful.
    // Follow the x86 protocol: set counter to `uv_counter_start + (-1)` after writing it.
    if (data->out_uv_addr) {
        data->out_uv_addr[-8 + 5] = 0;
    }
    if (stream.out_uv_counter) {
        stream.out_uv_counter->store(stream.uv_counter_start - 1, std::memory_order_release);
    }

    gcd_unsigned<gcd_size>(ab, uv, parity, threshold, &stream);

    // If the fast path couldn't produce a stream (gcd_128 failed or invariant violated),
    // fail like the x86 asm path does.
    if (!stream.ok) {
        return -1;
    }

    const int iter = stream.iter_count;

    // Store results so that data->a/data->a_2 always get the result for original a (first input)
    // and data->b/data->b_2 for original b (second input). When we swapped, internal ab[0]
    // is result for original b and ab[1] for original a, so we store accordingly.
    if (inputs_swapped) {
        store_fixed_integer_to_limbs(ab[1], data->a_2);   // result for original a
        store_fixed_integer_to_limbs(ab[0], data->b_2);   // result for original b
        store_fixed_integer_to_limbs(ab[1], data->a);
        store_fixed_integer_to_limbs(ab[0], data->b);
    } else {
        store_fixed_integer_to_limbs(ab[0], data->a_2);
        store_fixed_integer_to_limbs(ab[1], data->b_2);
        store_fixed_integer_to_limbs(ab[0], data->a);
        store_fixed_integer_to_limbs(ab[1], data->b);
    }

    data->iter = iter;

    // Match x86 protocol:
    // - The consumer starts at the sentinel entry (iter == -1, stored at uv_entries[0]).
    // - If N>0 iterations were produced, the final real entry (iter==N-1) must have exit_flag=1.
    // - If N==0, the sentinel entry itself must have exit_flag=1 so the consumer terminates
    //   without ever reading an uninitialized "iter 0" entry.
    //
    // The per-iteration publication happens inside `gcd_unsigned()` above; here we only
    // set the terminal exit_flag and then publish the final counter value again so
    // consumers (acquire) observe it.
    if (data->out_uv_addr) {
        if (iter > 0) {
            data->out_uv_addr[(iter - 1) * 8 + 5] = 1;
        } else {
            // iter == 0: out_uv_addr points at entry for iter 0; the sentinel is one entry before.
            data->out_uv_addr[-8 + 5] = 1;
        }
    }
    if (data->out_uv_counter_addr) {
        reinterpret_cast<std::atomic<uint64>*>(data->out_uv_counter_addr)->store(
            // After N iterations (0..N-1), last published counter is start + (N-1).
            // Publish again *after* setting exit_flag so consumers see it.
            data->uv_counter_start + iter - 1, std::memory_order_release);
    }

    return 0;
}

} // namespace asm_code

#endif
