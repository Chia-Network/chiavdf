#include "hw_proof.hpp"
#include "chia_driver.hpp"
#include "pll_freqs.hpp"
#include <chrono>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

int hw_proof_wait_value(struct vdf_state *vdf, size_t pos);
form *hw_proof_value_at(struct vdf_state *vdf, size_t pos);
void hw_proof_process_work(struct vdf_state *vdf);

static bool wait_for(const std::function<bool()> &ready, std::chrono::milliseconds limit)
{
    auto deadline = std::chrono::steady_clock::now() + limit;
    while (!ready()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

// A stopped proof returns in about 3ms. Under 3x CPU oversubscription the same
// wait stayed within a few tens of milliseconds, with no failures. An uncancelled
// proof of this fixture runs for several seconds and then verifies, so five
// seconds still fails closed.
constexpr std::chrono::milliseconds kAbandonedProofDeadline{5000};

constexpr size_t kMaxSquaringOrbit = 64;

static bool wait_for_proof_failed(vdf_state &vdf, uint16_t proof, std::chrono::milliseconds limit,
        uint8_t required_flags = 0)
{
    return wait_for([&] {
        if (vdf.aux_threads_busy.load() != 0 || vdf.n_proof_threads.load() != 0)
            return false;
        std::lock_guard<std::mutex> lock(vdf.proofs_resize_mtx);
        uint8_t flags = vdf.proofs[proof].flags;
        return (flags & HW_VDF_PROOF_FLAG_FAILED) != 0
            && (flags & HW_VDF_PROOF_FLAG_DONE) == 0
            && (flags & required_flags) == required_flags;
    }, limit);
}

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

static void check_pll_bounds() {
    ChiaDriver driver;
    require(!driver.SetPLLFrequency(0.0, VALID_PLL_FREQS),
            "SetPLLFrequency accepted an index past the PLL table");
    require(!driver.SetPLLFrequency(0.0, UINT32_MAX),
            "SetPLLFrequency accepted a wrapped PLL index");

    driver.freq_idx = 0;
    adjust_hw_freq(&driver, 0, -1);
    require(driver.freq_idx == 0, "Frequency moved below the PLL table");

    driver.freq_idx = VALID_PLL_FREQS - 1;
    adjust_hw_freq(&driver, 0, 1);
    require(driver.freq_idx == VALID_PLL_FREQS - 1,
            "Frequency moved past the PLL table");
}

static void check_delivery(const vdf_proof_delivery &delivery) {
    const auto tag = static_cast<uint8_t>(delivery.final.iters % 251);
    require(delivery.checkpoints.size() == 1, "Checkpoint snapshot was incomplete");
    require(delivery.final.y[0] == tag && delivery.final.proof[0] == tag,
            "Final payload was not published together");
    const auto &checkpoint = delivery.checkpoints[0];
    require(checkpoint.B[0] == tag && checkpoint.proof[0] == tag,
            "Checkpoint payload was not published together");
}

struct OwnedVdf {
    vdf_state *vdf;
    ~OwnedVdf() {
        hw_stop_proof(vdf);
        clear_vdf_state(vdf);
    }
};

static uint64_t read_be64(const uint8_t *bytes) {
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) value = (value << 8) | bytes[i];
    return value;
}

static std::vector<uint8_t> decode_hex_payload(const std::vector<uint8_t> &frame) {
    require(frame.size() >= 4 && ((frame.size() - 4) % 2) == 0, "Frame length is not hex");
    uint8_t expect[4];
    Int32ToBytes(expect, static_cast<uint32_t>(frame.size() - 4));
    require(std::memcmp(frame.data(), expect, 4) == 0, "Frame length prefix does not match the payload");
    std::vector<uint8_t> binary((frame.size() - 4) / 2);
    for (size_t i = 0; i < binary.size(); i++) {
        auto nibble = [](uint8_t c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        int hi = nibble(frame[4 + i * 2]);
        int lo = nibble(frame[5 + i * 2]);
        require(hi >= 0 && lo >= 0, "Frame is not hexadecimal");
        binary[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return binary;
}

static void check_frame(const vdf_proof_delivery &delivery) {
    require(delivery.checkpoints.size() <= HW_VDF_MAX_CHKP_SEGMENTS,
            "Checkpoint count exceeds the timelord limit");
    std::vector<uint8_t> frame = hw_proof_timelord_frame(delivery);
    std::vector<uint8_t> binary = decode_hex_payload(frame);
    const size_t header = 8 + 8 + 1 + BQFC_FORM_SIZE * 2;
    require(binary.size() >= header, "Frame header was truncated");
    const size_t count = binary[16 + BQFC_FORM_SIZE];
    require(count == delivery.checkpoints.size(), "Frame count does not match the checkpoint list");
    require(binary.size() == header + count * sizeof(hw_timelord_segment),
            "Frame payload does not cover the checkpoint list");
    const uint8_t *cursor = binary.data() + header;
    for (size_t i = 0; i < count; i++) {
        hw_timelord_segment segment{};
        std::memcpy(&segment, cursor, sizeof(segment));
        cursor += sizeof(segment);
        require(read_be64(segment.iters) == delivery.checkpoints[i].seg_iters,
                "Frame segment length does not round-trip");
        require(std::memcmp(segment.B, delivery.checkpoints[i].B, sizeof(segment.B)) == 0
                && std::memcmp(segment.proof, delivery.checkpoints[i].proof, sizeof(segment.proof)) == 0,
                "Frame segment bytes do not round-trip");
    }
}

static void mark_chain_done(vdf_state &vdf, uint16_t idx) {
    while (idx != HW_VDF_PROOF_NONE) {
        vdf.proofs[idx].flags |= HW_VDF_PROOF_FLAG_DONE;
        idx = vdf.proofs[idx].prev;
    }
}

// Repeatedly squaring a reduced form stays in a finite set, so the orbit must
// return to the starting form. The form reached after `steps` squarings is the
// orbit entry at `steps % period`. An orbit that does not return to the start
// inside kMaxSquaringOrbit squarings is rejected instead of storing one form
// per step. The discovered period is reported through `period`.
static form square_steps(form value, const integer &D, const integer &L, uint64_t steps,
        uint64_t *period)
{
    PulmarkReducer reducer;
    std::vector<form> orbit;
    orbit.push_back(value);
    for (uint64_t done = 1; done <= kMaxSquaringOrbit; done++) {
        nudupl_form(value, value, D, L);
        reducer.reduce(value);
        if (!value.check_valid(D))
            throw std::runtime_error("Squaring left the discriminant");
        if (value == orbit[0]) {
            *period = done;
            return orbit[steps % done];
        }
        orbit.push_back(value);
    }
    throw std::runtime_error("Squaring orbit did not return to the start");
}

static void initialize_wait_state(vdf_state& vdf) {
    vdf.valid_values = std::make_unique<std::atomic<uint8_t>[]>(2);
    for (size_t i = 0; i < 2; ++i) vdf.valid_values[i].store(0);
    vdf.stopping = false;
    vdf.aux_threads_busy = 0;
    vdf.done_values = 1;
    vdf.done_iters = 0;
    vdf.elapsed_us = 0;
    vdf.start_time = vdf_get_cur_time();
}

int main() {
    try {
        VdfBaseInit();
        check_pll_bounds();
        {
            vdf_state vdf{};
            initialize_wait_state(vdf);
            vdf.valid_values[0].store(1 << 3, std::memory_order_release);
            std::unique_lock<std::mutex> lock(vdf.valid_values_mtx);
            auto worker = std::async(std::launch::async, [&] { return hw_proof_wait_value(&vdf, 3); });
            auto status = worker.wait_for(std::chrono::seconds(1));
            lock.unlock();
            require(worker.get() == 0, "Published value should succeed");
            require(status == std::future_status::ready, "Published fast path acquired the wait mutex");
        }
        {
            vdf_state vdf{};
            initialize_wait_state(vdf);
            int payload = 0;
            auto worker = std::async(std::launch::async, [&] {
                int result = hw_proof_wait_value(&vdf, 9);
                return std::make_pair(result, payload);
            });
            auto before = worker.wait_for(std::chrono::milliseconds(20));
            {
                std::lock_guard<std::mutex> lock(vdf.valid_values_mtx);
                payload = 42;
                vdf.valid_values[1].fetch_or(1 << 1, std::memory_order_release);
            }
            vdf.valid_values_cv.notify_all();
            auto after = worker.wait_for(std::chrono::seconds(1));
            if (after != std::future_status::ready) hw_stop_proof(&vdf);
            auto result = worker.get();
            require(before == std::future_status::timeout, "Unpublished value did not wait");
            require(after == std::future_status::ready, "Publication did not wake the consumer");
            require(result.first == 0 && result.second == 42, "Publication did not expose producer writes");
        }
        {
            vdf_state vdf{};
            initialize_wait_state(vdf);
            auto worker = std::async(std::launch::async, [&] { return hw_proof_wait_value(&vdf, 7); });
            auto before = worker.wait_for(std::chrono::milliseconds(20));
            hw_stop_proof(&vdf);
            auto after = worker.wait_for(std::chrono::seconds(1));
            require(after == std::future_status::ready, "Stop did not wake an unpublished-value waiter");
            require(worker.get() == -1, "Stopped waiter did not report cancellation");
            require(before == std::future_status::timeout, "Unpublished value did not wait before stop");
        }
        {
            vdf_state vdf{};
            initialize_wait_state(vdf);
            vdf.n_proof_threads = 2;
            vdf.aux_threads_busy = 1;
            std::promise<void> allow_exit;
            auto permitted = allow_exit.get_future();
            vdf.aux_threads[0] = std::thread([&] {
                permitted.wait();
                vdf.n_proof_threads = 0;
                vdf.aux_threads_busy = 0;
            });
            auto drain = std::async(std::launch::async, [&] { hw_proof_wait_values(&vdf, false); });
            auto before = drain.wait_for(std::chrono::milliseconds(20));
            allow_exit.set_value();
            drain.get();
            require(before == std::future_status::timeout, "Drain ignored a still-running owned worker");
            require(vdf.n_proof_threads == 0, "Drain returned before worker accounting finished");
            require(vdf.aux_threads[0].joinable(), "Drain joined a worker it does not own");
            hw_stop_proof(&vdf);
            require(!vdf.aux_threads[0].joinable(), "Stop left an owned worker");
        }
        {
            vdf_state vdf{};
            std::vector<vdf_proof_delivery> snapshots;
            auto publisher = std::async(std::launch::async, [&] {
                for (uint64_t i = 1; i <= 1000; ++i) {
                    std::lock_guard<std::mutex> lock(vdf.proofs_resize_mtx);
                    vdf_proof checkpoint{}, final{};
                    auto tag = static_cast<uint8_t>(i % 251);
                    checkpoint.iters = i;
                    checkpoint.prev = HW_VDF_PROOF_NONE;
                    checkpoint.flags = HW_VDF_PROOF_FLAG_DONE;
                    checkpoint.B[0] = checkpoint.proof[0] = tag;
                    final.iters = i;
                    final.prev = static_cast<uint16_t>(vdf.proofs.size());
                    final.flags = HW_VDF_PROOF_FLAG_DONE | HW_VDF_PROOF_FLAG_IS_REQ;
                    final.y[0] = final.proof[0] = tag;
                    vdf.proofs.push_back(checkpoint);
                    vdf.proofs.push_back(final);
                    vdf.done_proofs.push_back(static_cast<uint16_t>(vdf.proofs.size() - 1));
                }
            });
            while (snapshots.size() < 1000) {
                vdf_proof_delivery delivery;
                int count = hw_retrieve_proof(&vdf, &delivery);
                if (count < 0) { std::this_thread::yield(); continue; }
                require(count == 1, "Wrong checkpoint count");
                check_delivery(delivery);
                snapshots.push_back(std::move(delivery));
            }
            publisher.get();
            vdf.proofs.clear();
            vdf.proofs.shrink_to_fit();
            for (const auto &snapshot : snapshots) check_delivery(snapshot);
        }
        {
            vdf_state vdf{};
            vdf.retained_generation = 2;
            integer d(-23);
            form seed = form::generator(d), result;
            vdf.retained_values.push_back({1, 999, seed});
            vdf.retained_values.push_back({2, 4001, seed});
            vdf.retained_values.push_back({2, 4500, seed});
            vdf.retained_values.push_back({2, 5001, seed});
            uint64_t iters = 4000;
            require(hw_proof_copy_retained_value(&vdf, 5000, 4000, &result, &iters),
                    "Retained endpoint was not found");
            require(iters == 4500 && (mpz_cmp(result.a.impl, seed.a.impl) == 0 && mpz_cmp(result.b.impl, seed.b.impl) == 0 && mpz_cmp(result.c.impl, seed.c.impl) == 0), "Endpoint exceeded target or was not the latest usable sample");
            hw_proof_reset_retained_values(&vdf);
            require((mpz_cmp(result.a.impl, seed.a.impl) == 0 && mpz_cmp(result.b.impl, seed.b.impl) == 0 && mpz_cmp(result.c.impl, seed.c.impl) == 0), "Endpoint copy did not own its GMP limbs");
            require(!hw_proof_copy_retained_value(&vdf, 5000, 4000, &result, &iters),
                    "Expired cache did not fall back to the grid");
            vdf.retained_values.push_back({1, 4500, seed});
            require(!hw_proof_copy_retained_value(&vdf, 5000, 4000, &result, &iters),
                    "Endpoint from an old challenge was reused");
        }
        {
            vdf_state vdf{};
            uint8_t initial[BQFC_FORM_SIZE] = {8};
            init_vdf_state(&vdf, nullptr, "-23", initial, 20000000, 0);
            OwnedVdf owned{&vdf};
            std::vector<std::future<form *>> readers;
            for (int i = 0; i < 8; ++i)
                readers.push_back(std::async(std::launch::async, [&] {
                    form *last = nullptr;
                    for (int j = 0; j < 1000; ++j) {
                        hw_proof_value_at(&vdf, 0);
                        last = hw_proof_value_at(&vdf, 5000);
                    }
                    return last;
                }));
            form *expected = readers[0].get();
            for (size_t i = 1; i < readers.size(); ++i)
                require(readers[i].get() == expected, "Concurrent directory growth moved an intermediate block");
        }
        {
            vdf_state vdf{};
            initialize_wait_state(vdf);
            vdf.aux_threads_busy = 1;
            std::promise<void> allow_exit;
            auto permitted = allow_exit.get_future();
            vdf.aux_threads[0] = std::thread([&] { permitted.wait(); });
            auto owner = std::async(std::launch::async, [&] { hw_proof_wait_values(&vdf, true); });
            require(owner.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout,
                    "Owner did not wait for a busy worker");
            hw_request_stop_proof(&vdf);
            require(owner.wait_for(std::chrono::seconds(1)) == std::future_status::ready,
                    "Cancellation did not release the owner");
            owner.get();
            require(vdf.aux_threads[0].joinable(), "Cancellation joined an owned worker");
            allow_exit.set_value();
            hw_stop_proof(&vdf);
            require(vdf.stopping && !vdf.aux_threads[0].joinable(), "Stop left an owned worker");
        }
        {
            vdf_state vdf{};
            vdf.interval = HW_VDF_VALUE_INTERVAL;
            vdf.checkpoint_threshold = 8000;
            hw_request_proof(&vdf, 30000, false);
            hw_proof_process_req(&vdf);
            require(vdf.proofs.size() == 4, "Threshold split did not bound every segment");
            uint64_t covered = 0;
            for (size_t i = 0; i < vdf.proofs.size(); i++) {
                require(vdf.proofs[i].seg_iters <= vdf.checkpoint_threshold,
                        "Segment exceeded the checkpoint threshold");
                if (i + 1 < vdf.proofs.size())
                    require(vdf.proofs[i].seg_iters % vdf.interval == 0,
                            "Checkpoint was not aligned to the value interval");
                covered += vdf.proofs[i].seg_iters;
            }
            require(covered == 30000, "Segments do not cover the requested iteration");
        }
        {
            vdf_state vdf{};
            vdf.interval = HW_VDF_VALUE_INTERVAL;
            vdf.checkpoint_threshold = 8000;
            uint16_t checkpoint = hw_queue_proof(&vdf, 8000, HW_VDF_PROOF_NONE, 0);
            hw_request_proof(&vdf, vdf.proofs[checkpoint].iters, false);
            hw_proof_process_req(&vdf);
            require(vdf.proofs.size() == 1, "Exact checkpoint match queued another segment");
            require(vdf.proofs[0].seg_iters == 8000, "Exact match changed the checkpoint length");
            require((vdf.proofs[0].flags & HW_VDF_PROOF_FLAG_EXACT_REQ) != 0,
                    "Exact checkpoint was not delivered as the request");
            require((vdf.proofs[0].flags & HW_VDF_PROOF_FLAG_IS_REQ) == 0,
                    "Exact checkpoint was no longer a checkpoint");
            hw_request_proof(&vdf, 16000, false);
            hw_proof_process_req(&vdf);
            require(vdf.proofs.size() == 2, "Later request did not extend the exact checkpoint");
            require(vdf.proofs[1].prev == checkpoint && vdf.proofs[1].seg_iters == 8000
                    && vdf.proofs[1].iters == 16000,
                    "Later request did not continue from the reused checkpoint");
            for (const auto &proof : vdf.proofs) {
                require(proof.seg_iters != 0, "Zero-length segment was queued");
            }
            vdf.proofs[0].flags |= HW_VDF_PROOF_FLAG_DONE;
            vdf.done_proofs.push_back(checkpoint);
            vdf_proof_delivery delivery;
            int count = hw_retrieve_proof(&vdf, &delivery);
            require(count == 0 && delivery.final.iters == 8000 && delivery.final.seg_iters == 8000,
                    "Exact checkpoint request was not the checkpoint itself");
            check_frame(delivery);
        }
        {
            vdf_state vdf{};
            vdf.interval = HW_VDF_VALUE_INTERVAL;
            vdf.checkpoint_threshold = 8000;
            hw_request_proof(&vdf, 24000, false);
            hw_proof_process_req(&vdf);
            hw_request_proof(&vdf, 16000, false);
            while (!vdf.req_proofs.empty()) hw_proof_process_req(&vdf);
            bool saw_16000 = false;
            uint64_t covered_24000 = 0;
            for (uint16_t i = 0; i < vdf.proofs.size(); i++) {
                const vdf_proof &proof = vdf.proofs[i];
                if (proof.flags == 0xff) continue;
                require(proof.seg_iters != 0, "Even split queued a zero-length segment");
                if (proof.iters == 16000 &&
                        (proof.flags & (HW_VDF_PROOF_FLAG_IS_REQ | HW_VDF_PROOF_FLAG_EXACT_REQ)))
                    saw_16000 = true;
                if (proof.iters == 24000 && (proof.flags & HW_VDF_PROOF_FLAG_IS_REQ)) {
                    for (uint16_t idx = i; idx != HW_VDF_PROOF_NONE; idx = vdf.proofs[idx].prev)
                        covered_24000 += vdf.proofs[idx].seg_iters;
                }
            }
            require(saw_16000, "Request on an even checkpoint was not queued");
            require(covered_24000 == 24000, "Even-split chain does not cover its request");
        }
        {
            vdf_state vdf{};
            vdf.interval = HW_VDF_VALUE_INTERVAL;
            vdf.checkpoint_threshold = HW_VDF_MIN_CHKP_THRESHOLD;
            const uint64_t challenge = 2000000000ULL;
            hw_request_proof(&vdf, challenge, false);
            hw_proof_process_req(&vdf);
            uint16_t request = HW_VDF_PROOF_NONE;
            for (uint16_t i = 0; i < vdf.proofs.size(); i++) {
                if (vdf.proofs[i].flags & HW_VDF_PROOF_FLAG_IS_REQ) request = i;
            }
            require(request != HW_VDF_PROOF_NONE, "Long challenge did not queue a proof");
            mark_chain_done(vdf, request);
            vdf.done_proofs.push_back(request);
            vdf_proof_delivery delivery;
            int count = hw_retrieve_proof(&vdf, &delivery);
            require(count >= 0 && count <= HW_VDF_MAX_CHKP_SEGMENTS,
                    "Long challenge exceeded the checkpoint cap");
            require(static_cast<size_t>(count) == delivery.checkpoints.size(),
                    "Retrieved count does not match the snapshot");
            uint64_t covered = delivery.final.seg_iters;
            for (const auto &checkpoint : delivery.checkpoints) covered += checkpoint.seg_iters;
            require(covered == challenge, "Capped segments do not cover the challenge");
            check_frame(delivery);
        }
        {
            vdf_state vdf{};
            vdf.interval = HW_VDF_VALUE_INTERVAL;
            vdf.checkpoint_threshold = HW_VDF_MIN_CHKP_THRESHOLD;
            uint16_t prev = HW_VDF_PROOF_NONE;
            for (int i = 0; i < HW_VDF_MAX_CHKP_SEGMENTS; i++)
                prev = hw_queue_proof(&vdf, vdf.interval, prev, 0);
            hw_request_proof(&vdf, vdf.proofs[prev].iters + 2000000000ULL, false);
            hw_proof_process_req(&vdf);
            uint16_t request = HW_VDF_PROOF_NONE;
            for (uint16_t i = 0; i < vdf.proofs.size(); i++) {
                if (vdf.proofs[i].flags & HW_VDF_PROOF_FLAG_IS_REQ) request = i;
            }
            mark_chain_done(vdf, request);
            vdf.done_proofs.push_back(request);
            vdf_proof_delivery delivery;
            int count = hw_retrieve_proof(&vdf, &delivery);
            require(count == HW_VDF_MAX_CHKP_SEGMENTS, "Existing chain was extended past the cap");
            check_frame(delivery);
        }
        {
            vdf_state vdf{};
            uint8_t initial[BQFC_FORM_SIZE] = {8};
            vdf_proof_opts opts{};
            opts.retain_asic_values = true;
            init_vdf_state(&vdf, &opts, "-23", initial, 4000, 0);
            OwnedVdf owned{&vdf};
            require(vdf.retain_asic_values, "Endpoint retention option was not applied");
            form wrong = form::from_abd(integer(1), integer(1), vdf.D);
            {
                std::lock_guard<std::mutex> lock(vdf.retained_values_mtx);
                vdf.retained_values.push_back({vdf.retained_generation, 50, wrong});
            }
            vdf_proof proof{};
            proof.iters = 100;
            proof.seg_iters = 100;
            proof.flags = HW_VDF_PROOF_FLAG_IS_REQ;
            hw_compute_proof(&vdf, SIZE_MAX, &proof, 255);
            require(vdf.retained_values.empty(), "Wrong retained form was kept");
            require((proof.flags & HW_VDF_PROOF_FLAG_DONE) != 0, "Grid proof was dropped with the wrong sample");
            form honest = *hw_proof_value_at(&vdf, 0);
            PulmarkReducer reducer;
            for (int i = 0; i < 100; i++) {
                nudupl_form(honest, honest, vdf.D, vdf.L);
                reducer.reduce(honest);
            }
            uint8_t encoded[BQFC_FORM_SIZE];
            bqfc_serialize(encoded, honest.a.impl, honest.b.impl, mpz_sizeinbase(vdf.D.impl, 2));
            require(std::memcmp(proof.y, encoded, BQFC_FORM_SIZE) == 0,
                    "Proof endpoint followed the wrong retained form");

            form *slot = hw_proof_value_at(&vdf, 0);
            mpz_set_ui(slot->a.impl, 15);
            vdf_proof rejected{};
            rejected.iters = 100;
            rejected.seg_iters = 100;
            rejected.flags = HW_VDF_PROOF_FLAG_IS_REQ;
            hw_compute_proof(&vdf, SIZE_MAX, &rejected, 255);
            require((rejected.flags & HW_VDF_PROOF_FLAG_DONE) == 0,
                    "Invalid endpoint still published a proof");
        }
        {
            vdf_state vdf{};
            uint8_t initial[BQFC_FORM_SIZE] = {8};
            init_vdf_state(&vdf, nullptr, "-23", initial, 4000, 0);
            OwnedVdf owned{&vdf};
            require(vdf.value_block_count == 1, "Short challenge did not fit in one intermediate block");
            uint16_t proof = hw_queue_proof(&vdf, 100, HW_VDF_PROOF_NONE, HW_VDF_PROOF_FLAG_IS_REQ);
            auto *work = new vdf_work;
            copy_vdf_value(&work->start_val, &vdf.last_val);
            // Blocks hold 4096 forms. This is the first iteration of the next block.
            work->start_val.iters = 4096ULL * vdf.interval - 1;
            work->start_iters = 4096ULL * vdf.interval;
            work->n_steps = 0;
            vdf.wq.push_back(work);
            hw_proof_process_work(&vdf);
            require(wait_for([&] { return vdf.wq.empty() && vdf.aux_threads_busy.load() == 0; },
                             std::chrono::seconds(2)),
                    "Out-of-range aux job did not finish");
            require(vdf.value_block_count == 1, "Out-of-range position grew the intermediate directory");
            require((vdf.proofs[proof].flags & HW_VDF_PROOF_FLAG_DONE) == 0,
                    "Out-of-range aux job published a proof");
            require(vdf.done_proofs.empty(), "Out-of-range aux job published a proof");
        }
        {
            vdf_state vdf{};
            uint8_t initial[BQFC_FORM_SIZE] = {8};
            init_vdf_state(&vdf, nullptr, "-23", initial, 4000, 0);
            OwnedVdf owned{&vdf};
            uint16_t covered = hw_queue_proof(&vdf, vdf.interval, HW_VDF_PROOF_NONE,
                    HW_VDF_PROOF_FLAG_IS_REQ);
            vdf_value val;
            init_vdf_value(&val);
            val.iters = 12100;
            vdf.last_val.iters = vdf.interval;
            hw_proof_handle_value(&vdf, &val);
            require(vdf.wq.empty(), "Past-target gap was queued");
            require(vdf.last_val.iters == vdf.interval, "Past-target sample replaced the last value");
            require(vdf.queued_proofs.empty(), "Covered proof stayed queued");
            {
                std::lock_guard<std::mutex> lock(vdf.proofs_resize_mtx);
                require((vdf.proofs[covered].flags & HW_VDF_PROOF_FLAG_STARTED) != 0,
                        "Covered proof did not start");
            }
            require(vdf.done_proofs.empty(), "Past-target gap published a proof");
            clear_vdf_value(&val);
        }
        {
            vdf_state vdf{};
            uint8_t initial[BQFC_FORM_SIZE] = {8};
            init_vdf_state(&vdf, nullptr, "-23", initial, 4000, 0);
            OwnedVdf owned{&vdf};
            vdf.interval = 20;
            form wrong = *hw_proof_value_at(&vdf, 0);
            PulmarkReducer reducer;
            nudupl_form(wrong, wrong, vdf.D, vdf.L);
            reducer.reduce(wrong);
            *hw_proof_value_at(&vdf, 1) = wrong;
            vdf.valid_values[0].fetch_or(1 << 1, std::memory_order_release);
            uint16_t checkpoint = hw_queue_proof(&vdf, vdf.interval, HW_VDF_PROOF_NONE, 0);
            vdf.last_val.iters = vdf.interval;
            hw_proof_process_work(&vdf);
            require(wait_for_proof_failed(vdf, checkpoint, std::chrono::seconds(20)),
                    "Verify failure did not release the checkpoint");
            require(vdf.queued_proofs.empty(), "Failed checkpoint was requeued");
            {
                std::lock_guard<std::mutex> lock(vdf.proofs_resize_mtx);
                vdf_proof final{};
                final.iters = vdf.interval * 2;
                final.seg_iters = vdf.interval;
                final.prev = checkpoint;
                final.flags = HW_VDF_PROOF_FLAG_DONE | HW_VDF_PROOF_FLAG_IS_REQ;
                final.y[0] = final.proof[0] = 9;
                vdf.proofs.push_back(final);
                vdf.done_proofs.push_back(static_cast<uint16_t>(vdf.proofs.size() - 1));
            }
            vdf_proof_delivery dropped{};
            auto started = std::chrono::steady_clock::now();
            int count = hw_retrieve_proof(&vdf, &dropped);
            require(std::chrono::steady_clock::now() - started < std::chrono::seconds(1),
                    "Failed chain stalled retrieval");
            require(count < 0, "Failed chain was published");
            require(vdf.done_proofs.empty(), "Failed chain stayed pending");
            {
                std::lock_guard<std::mutex> lock(vdf.proofs_resize_mtx);
                vdf_proof good{};
                good.iters = 50;
                good.seg_iters = 50;
                good.prev = HW_VDF_PROOF_NONE;
                good.flags = HW_VDF_PROOF_FLAG_DONE | HW_VDF_PROOF_FLAG_IS_REQ;
                good.y[0] = good.proof[0] = 4;
                vdf.proofs.push_back(good);
                vdf.done_proofs.push_back(static_cast<uint16_t>(vdf.proofs.size() - 1));
            }
            vdf_proof_delivery delivery{};
            count = hw_retrieve_proof(&vdf, &delivery);
            require(count == 0 && delivery.final.y[0] == 4, "Later proof was not delivered");
        }
        {
            vdf_state vdf{};
            uint8_t initial[BQFC_FORM_SIZE] = {8};
            init_vdf_state(&vdf, nullptr, "-23", initial, 4000, 0);
            OwnedVdf owned{&vdf};
            vdf.interval = 20;
            uint16_t checkpoint = hw_queue_proof(&vdf, vdf.interval, HW_VDF_PROOF_NONE, 0);
            hw_request_proof(&vdf, vdf.proofs[checkpoint].iters, false);
            hw_proof_process_req(&vdf);
            require((vdf.proofs[checkpoint].flags & HW_VDF_PROOF_FLAG_EXACT_REQ) != 0,
                    "Fill checkpoint was not the exact request");
            require((vdf.proofs[checkpoint].flags & HW_VDF_PROOF_FLAG_IS_REQ) == 0,
                    "Exact fill checkpoint was queued as its own segment");
            // 4a does not divide b^2 - D, so from_abd throws before nudupl.
            // hw_job_exit abandons the gap while that exception unwinds.
            bool accepted = false;
            try {
                form ignored = form::from_abd(integer(1), integer(2), vdf.D);
                (void)ignored;
                accepted = true;
            } catch (const std::exception &) {
            }
            require(!accepted, "Bad start form was accepted by from_abd");
            vdf.last_val.iters = vdf.interval;
            auto *work = new vdf_work;
            copy_vdf_value(&work->start_val, &vdf.last_val);
            mpz_set_ui(work->start_val.a, 1);
            mpz_set_ui(work->start_val.b, 2);
            work->start_val.iters = vdf.interval - 1;
            work->start_iters = vdf.interval;
            work->n_steps = 0;
            vdf.wq.push_back(work);
            hw_proof_process_work(&vdf);
            require(wait_for_proof_failed(vdf, checkpoint, std::chrono::seconds(5),
                            HW_VDF_PROOF_FLAG_EXACT_REQ),
                    "Bad start form stalled the dependent proof");
            require((vdf.valid_values[0].load(std::memory_order_acquire) & (1u << 1)) == 0,
                    "Rejected start form published the grid point");
            require(vdf.queued_proofs.empty(), "Bad start form left the failed checkpoint queued");
            require(vdf.done_proofs.empty(), "Bad start form published the exact checkpoint");
            {
                std::lock_guard<std::mutex> lock(vdf.proofs_resize_mtx);
                vdf_proof child{};
                child.iters = vdf.interval * 2;
                child.seg_iters = vdf.interval;
                child.prev = checkpoint;
                child.flags = HW_VDF_PROOF_FLAG_DONE | HW_VDF_PROOF_FLAG_IS_REQ;
                child.y[0] = child.proof[0] = 3;
                vdf.proofs.push_back(child);
                vdf.done_proofs.push_back(static_cast<uint16_t>(vdf.proofs.size() - 1));
            }
            vdf_proof_delivery dropped{};
            auto started = std::chrono::steady_clock::now();
            int count = hw_retrieve_proof(&vdf, &dropped);
            require(std::chrono::steady_clock::now() - started < std::chrono::seconds(1),
                    "Exact checkpoint failure stalled retrieval");
            require(count < 0, "Chain through a bad start form was published");
            require(vdf.done_proofs.empty(), "Chain through a bad start form stayed pending");
            {
                std::lock_guard<std::mutex> lock(vdf.proofs_resize_mtx);
                vdf_proof good{};
                good.iters = 80;
                good.seg_iters = 80;
                good.prev = HW_VDF_PROOF_NONE;
                good.flags = HW_VDF_PROOF_FLAG_DONE | HW_VDF_PROOF_FLAG_IS_REQ;
                good.y[0] = good.proof[0] = 6;
                vdf.proofs.push_back(good);
                vdf.done_proofs.push_back(static_cast<uint16_t>(vdf.proofs.size() - 1));
            }
            vdf_proof_delivery delivery{};
            count = hw_retrieve_proof(&vdf, &delivery);
            require(count == 0 && delivery.final.y[0] == 6,
                    "Proof that does not read the missing square was not delivered");
        }
        {
            // The endpoint is the real square of the start form. Interior grid
            // points are stand-ins: an abandoned GetForm returns the form at
            // position 0. The proof continues past an interior read only when
            // that stand-in equals the real grid form, which holds when the
            // squaring period divides the interval (asserted below). Skipping
            // cancel_slot, the proof then verifies and is marked DONE after
            // several seconds; only cancellation yields FAILED before the deadline.
            vdf_state vdf{};
            uint8_t initial[BQFC_FORM_SIZE] = {8};
            const uint64_t length = 400000000;
            init_vdf_state(&vdf, nullptr, "-23", initial, length, 0);
            OwnedVdf owned{&vdf};
            form start = *hw_proof_value_at(&vdf, 0);
            uint64_t period = 0;
            form end = square_steps(start, vdf.D, vdf.L, length, &period);
            require(vdf.interval % period == 0,
                    "Squaring period does not divide the interval, so the stand-in would not equal the interior grid forms");
            require(length % vdf.interval == 0 && length / vdf.interval > 1,
                    "Abandoned segment is not a grid-aligned span with an interior point");
            require(end.check_valid(vdf.D), "Proof endpoint is not a form of the discriminant");
            size_t end_pos = length / vdf.interval;
            *hw_proof_value_at(&vdf, end_pos) = end;
            vdf.valid_values[end_pos / 8].fetch_or(1u << (end_pos % 8), std::memory_order_release);
            for (size_t pos = 1; pos < end_pos; pos++) {
                vdf.abandoned_values[pos / 8].fetch_or(1u << (pos % 8), std::memory_order_release);
            }
            uint16_t proof = hw_queue_proof(&vdf, length, HW_VDF_PROOF_NONE, HW_VDF_PROOF_FLAG_IS_REQ);
            vdf.last_val.iters = length;
            hw_proof_process_work(&vdf);
            require(wait_for_proof_failed(vdf, proof, kAbandonedProofDeadline),
                    "Abandoned interior point did not stop before the deadline");
            require(vdf.done_proofs.empty(), "Abandoned interior point published a proof");
            require(vdf.queued_proofs.empty(), "Abandoned interior point stayed queued");
        }
        {
            vdf_state vdf{};
            uint8_t initial[BQFC_FORM_SIZE] = {8};
            vdf_proof_opts opts{};
            opts.retain_asic_values = true;
            init_vdf_state(&vdf, &opts, "-23", initial, 4000, 0);
            OwnedVdf owned{&vdf};
            form at_40 = *hw_proof_value_at(&vdf, 0);
            PulmarkReducer reducer;
            for (int i = 0; i < 40; i++) {
                nudupl_form(at_40, at_40, vdf.D, vdf.L);
                reducer.reduce(at_40);
            }
            {
                std::lock_guard<std::mutex> lock(vdf.retained_values_mtx);
                vdf.retained_values.push_back({vdf.retained_generation, 40, at_40});
            }
            vdf_proof proof{};
            proof.iters = 100;
            proof.seg_iters = 100;
            proof.flags = HW_VDF_PROOF_FLAG_IS_REQ;
            hw_compute_proof(&vdf, SIZE_MAX, &proof, 255);
            {
                std::lock_guard<std::mutex> lock(vdf.retained_values_mtx);
                require(vdf.retained_values.size() == 1, "Matching retained sample was dropped");
                require(vdf.retained_values.front().iters == 40, "Matching retained sample was replaced");
                require(vdf.retained_values.front().value == at_40, "Matching retained sample was rewritten");
            }
            require((proof.flags & HW_VDF_PROOF_FLAG_DONE) != 0,
                    "Matching retained sample dropped the proof");
            form expect = at_40;
            for (int i = 0; i < 60; i++) {
                nudupl_form(expect, expect, vdf.D, vdf.L);
                reducer.reduce(expect);
            }
            uint8_t encoded[BQFC_FORM_SIZE];
            bqfc_serialize(encoded, expect.a.impl, expect.b.impl, mpz_sizeinbase(vdf.D.impl, 2));
            require(std::memcmp(proof.y, encoded, BQFC_FORM_SIZE) == 0,
                    "Proof endpoint did not follow the matching retained sample");
        }
        std::cout << "Readiness, worker draining, proof snapshots, retained endpoints, and intermediate publication passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
