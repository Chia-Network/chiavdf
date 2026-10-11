#include "hw_proof.hpp"
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>

int hw_proof_wait_value(struct vdf_state *vdf, size_t pos);

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
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
        std::cout << "Published fast path, producer visibility, and stop wakeup passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
