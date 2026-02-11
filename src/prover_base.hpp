#ifndef PROVER_BASE_H
#define PROVER_BASE_H

#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

#include "proof_common.h"
#include "util.h"

inline const char* ProverAgentDebugLogPath() {
    const char* env_path = std::getenv("CHIAVDF_AGENT_DEBUG_LOG");
    if (env_path != nullptr && env_path[0] != '\0') {
        return env_path;
    }
    return "/Users/hoffmang/src/chiavdf/.cursor/debug.log";
}

inline bool ProverAgentDebugShouldMirror(const char* hypothesis_id) {
    const char* mirror_all = std::getenv("CHIAVDF_AGENT_DEBUG_MIRROR_ALL");
    if (mirror_all != nullptr && mirror_all[0] == '1') {
        return true;
    }
    return std::strcmp(hypothesis_id, "H14") == 0 ||
           std::strcmp(hypothesis_id, "H16") == 0 ||
           std::strcmp(hypothesis_id, "H18") == 0 ||
           std::strcmp(hypothesis_id, "H19") == 0 ||
           std::strcmp(hypothesis_id, "H22") == 0 ||
           std::strcmp(hypothesis_id, "H28") == 0 ||
           std::strcmp(hypothesis_id, "H29") == 0 ||
           std::strcmp(hypothesis_id, "H32") == 0 ||
           std::strcmp(hypothesis_id, "H33") == 0 ||
           std::strcmp(hypothesis_id, "H34") == 0 ||
           std::strcmp(hypothesis_id, "H35") == 0 ||
           std::strcmp(hypothesis_id, "H36") == 0 ||
           std::strcmp(hypothesis_id, "H38") == 0 ||
           std::strcmp(hypothesis_id, "H40") == 0 ||
           std::strcmp(hypothesis_id, "H41") == 0 ||
           std::strcmp(hypothesis_id, "H42") == 0 ||
           std::strcmp(hypothesis_id, "H43") == 0 ||
           std::strcmp(hypothesis_id, "H44") == 0 ||
           std::strcmp(hypothesis_id, "H45") == 0 ||
           std::strcmp(hypothesis_id, "H46") == 0 ||
           std::strcmp(hypothesis_id, "H52") == 0;
}

inline void ProverAgentDebugLog(const char* run_id, const char* hypothesis_id, const char* location, const char* message, const std::string& data_json) {
    static std::atomic<uint64_t> seq{0};
    std::ofstream out(ProverAgentDebugLogPath(), std::ios::app);
    if (!out.is_open()) {
        return;
    }
    const auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    const uint64_t id = ++seq;
    out << "{\"id\":\"log_" << ts << "_" << id
        << "\",\"timestamp\":" << ts
        << ",\"runId\":\"" << run_id
        << "\",\"hypothesisId\":\"" << hypothesis_id
        << "\",\"location\":\"" << location
        << "\",\"message\":\"" << message
        << "\",\"data\":" << data_json
        << "}\n";
    if (ProverAgentDebugShouldMirror(hypothesis_id)) {
        std::cerr << "AGENTLOG "
                  << "{\"id\":\"log_" << ts << "_" << id
                  << "\",\"timestamp\":" << ts
                  << ",\"runId\":\"" << run_id
                  << "\",\"hypothesisId\":\"" << hypothesis_id
                  << "\",\"location\":\"" << location
                  << "\",\"message\":\"" << message
                  << "\",\"data\":" << data_json
                  << "}\n";
    }
}

class Prover {
  public:
    Prover(Segment segm, integer D) {
        this->segm = segm;
        this->D = D;
        this->num_iterations = segm.length;
        is_finished = false;
    }

    virtual form* GetForm(uint64_t iteration) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool PerformExtraStep() = 0;
    virtual void OnFinish() = 0;

    bool IsFinished() {
        return is_finished;
    }

    form GetProof() {
        return proof;
    }

    uint64_t GetBlock(uint64_t i, uint64_t k, uint64_t T, integer& B) {
        integer res = FastPow(2, T - k * (i + 1), B);
        mpz_mul_2exp(res.impl, res.impl, k);
        res = res / B;
        auto res_vector = res.to_vector();
        return res_vector.empty() ? 0 : res_vector[0];
    }

