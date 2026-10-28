// Segment thread accounting and proof checks (hw_proof.cpp). Included by
// hw_proof_wait_test.cpp after its helpers (require, OwnedVdf,
// kFillTestDiscriminant).

static void check_segment_threads_accounting() {
    for (uint8_t segment_threads : {uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(8)}) {
        {
            vdf_state vdf{};
            vdf.segment_threads = segment_threads;
            vdf.n_proof_threads = segment_threads + 5;
            vdf.aux_threads_busy = 1;
            hw_proof_finish_job(&vdf, 0, true, SIZE_MAX, true);
            require(vdf.n_proof_threads == 5,
                    "Proof completion removed the wrong thread count");
            require(vdf.aux_threads_busy == 0, "Proof completion left its slot busy");
        }
        {
            vdf_proof_opts opts{};
            opts.max_aux_threads = 8;
            opts.max_proof_threads = segment_threads;
            opts.segment_threads = segment_threads;
            vdf_state vdf{};
            uint8_t initial[BQFC_FORM_SIZE] = {8};
            const uint64_t span = 3 * HW_VDF_VALUE_INTERVAL;
            init_vdf_state(&vdf, &opts, "-23", initial, span, 0);
            OwnedVdf owned{&vdf};
            uint16_t proof = hw_queue_proof(&vdf, span, HW_VDF_PROOF_NONE,
                    HW_VDF_PROOF_FLAG_IS_REQ);
            vdf.last_val.iters = vdf.proofs[proof].iters;
            hw_proof_process_work(&vdf);
            require(vdf.n_proof_threads == segment_threads,
                    "Proof dispatch added the wrong thread count");
            hw_request_stop_proof(&vdf);
            hw_stop_proof(&vdf);
            require(vdf.n_proof_threads == 0,
                    "Proof cancellation removed the wrong thread count");
        }
    }
}

static void check_segment_threads_validation() {
    uint8_t initial[BQFC_FORM_SIZE] = {8};
    for (uint8_t segment_threads : {uint8_t(1), uint8_t(2), uint8_t(3),
                                    uint8_t(4), uint8_t(5), uint8_t(6),
                                    uint8_t(7), uint8_t(8)}) {
        vdf_proof_opts opts{};
        opts.segment_threads = segment_threads;
        vdf_state vdf{};
        init_vdf_state(&vdf, &opts, "-23", initial, 4000, 0);
        OwnedVdf owned{&vdf};
        require(vdf.segment_threads == segment_threads,
                "Segment thread count was not copied into the state");
    }
    for (uint8_t segment_threads : {uint8_t(0), uint8_t(9)}) {
        vdf_proof_opts opts{};
        opts.segment_threads = segment_threads;
        vdf_state vdf{};
        bool rejected = false;
        try {
            init_vdf_state(&vdf, &opts, "-23", initial, 4000, 0);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        if (!rejected) {
            hw_stop_proof(&vdf);
            clear_vdf_state(&vdf);
        }
        require(rejected, "Invalid segment thread count was accepted");
    }
    {
        vdf_state vdf{};
        init_vdf_state(&vdf, nullptr, "-23", initial, 4000, 0);
        OwnedVdf owned{&vdf};
        require(vdf.segment_threads == HW_VDF_DEFAULT_SEGMENT_THREADS,
                "Default segment thread count was not applied");
    }
}

static void check_segment_threads_proofs() {
    uint8_t initial[BQFC_FORM_SIZE] = {8};
    const uint64_t interval = HW_VDF_VALUE_INTERVAL;
    const uint64_t span = 3 * interval;
    std::vector<uint8_t> reference;
    for (uint8_t segment_threads : {uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(8)}) {
        vdf_proof_opts opts{};
        opts.segment_threads = segment_threads;
        vdf_state vdf{};
        init_vdf_state(&vdf, &opts, kFillTestDiscriminant, initial, span, 0);
        OwnedVdf owned{&vdf};

        // Fill every grid point the segment reads, from a non-trivial
        // discriminant, so each thread range holds digits.
        std::vector<form> chain(span + 1);
        chain[0] = *hw_proof_value_at(&vdf, 0);
        PulmarkReducer reducer;
        for (uint64_t i = 1; i <= span; i++) {
            chain[i] = chain[i - 1];
            nudupl_form(chain[i], chain[i], vdf.D, vdf.L);
            reducer.reduce(chain[i]);
        }
        for (uint64_t pos = 1; pos <= 3; pos++) {
            *hw_proof_value_at(&vdf, pos) = chain[pos * interval];
            hw_proof_add_intermediate(&vdf, nullptr, pos);
        }

        vdf_proof proof{};
        proof.iters = span;
        proof.seg_iters = span;
        proof.flags = HW_VDF_PROOF_FLAG_IS_REQ;
        hw_compute_proof(&vdf, SIZE_MAX, &proof, 255);
        require((proof.flags & HW_VDF_PROOF_FLAG_DONE) != 0,
                "Segment thread proof was not published");
        std::vector<uint8_t> bytes(proof.proof, proof.proof + BQFC_FORM_SIZE);
        if (reference.empty())
            reference = bytes;
        else
            require(bytes == reference,
                    "Proof bytes depend on the segment thread count");
        form x = *hw_proof_value_at(&vdf, 0);
        form y = DeserializeForm(vdf.D, proof.y, BQFC_FORM_SIZE, false);
        form proof_form = DeserializeForm(vdf.D, proof.proof, BQFC_FORM_SIZE, false);
        bool valid = false;
        VerifyWesolowskiProof(vdf.D, x, y, proof_form, span, valid);
        require(valid, "Segment thread proof did not verify");
    }
}

static void check_segment_threads_hw_prover() {
    uint8_t initial[BQFC_FORM_SIZE] = {8};
    for (uint8_t segment_threads : {uint8_t(1), uint8_t(3), uint8_t(8)}) {
        vdf_proof_opts opts{};
        opts.segment_threads = segment_threads;
        vdf_state vdf{};
        init_vdf_state(&vdf, &opts, "-23", initial, 4000, 0);
        OwnedVdf owned{&vdf};
        form x = *hw_proof_value_at(&vdf, 0);
        Segment seg(0, 4000, x, x);
        auto prover = hw_make_prover(seg, vdf.D, &vdf, HW_VDF_MAX_AUX_THREADS);
        require(prover->SegmentThreads() == segment_threads,
                "HwProver ignored --segment-threads");
    }
}
