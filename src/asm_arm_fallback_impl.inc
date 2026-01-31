/*
 * ARM C++ fallback implementation. Include this from exactly one .cpp per binary
 * when ARCH_ARM (after vdf.h so all types are available).
 */
#if defined(ARCH_ARM)

void (*gcd_unsigned_arm_uv_callback)(int index, const array<array<uint64, 2>, 2>& uv, int parity) = nullptr;

namespace {

thread_local asm_code::asm_func_gcd_unsigned_data* g_arm_gcd_data = nullptr;
thread_local int g_arm_iter_count = 0;

void arm_uv_callback(int index, const array<array<uint64, 2>, 2>& uv, int parity) {
    g_arm_iter_count = index + 1;
    asm_code::asm_func_gcd_unsigned_data* data = g_arm_gcd_data;
    if (!data || !data->out_uv_addr) return;

    uint64* entry = data->out_uv_addr + index * 8;
    entry[0] = uv[0][0];
    entry[1] = uv[1][0];
    entry[2] = uv[0][1];
    entry[3] = uv[1][1];
    entry[4] = static_cast<uint64>(parity);
    entry[5] = 0;
    entry[6] = 0;
    entry[7] = 0;
}

void load_limbs_to_fixed_integer(uint64* limbs, fixed_integer<uint64, gcd_size>& out) {
    integer tmp;
    mpz_import(tmp.impl, gcd_size, -1, sizeof(uint64), -1, 0, limbs);
    out = fixed_integer<uint64, gcd_size>(tmp);
}

void store_fixed_integer_to_limbs(const fixed_integer<uint64, gcd_size>& in, uint64* limbs) {
    for (int i = 0; i < gcd_size; i++) {
        limbs[i] = in[i];
    }
}

}  // namespace

extern "C" int asm_arm_func_gcd_unsigned(asm_code::asm_func_gcd_unsigned_data* data) {
    assert(data != nullptr);
    assert(data->a != nullptr && data->b != nullptr);
    assert(data->a_2 != nullptr && data->b_2 != nullptr);
    assert(data->threshold != nullptr);

    fixed_integer<uint64, gcd_size> a, b, threshold;
    load_limbs_to_fixed_integer(data->a, a);
    load_limbs_to_fixed_integer(data->b, b);
    load_limbs_to_fixed_integer(data->threshold, threshold);

    if (a < b) {
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

    g_arm_gcd_data = data;
    gcd_unsigned_arm_uv_callback = arm_uv_callback;

    gcd_unsigned<gcd_size>(ab, uv, parity, threshold);

    gcd_unsigned_arm_uv_callback = nullptr;
    int iter = g_arm_iter_count;
    g_arm_gcd_data = nullptr;
    g_arm_iter_count = 0;

    store_fixed_integer_to_limbs(ab[0], data->a_2);
    store_fixed_integer_to_limbs(ab[1], data->b_2);
    store_fixed_integer_to_limbs(ab[0], data->a);
    store_fixed_integer_to_limbs(ab[1], data->b);

    data->iter = iter;

    if (data->out_uv_counter_addr) {
        *data->out_uv_counter_addr = data->uv_counter_start + iter;
    }
    if (data->out_uv_addr && iter > 0) {
        data->out_uv_addr[(iter - 1) * 8 + 5] = 1;
    }

    return 0;
}

#endif
