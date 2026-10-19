#include "hw_proof.hpp"
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <cstring>


int hw_proof_wait_value(struct vdf_state *vdf, size_t pos);
form *hw_proof_value_at(struct vdf_state *vdf, size_t pos);
void hw_proof_process_req(struct vdf_state *vdf);
void hw_compute_proof(struct vdf_state *vdf, size_t proof_idx, struct vdf_proof *out_proof, uint8_t thr_idx);

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
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
            uint8_t initial[BQFC_FORM_SIZE] = {8};
            vdf_proof_opts opts{};
            opts.retain_asic_values = true;
            init_vdf_state(&vdf, &opts, "-23", initial, 4000, 0);
            OwnedVdf owned{&vdf};
            form wrong = form::identity(vdf.D);
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
            uint8_t wrong_bytes[BQFC_FORM_SIZE];
            bqfc_serialize(wrong_bytes, wrong.a.impl, wrong.b.impl, mpz_sizeinbase(vdf.D.impl, 2));
            require(std::memcmp(proof.y, wrong_bytes, BQFC_FORM_SIZE) != 0,
                    "Wrong retained form was published");

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
        std::cout << "Readiness, worker draining, proof snapshots, retained endpoints, and intermediate publication passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
