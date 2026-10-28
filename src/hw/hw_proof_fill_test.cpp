// Fill scheduling and paired fills (hw_proof_fill.cpp). Included by
// hw_proof_wait_test.cpp after its helpers (require, wait_for, OwnedVdf).

// A 1024-bit test discriminant (the first one in hw_test.cpp).
static const char *const kFillTestDiscriminant =
    "-0xac566497f63870a7b661f5482f47336cd1aa85ab43914828b7998f255916729c2ad965bcf7fe231721d96706ea7d823ed4adf663a0263714bb80144aebafcdd2915b6c7ef68c2d19447be83e7f39b4a7442640914053d2e7d6a561aa29b9449c815717af7da97a823798f402d073901a1f2bd8cd879b8b1afe2496649197021f";

static vdf_work *make_queued_fill()
{
    auto *work = new vdf_work;
    init_vdf_value(&work->start_val);
    return work;
}

// A lone fill keeps its own thread: fills pair only with paired kernels on
// and only when the queue holds more fills than the other free slots.
static void check_fill_pairing_rule()
{
    struct Case {
        bool pair_kernels;
        size_t queued;
        int free_slots;
        bool paired;
    };
    const Case cases[] = {
        {true, 2, 1, true},
        {true, 2, 2, false},
        {true, 3, 2, true},
        {true, 1, 1, false},
        {false, 2, 1, false},
    };
    for (const Case &c : cases) {
        vdf_state vdf{};
        vdf.pair_kernels = c.pair_kernels;
        for (size_t i = 0; i < c.queued; i++)
            vdf.wq.push_back(make_queued_fill());
        std::unique_ptr<vdf_work> taken = hw_proof_take_fill(&vdf, c.free_slots);
        require((taken->pair != nullptr) == c.paired, "Fill pairing rule chose the wrong pairing");
        require(vdf.wq.size() == c.queued - (c.paired ? 2 : 1), "Fill pairing took the wrong number of fills");
        hw_proof_free_work(std::move(taken));
        for (vdf_work *w : vdf.wq)
            hw_proof_free_work(std::unique_ptr<vdf_work>(w));
    }
}

#if defined(CHIAVDF_PAIR_KERNELS)
static vdf_work *make_fill(vdf_state &vdf, const form &start, uint64_t start_iters,
                           uint64_t grid_iters, uint32_t n_steps)
{
    auto *work = new vdf_work;
    copy_vdf_value(&work->start_val, &vdf.last_val);
    mpz_set(work->start_val.a, start.a.impl);
    mpz_set(work->start_val.b, start.b.impl);
    work->start_val.iters = start_iters;
    work->start_iters = grid_iters;
    work->n_steps = n_steps;
    return work;
}

static bool value_bit(const std::unique_ptr<std::atomic<uint8_t>[]> &bits, size_t pos)
{
    return bits[pos / 8].load(std::memory_order_acquire) & (1u << (pos % 8));
}

// With one aux slot and two queued fills, dispatch pairs them on that slot.
// Paired fills must publish the grid values of a plain squaring chain, and a
// bad start form must drop only its own fill.
static void check_paired_fills()
{
    vdf_proof_opts opts{};
    opts.max_aux_threads = 1;
    opts.max_proof_threads = 1;
    opts.pair_kernels = true;
    vdf_state vdf{};
    uint8_t initial[BQFC_FORM_SIZE] = {8};
    const uint64_t interval = HW_VDF_VALUE_INTERVAL;
    init_vdf_state(&vdf, &opts, kFillTestDiscriminant, initial, 6 * interval, 0);
    OwnedVdf owned{&vdf};

    std::vector<form> chain(6 * interval + 1);
    chain[0] = *hw_proof_value_at(&vdf, 0);
    PulmarkReducer reducer;
    for (size_t i = 1; i < chain.size(); i++) {
        chain[i] = chain[i - 1];
        nudupl_form(chain[i], chain[i], vdf.D, vdf.L);
        reducer.reduce(chain[i]);
    }

    // Two fills: 3600 -> 4000 and 8000, and 11800 -> 12000.
    vdf.wq.push_back(make_fill(vdf, chain[3600], 3600, interval, 1));
    vdf.wq.push_back(make_fill(vdf, chain[11800], 11800, 3 * interval, 0));
    hw_proof_process_work(&vdf);
    require(vdf.wq.empty(), "Paired dispatch left a fill queued with one slot");
    require(wait_for([&] { return vdf.aux_threads_busy.load() == 0; }, std::chrono::seconds(10)),
            "Paired fills did not finish");
    for (size_t pos : {1, 2, 3}) {
        require(value_bit(vdf.valid_values, pos), "Paired fill did not publish its grid value");
        require(*hw_proof_value_at(&vdf, pos) == chain[pos * interval],
                "Paired fill published a different grid value");
    }

    // A bad start form (4a does not divide b^2 - D) drops only its own fill.
    vdf_work *bad = make_fill(vdf, chain[19000], 19000, 5 * interval, 0);
    mpz_set_ui(bad->start_val.a, 1);
    mpz_set_ui(bad->start_val.b, 2);
    vdf.wq.push_back(bad);
    vdf.wq.push_back(make_fill(vdf, chain[15900], 15900, 4 * interval, 0));
    hw_proof_process_work(&vdf);
    require(wait_for([&] { return vdf.aux_threads_busy.load() == 0; }, std::chrono::seconds(10)),
            "Paired fills with a bad start form did not finish");
    require(value_bit(vdf.valid_values, 4) && *hw_proof_value_at(&vdf, 4) == chain[4 * interval],
            "A bad paired fill stopped the good one");
    require(!value_bit(vdf.valid_values, 5), "A bad paired fill published its grid value");
    require(value_bit(vdf.abandoned_values, 5), "A bad paired fill did not abandon its range");
}
#endif