    void GenerateProof() {
        // #region agent log
        ProverAgentDebugLog(
            "post-fix",
            "H15",
            "prover_base.hpp:Prover:GenerateProof_enter",
            "GenerateProof enter",
            std::string("{\"num_iterations\":") + std::to_string(num_iterations) +
                ",\"k\":" + std::to_string(k) +
                ",\"l\":" + std::to_string(l) +
                ",\"seg_start\":" + std::to_string(segm.start) +
                ",\"seg_length\":" + std::to_string(segm.length) + "}");
        // #endregion
        PulmarkReducer reducer;

        integer B = GetB(D, segm.x, segm.y);
        integer L=root(-D, 4);
        form id;
        try {
            id = form::identity(D);
        } catch(std::exception& e) {
            std::cout << "Warning: Could not create identity: " << e.what() << "\n";
            std::cout << "Discriminant: " << D.to_string() << "\n";
            std::cout << "Segment start:" << segm.start << "\n";
            std::cout << "Segment length:" << segm.length << "\n";
            std::cout << std::flush;

            return ;
        }
        // #region agent log
        ProverAgentDebugLog(
            "post-fix",
            "H15",
            "prover_base.hpp:Prover:GenerateProof_after_identity",
            "Identity setup complete",
            "{}");
        // #endregion
        uint64_t k1 = k / 2;
        uint64_t k0 = k - k1;
        form x = id;

        for (int64_t j = l - 1; j >= 0; j--) {
            x = FastPowFormNucomp(x, D, integer(1 << k), L, reducer);

            std::vector<form> ys((1 << k));
            for (uint64_t i = 0; i < (1UL << k); i++)
                ys[i] = id;

            form *tmp;
            uint64_t limit = num_iterations / (k * l);
            if (num_iterations % (k * l))
                limit++;
            // #region agent log
            if (j == static_cast<int64_t>(l - 1)) {
                ProverAgentDebugLog(
                    "post-fix",
                    "H15",
                    "prover_base.hpp:Prover:GenerateProof_first_round",
                    "First outer round setup",
                    std::string("{\"j\":") + std::to_string(j) +
                        ",\"limit\":" + std::to_string(limit) + "}");
            }
            // #endregion
            for (uint64_t i = 0; i < limit; i++) {
                if (num_iterations >= k * (i * l + j + 1)) {
                    if (segm.start >= 10000000 && j == 0 && i >= 264300 && i <= 264700) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H33",
                            "prover_base.hpp:Prover:GenerateProof_boundary_iter_enter",
                            "Boundary window iteration enter",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"limit\":" + std::to_string(limit) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264430 && i <= 264432) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H34",
                            "prover_base.hpp:Prover:GenerateProof_iter_before_getblock",
                            "Focused window before GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264701 && i <= 264760) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H35",
                            "prover_base.hpp:Prover:GenerateProof_post700_before_getblock",
                            "Post-264700 focused window before GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 269980 && i <= 270060) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H36",
                            "prover_base.hpp:Prover:GenerateProof_270k_before_getblock",
                            "270k window before GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264318 && i <= 264324) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H38",
                            "prover_base.hpp:Prover:GenerateProof_crash_window_before_getblock",
                            "Crash-window before GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264410 && i <= 264430) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H40",
                            "prover_base.hpp:Prover:GenerateProof_h40_before_getblock",
                            "H40 window before GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 263500 && i <= 263520) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H41",
                            "prover_base.hpp:Prover:GenerateProof_h41_before_getblock",
                            "H41 window before GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260201 && i <= 260240) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H42",
                            "prover_base.hpp:Prover:GenerateProof_h42_before_getblock",
                            "H42 micro window before GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264201 && i <= 264240) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H43",
                            "prover_base.hpp:Prover:GenerateProof_h43_before_getblock",
                            "H43 micro window before GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 250001 && i <= 250060) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H44",
                            "prover_base.hpp:Prover:GenerateProof_h44_before_getblock",
                            "H44 micro window before GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260701 && i <= 260760) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H45",
                            "prover_base.hpp:Prover:GenerateProof_h45_before_getblock",
                            "H45 micro window before GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 250061 && i <= 250120) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H46",
                            "prover_base.hpp:Prover:GenerateProof_h46_before_getblock",
                            "H46 micro window before GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260000 && i <= 265000 && (i % 100 == 0)) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H29",
                            "prover_base.hpp:Prover:GenerateProof_tail_before_getblock",
                            "Tail window before GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"num_iterations\":" + std::to_string(num_iterations) + "}");
                        // #endregion
                    }
                    uint64_t b = GetBlock(i*l + j, k, num_iterations, B);
                    if (segm.start >= 10000000 && j == 0 && i >= 264430 && i <= 264432) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H34",
                            "prover_base.hpp:Prover:GenerateProof_iter_after_getblock",
                            "Focused window after GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"b_in_range\":" + ((b < (1UL << k)) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264701 && i <= 264760) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H35",
                            "prover_base.hpp:Prover:GenerateProof_post700_after_getblock",
                            "Post-264700 focused window after GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"b_in_range\":" + ((b < (1UL << k)) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 269980 && i <= 270060) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H36",
                            "prover_base.hpp:Prover:GenerateProof_270k_after_getblock",
                            "270k window after GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"b_in_range\":" + ((b < (1UL << k)) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264318 && i <= 264324) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H38",
                            "prover_base.hpp:Prover:GenerateProof_crash_window_after_getblock",
                            "Crash-window after GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"b_in_range\":" + ((b < (1UL << k)) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264410 && i <= 264430) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H40",
                            "prover_base.hpp:Prover:GenerateProof_h40_after_getblock",
                            "H40 window after GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"b_in_range\":" + ((b < (1UL << k)) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 263500 && i <= 263520) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H41",
                            "prover_base.hpp:Prover:GenerateProof_h41_after_getblock",
                            "H41 window after GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"b_in_range\":" + ((b < (1UL << k)) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260201 && i <= 260240) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H42",
                            "prover_base.hpp:Prover:GenerateProof_h42_after_getblock",
                            "H42 micro window after GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"b_in_range\":" + ((b < (1UL << k)) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264201 && i <= 264240) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H43",
                            "prover_base.hpp:Prover:GenerateProof_h43_after_getblock",
                            "H43 micro window after GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"b_in_range\":" + ((b < (1UL << k)) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 250001 && i <= 250060) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H44",
                            "prover_base.hpp:Prover:GenerateProof_h44_after_getblock",
                            "H44 micro window after GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"b_in_range\":" + ((b < (1UL << k)) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260701 && i <= 260760) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H45",
                            "prover_base.hpp:Prover:GenerateProof_h45_after_getblock",
                            "H45 micro window after GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"b_in_range\":" + ((b < (1UL << k)) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 250061 && i <= 250120) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H46",
                            "prover_base.hpp:Prover:GenerateProof_h46_after_getblock",
                            "H46 micro window after GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"b_in_range\":" + ((b < (1UL << k)) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 265001 && i <= 265200) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H30",
                            "prover_base.hpp:Prover:GenerateProof_micro_after_getblock",
                            "Micro window after GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260000 && i <= 265000 && (i % 100 == 0)) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H29",
                            "prover_base.hpp:Prover:GenerateProof_tail_after_getblock",
                            "Tail window after GetBlock",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    if (b >= (1UL << k)) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H18",
                            "prover_base.hpp:Prover:GenerateProof_block_oob",
                            "Computed block index out of bounds",
                            std::string("{\"b\":") + std::to_string(b) +
                                ",\"k\":" + std::to_string(k) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"j\":" + std::to_string(j) +
                                ",\"num_iterations\":" + std::to_string(num_iterations) + "}");
                        // #endregion
                        throw std::runtime_error("GenerateProof block index out of bounds");
                    }
                    if (!PerformExtraStep()) return;
                    if (segm.start >= 10000000 && j == 0 && i >= 265001 && i <= 265200) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H30",
                            "prover_base.hpp:Prover:GenerateProof_micro_after_extra_step",
                            "Micro window after PerformExtraStep",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260000 && i <= 265000 && (i % 100 == 0)) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H28",
                            "prover_base.hpp:Prover:GenerateProof_tail_before_getform",
                            "Tail window before GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && (i % 50000 == 0)) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H24",
                            "prover_base.hpp:Prover:GenerateProof_progress_before_getform",
                            "Long GenerateProof progress before GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"limit\":" + std::to_string(limit) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    // #region agent log
                    if (j == static_cast<int64_t>(l - 1) && i == 0) {
                        ProverAgentDebugLog(
                            "post-fix",
                            "H15",
                            "prover_base.hpp:Prover:GenerateProof_before_first_getform",
                            "About to fetch first intermediate form",
                            std::string("{\"block\":") + std::to_string(b) + "}");
                    }
                    // #endregion
                    if (segm.start >= 10000000 && j == 0 && i >= 265001 && i <= 265200) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H32",
                            "prover_base.hpp:Prover:GenerateProof_micro_before_getform_call",
                            "Micro window immediately before GetForm call",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264430 && i <= 264432) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H34",
                            "prover_base.hpp:Prover:GenerateProof_iter_before_getform",
                            "Focused window before GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264701 && i <= 264760) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H35",
                            "prover_base.hpp:Prover:GenerateProof_post700_before_getform",
                            "Post-264700 focused window before GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 269980 && i <= 270060) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H36",
                            "prover_base.hpp:Prover:GenerateProof_270k_before_getform",
                            "270k window before GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264318 && i <= 264324) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H38",
                            "prover_base.hpp:Prover:GenerateProof_crash_window_before_getform",
                            "Crash-window before GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264410 && i <= 264430) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H40",
                            "prover_base.hpp:Prover:GenerateProof_h40_before_getform",
                            "H40 window before GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260201 && i <= 260240) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H42",
                            "prover_base.hpp:Prover:GenerateProof_h42_before_getform",
                            "H42 micro window before GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264201 && i <= 264240) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H43",
                            "prover_base.hpp:Prover:GenerateProof_h43_before_getform",
                            "H43 micro window before GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 250001 && i <= 250060) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H44",
                            "prover_base.hpp:Prover:GenerateProof_h44_before_getform",
                            "H44 micro window before GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260701 && i <= 260760) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H45",
                            "prover_base.hpp:Prover:GenerateProof_h45_before_getform",
                            "H45 micro window before GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 250061 && i <= 250120) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H46",
                            "prover_base.hpp:Prover:GenerateProof_h46_before_getform",
                            "H46 micro window before GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 230000 && i <= 230200) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H52",
                            "prover_base.hpp:Prover:GenerateProof_h52_before_getform",
                            "H52 focused window before GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    tmp = GetForm(i);
                    if (segm.start >= 10000000 && j == 0 && i >= 264430 && i <= 264432) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H34",
                            "prover_base.hpp:Prover:GenerateProof_iter_after_getform",
                            "Focused window after GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") +
                                ",\"tmp_a_bits\":" + std::to_string(tmp->a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264701 && i <= 264760) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H35",
                            "prover_base.hpp:Prover:GenerateProof_post700_after_getform",
                            "Post-264700 focused window after GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") +
                                ",\"tmp_a_bits\":" + std::to_string(tmp->a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 269980 && i <= 270060) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H36",
                            "prover_base.hpp:Prover:GenerateProof_270k_after_getform",
                            "270k window after GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") +
                                ",\"tmp_a_bits\":" + std::to_string(tmp->a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264318 && i <= 264324) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H38",
                            "prover_base.hpp:Prover:GenerateProof_crash_window_after_getform",
                            "Crash-window after GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") +
                                ",\"tmp_a_bits\":" + std::to_string(tmp->a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264410 && i <= 264430) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H40",
                            "prover_base.hpp:Prover:GenerateProof_h40_after_getform",
                            "H40 window after GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") +
                                ",\"tmp_a_bits\":" + std::to_string(tmp->a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260201 && i <= 260240) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H42",
                            "prover_base.hpp:Prover:GenerateProof_h42_after_getform",
                            "H42 micro window after GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") +
                                ",\"tmp_a_bits\":" + std::to_string(tmp->a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264201 && i <= 264240) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H43",
                            "prover_base.hpp:Prover:GenerateProof_h43_after_getform",
                            "H43 micro window after GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") +
                                ",\"tmp_a_bits\":" + std::to_string(tmp->a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 250001 && i <= 250060) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H44",
                            "prover_base.hpp:Prover:GenerateProof_h44_after_getform",
                            "H44 micro window after GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") +
                                ",\"tmp_a_bits\":" + std::to_string(tmp->a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260701 && i <= 260760) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H45",
                            "prover_base.hpp:Prover:GenerateProof_h45_after_getform",
                            "H45 micro window after GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") +
                                ",\"tmp_a_bits\":" + std::to_string(tmp->a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 250061 && i <= 250120) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H46",
                            "prover_base.hpp:Prover:GenerateProof_h46_after_getform",
                            "H46 micro window after GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") +
                                ",\"tmp_a_bits\":" + std::to_string(tmp->a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 230000 && i <= 230200) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H52",
                            "prover_base.hpp:Prover:GenerateProof_h52_after_getform",
                            "H52 focused window after GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") +
                                ",\"tmp_a_bits\":" + std::to_string(tmp->a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 265001 && i <= 265200) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H30",
                            "prover_base.hpp:Prover:GenerateProof_micro_after_getform",
                            "Micro window after GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260000 && i <= 265000 && (i % 100 == 0)) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H28",
                            "prover_base.hpp:Prover:GenerateProof_tail_after_getform",
                            "Tail window after GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && (i % 50000 == 0)) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H24",
                            "prover_base.hpp:Prover:GenerateProof_progress_after_getform",
                            "Long GenerateProof progress after GetForm",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    // #region agent log
                    if (j == static_cast<int64_t>(l - 1) && i == 0) {
                        ProverAgentDebugLog(
                            "post-fix",
                            "H15",
                            "prover_base.hpp:Prover:GenerateProof_after_first_getform",
                            "Fetched first intermediate form",
                            "{}");
                    }
                    // #endregion
                    if (segm.start >= 10000000 && j == 0 && (i % 10000 == 0)) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H26",
                            "prover_base.hpp:Prover:GenerateProof_before_nucomp_fine",
                            "Fine-grained long GenerateProof before nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") +
                                ",\"ys_a_bits\":" + std::to_string(ys[b].a.num_bits()) +
                                ",\"tmp_a_bits\":" + std::to_string(tmp->a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260000 && i <= 265000 && (i % 100 == 0)) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H28",
                            "prover_base.hpp:Prover:GenerateProof_tail_before_nucomp",
                            "Tail window before nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264430 && i <= 264432) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H34",
                            "prover_base.hpp:Prover:GenerateProof_iter_before_nucomp",
                            "Focused window before nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264701 && i <= 264760) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H35",
                            "prover_base.hpp:Prover:GenerateProof_post700_before_nucomp",
                            "Post-264700 focused window before nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 269980 && i <= 270060) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H36",
                            "prover_base.hpp:Prover:GenerateProof_270k_before_nucomp",
                            "270k window before nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264318 && i <= 264324) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H38",
                            "prover_base.hpp:Prover:GenerateProof_crash_window_before_nucomp",
                            "Crash-window before nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264410 && i <= 264430) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H40",
                            "prover_base.hpp:Prover:GenerateProof_h40_before_nucomp",
                            "H40 window before nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260201 && i <= 260240) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H42",
                            "prover_base.hpp:Prover:GenerateProof_h42_before_nucomp",
                            "H42 micro window before nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264201 && i <= 264240) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H43",
                            "prover_base.hpp:Prover:GenerateProof_h43_before_nucomp",
                            "H43 micro window before nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 250001 && i <= 250060) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H44",
                            "prover_base.hpp:Prover:GenerateProof_h44_before_nucomp",
                            "H44 micro window before nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260701 && i <= 260760) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H45",
                            "prover_base.hpp:Prover:GenerateProof_h45_before_nucomp",
                            "H45 micro window before nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 250061 && i <= 250120) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H46",
                            "prover_base.hpp:Prover:GenerateProof_h46_before_nucomp",
                            "H46 micro window before nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"tmp_valid\":" + (tmp->check_valid(D) ? "true" : "false") + "}");
                        // #endregion
                    }
                    nucomp_form(ys[b], ys[b], *tmp, D, L);
                    if (segm.start >= 10000000 && j == 0 && i >= 264430 && i <= 264432) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H34",
                            "prover_base.hpp:Prover:GenerateProof_iter_after_nucomp",
                            "Focused window after nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"ys_a_bits\":" + std::to_string(ys[b].a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 230000 && i <= 230200) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H52",
                            "prover_base.hpp:Prover:GenerateProof_h52_after_nucomp",
                            "H52 focused window after nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"ys_a_bits\":" + std::to_string(ys[b].a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264701 && i <= 264760) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H35",
                            "prover_base.hpp:Prover:GenerateProof_post700_after_nucomp",
                            "Post-264700 focused window after nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"ys_a_bits\":" + std::to_string(ys[b].a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 269980 && i <= 270060) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H36",
                            "prover_base.hpp:Prover:GenerateProof_270k_after_nucomp",
                            "270k window after nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"ys_a_bits\":" + std::to_string(ys[b].a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264318 && i <= 264324) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H38",
                            "prover_base.hpp:Prover:GenerateProof_crash_window_after_nucomp",
                            "Crash-window after nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"ys_a_bits\":" + std::to_string(ys[b].a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264410 && i <= 264430) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H40",
                            "prover_base.hpp:Prover:GenerateProof_h40_after_nucomp",
                            "H40 window after nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"ys_a_bits\":" + std::to_string(ys[b].a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 263500 && i <= 263520) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H41",
                            "prover_base.hpp:Prover:GenerateProof_h41_after_nucomp",
                            "H41 window after nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"ys_a_bits\":" + std::to_string(ys[b].a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260201 && i <= 260240) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H42",
                            "prover_base.hpp:Prover:GenerateProof_h42_after_nucomp",
                            "H42 micro window after nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"ys_a_bits\":" + std::to_string(ys[b].a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264201 && i <= 264240) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H43",
                            "prover_base.hpp:Prover:GenerateProof_h43_after_nucomp",
                            "H43 micro window after nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"ys_a_bits\":" + std::to_string(ys[b].a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 250001 && i <= 250060) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H44",
                            "prover_base.hpp:Prover:GenerateProof_h44_after_nucomp",
                            "H44 micro window after nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"ys_a_bits\":" + std::to_string(ys[b].a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260701 && i <= 260760) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H45",
                            "prover_base.hpp:Prover:GenerateProof_h45_after_nucomp",
                            "H45 micro window after nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"ys_a_bits\":" + std::to_string(ys[b].a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 250061 && i <= 250120) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H46",
                            "prover_base.hpp:Prover:GenerateProof_h46_after_nucomp",
                            "H46 micro window after nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"ys_a_bits\":" + std::to_string(ys[b].a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 265001 && i <= 265200) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H30",
                            "prover_base.hpp:Prover:GenerateProof_micro_after_nucomp",
                            "Micro window after nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260000 && i <= 265000 && (i % 100 == 0)) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H28",
                            "prover_base.hpp:Prover:GenerateProof_tail_after_nucomp",
                            "Tail window after nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && (i % 10000 == 0)) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H26",
                            "prover_base.hpp:Prover:GenerateProof_after_nucomp_fine",
                            "Fine-grained long GenerateProof after nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"ys_a_bits\":" + std::to_string(ys[b].a.num_bits()) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && (i % 50000 == 0)) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H24",
                            "prover_base.hpp:Prover:GenerateProof_progress_after_nucomp",
                            "Long GenerateProof progress after nucomp_form",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"b\":" + std::to_string(b) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 260000 && (i % 5000 == 0)) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H27",
                            "prover_base.hpp:Prover:GenerateProof_tail_progress",
                            "Long GenerateProof tail progress marker",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"limit\":" + std::to_string(limit) + "}");
                        // #endregion
                    }
                    if (segm.start >= 10000000 && j == 0 && i >= 264300 && i <= 264700) {
                        // #region agent log
                        ProverAgentDebugLog(
                            "post-fix",
                            "H33",
                            "prover_base.hpp:Prover:GenerateProof_boundary_iter_exit",
                            "Boundary window iteration exit",
                            std::string("{\"seg_start\":") + std::to_string(segm.start) +
                                ",\"i\":" + std::to_string(i) +
                                ",\"ys_valid\":" + (ys[b].check_valid(D) ? "true" : "false") +
                                ",\"ys_a_bits\":" + std::to_string(ys[b].a.num_bits()) + "}");
                        // #endregion
                    }
                }
            }
            if (segm.start >= 10000000 && j == 0) {
                // #region agent log
                ProverAgentDebugLog(
                    "post-fix",
                    "H27",
                    "prover_base.hpp:Prover:GenerateProof_after_collect_phase",
                    "Completed long GenerateProof collect phase",
                    std::string("{\"seg_start\":") + std::to_string(segm.start) +
                        ",\"limit\":" + std::to_string(limit) + "}");
                // #endregion
            }

            for (uint64_t b1 = 0; b1 < (1UL << k1); b1++) {
                if (segm.start >= 10000000 && j == 0 && (b1 % 16 == 0)) {
                    // #region agent log
                    ProverAgentDebugLog(
                        "post-fix",
                        "H27",
                        "prover_base.hpp:Prover:GenerateProof_phase2_progress",
                        "Long GenerateProof phase2 progress",
                        std::string("{\"seg_start\":") + std::to_string(segm.start) +
                            ",\"b1\":" + std::to_string(b1) + "}");
                    // #endregion
                }
                form z = id;
                for (uint64_t b0 = 0; b0 < (1UL << k0); b0++) {
                    if (!PerformExtraStep()) return;
                    nucomp_form(z, z, ys[b1 * (1 << k0) + b0], D, L);
                }
                z = FastPowFormNucomp(z, D, integer(b1 * (1 << k0)), L, reducer);
                nucomp_form(x, x, z, D, L);
            }
            if (segm.start >= 10000000 && j == 0) {
                // #region agent log
                ProverAgentDebugLog(
                    "post-fix",
                    "H27",
                    "prover_base.hpp:Prover:GenerateProof_after_phase2",
                    "Completed long GenerateProof phase2",
                    std::string("{\"seg_start\":") + std::to_string(segm.start) + "}");
                // #endregion
            }

            for (uint64_t b0 = 0; b0 < (1UL << k0); b0++) {
                if (segm.start >= 10000000 && j == 0 && (b0 % 16 == 0)) {
                    // #region agent log
                    ProverAgentDebugLog(
                        "post-fix",
                        "H27",
                        "prover_base.hpp:Prover:GenerateProof_phase3_progress",
                        "Long GenerateProof phase3 progress",
                        std::string("{\"seg_start\":") + std::to_string(segm.start) +
                            ",\"b0\":" + std::to_string(b0) + "}");
                    // #endregion
                }
                form z = id;
                for (uint64_t b1 = 0; b1 < (1UL << k1); b1++) {
                    if (!PerformExtraStep()) return;
                    nucomp_form(z, z, ys[b1 * (1 << k0) + b0], D, L);
                }
                z = FastPowFormNucomp(z, D, integer(b0), L, reducer);
                nucomp_form(x, x, z, D, L);
            }
            if (segm.start >= 10000000 && j == 0) {
                // #region agent log
                ProverAgentDebugLog(
                    "post-fix",
                    "H27",
                    "prover_base.hpp:Prover:GenerateProof_after_phase3",
                    "Completed long GenerateProof phase3",
                    std::string("{\"seg_start\":") + std::to_string(segm.start) + "}");
                // #endregion
            }
        }
        reducer.reduce(x);
        proof = x;
        OnFinish();
        // #region agent log
        ProverAgentDebugLog(
            "post-fix",
            "H15",
            "prover_base.hpp:Prover:GenerateProof_exit",
            "GenerateProof finished",
            "{}");
        // #endregion
    }

  protected:
    Segment segm;
    integer D;
    form proof;
    uint64_t num_iterations;
    uint32_t k;
    uint32_t l;
    std::atomic<bool> is_finished;
};
#endif // PROVER_BASE_H
