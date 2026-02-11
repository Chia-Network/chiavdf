#ifndef PROVERS_H
#define PROVERS_H

#include "prover_base.hpp"
#include "callback.h"

class OneWesolowskiProver : public Prover {
  public:
    OneWesolowskiProver(Segment segm, integer D, form* intermediates, std::atomic<bool>& stop_signal)
        : Prover(segm, D), stop_signal(stop_signal)
    {
        this->intermediates = intermediates;
        if (num_iterations >= (1 << 16)) {
            ApproximateParameters(num_iterations, l, k);
        } else {
            k = 10;
            l = 1;
        }
    }

    form* GetForm(uint64_t iteration) {
        return &intermediates[iteration];
    }

    void start() {
        GenerateProof();
    }

    void stop() {
    }

    bool PerformExtraStep() {
        return !stop_signal;
    }

    void OnFinish() {
        is_finished = true;
    }

  private:
    form* intermediates;
    std::atomic<bool>& stop_signal;
};

class TwoWesolowskiProver : public Prover{
  public:
    TwoWesolowskiProver(Segment segm, integer D, TwoWesolowskiCallback* weso, std::atomic<bool>& stop_signal):
        Prover(segm, D), stop_signal(stop_signal)
    {
        this->weso = weso;
        this->done_iterations = segm.start;
        k = 10;
        l = (segm.length < 10000000) ? 1 : 10;
    }

    void start() {
        // #region agent log
        CallbackAgentDebugLog("post-fix", "H14", "provers.h:TwoWesolowskiProver:start_enter", "TwoWesolowskiProver start called", "{}");
        // #endregion
        try {
            std::thread t([=] { GenerateProof(); });
            t.detach();
            // #region agent log
            CallbackAgentDebugLog("post-fix", "H14", "provers.h:TwoWesolowskiProver:start_detached", "Prover thread created and detached", "{}");
            // #endregion
        } catch (const std::system_error& e) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H14",
                "provers.h:TwoWesolowskiProver:start_fallback_inline",
                "Thread creation failed, running inline",
                std::string("{\"what\":\"") + e.what() + "\"}");
            // #endregion
            GenerateProof();
        }
    }

    virtual form* GetForm(uint64_t i) {
        const uint64_t power = done_iterations + i * k * l;
        const int64_t produced = weso->iterations.load(std::memory_order_relaxed);
        if (done_iterations >= 10000000 && (i % 50000 == 0)) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H25",
                "provers.h:TwoWesolowskiProver:GetForm_progress",
                "Long prover GetForm progress",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) +
                    ",\"k\":" + std::to_string(k) +
                    ",\"l\":" + std::to_string(l) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 260000 && i <= 265000 && (i % 100 == 0)) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H29",
                "provers.h:TwoWesolowskiProver:GetForm_tail_window",
                "Tail window prover GetForm details",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) +
                    ",\"k\":" + std::to_string(k) +
                    ",\"l\":" + std::to_string(l) + "}");
            // #endregion
        }
        if (power > static_cast<uint64_t>(produced)) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H22",
                "provers.h:TwoWesolowskiProver:GetForm_read_ahead",
                "Requested form ahead of produced iterations",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) +
                    ",\"done_iterations\":" + std::to_string(done_iterations) +
                    ",\"k\":" + std::to_string(k) +
                    ",\"l\":" + std::to_string(l) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 265001 && i <= 265200) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H30",
                "provers.h:TwoWesolowskiProver:GetForm_micro_before_copy",
                "Micro window before GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 264430 && i <= 264432) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H34",
                "provers.h:TwoWesolowskiProver:GetForm_focused_before_copy",
                "Focused window before GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 264701 && i <= 264760) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H35",
                "provers.h:TwoWesolowskiProver:GetForm_post700_before_copy",
                "Post-264700 focused window before GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 269980 && i <= 270060) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H36",
                "provers.h:TwoWesolowskiProver:GetForm_270k_before_copy",
                "270k window before GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 264318 && i <= 264324) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H38",
                "provers.h:TwoWesolowskiProver:GetForm_crash_window_before_copy",
                "Crash-window before GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 264410 && i <= 264430) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H40",
                "provers.h:TwoWesolowskiProver:GetForm_h40_before_copy",
                "H40 window before GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 263500 && i <= 263520) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H41",
                "provers.h:TwoWesolowskiProver:GetForm_h41_before_copy",
                "H41 window before GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 260201 && i <= 260240) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H42",
                "provers.h:TwoWesolowskiProver:GetForm_h42_before_copy",
                "H42 micro window before GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 264201 && i <= 264240) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H43",
                "provers.h:TwoWesolowskiProver:GetForm_h43_before_copy",
                "H43 micro window before GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 250001 && i <= 250060) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H44",
                "provers.h:TwoWesolowskiProver:GetForm_h44_before_copy",
                "H44 micro window before GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 260701 && i <= 260760) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H45",
                "provers.h:TwoWesolowskiProver:GetForm_h45_before_copy",
                "H45 micro window before GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 250061 && i <= 250120) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H46",
                "provers.h:TwoWesolowskiProver:GetForm_h46_before_copy",
                "H46 micro window before GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 250058 && i <= 250062) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H47",
                "provers.h:TwoWesolowskiProver:GetForm_h47_before_copy",
                "H47 tight boundary before GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 250090 && i <= 250100) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H50",
                "provers.h:TwoWesolowskiProver:GetForm_h50_before_copy",
                "H50 boundary before GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 250061 && i <= 250063) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H51",
                "provers.h:TwoWesolowskiProver:GetForm_h51_before_copy",
                "H51 boundary before GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 250030 && i <= 250040) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H49",
                "provers.h:TwoWesolowskiProver:GetForm_h49_before_copy",
                "H49 boundary before GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"produced_iterations\":" + std::to_string(produced) +
                    ",\"weso_ptr\":" +
                    std::to_string(static_cast<unsigned long long>(reinterpret_cast<uint64_t>(weso))) + "}");
            // #endregion
        }
        cached_form = weso->GetFormCopy(power);
        if (done_iterations >= 10000000 && i >= 264430 && i <= 264432) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H34",
                "provers.h:TwoWesolowskiProver:GetForm_focused_after_copy",
                "Focused window after GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"cached_valid\":" + (cached_form.check_valid(D) ? "true" : "false") +
                    ",\"cached_a_bits\":" + std::to_string(cached_form.a.num_bits()) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 264701 && i <= 264760) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H35",
                "provers.h:TwoWesolowskiProver:GetForm_post700_after_copy",
                "Post-264700 focused window after GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"cached_valid\":" + (cached_form.check_valid(D) ? "true" : "false") +
                    ",\"cached_a_bits\":" + std::to_string(cached_form.a.num_bits()) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 269980 && i <= 270060) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H36",
                "provers.h:TwoWesolowskiProver:GetForm_270k_after_copy",
                "270k window after GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"cached_valid\":" + (cached_form.check_valid(D) ? "true" : "false") +
                    ",\"cached_a_bits\":" + std::to_string(cached_form.a.num_bits()) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 264410 && i <= 264430) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H40",
                "provers.h:TwoWesolowskiProver:GetForm_h40_after_copy",
                "H40 window after GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"cached_valid\":" + (cached_form.check_valid(D) ? "true" : "false") +
                    ",\"cached_a_bits\":" + std::to_string(cached_form.a.num_bits()) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 264318 && i <= 264324) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H38",
                "provers.h:TwoWesolowskiProver:GetForm_crash_window_after_copy",
                "Crash-window after GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"cached_valid\":" + (cached_form.check_valid(D) ? "true" : "false") +
                    ",\"cached_a_bits\":" + std::to_string(cached_form.a.num_bits()) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 263500 && i <= 263520) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H41",
                "provers.h:TwoWesolowskiProver:GetForm_h41_after_copy",
                "H41 window after GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"cached_valid\":" + (cached_form.check_valid(D) ? "true" : "false") +
                    ",\"cached_a_bits\":" + std::to_string(cached_form.a.num_bits()) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 260201 && i <= 260240) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H42",
                "provers.h:TwoWesolowskiProver:GetForm_h42_after_copy",
                "H42 micro window after GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"cached_valid\":" + (cached_form.check_valid(D) ? "true" : "false") +
                    ",\"cached_a_bits\":" + std::to_string(cached_form.a.num_bits()) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 264201 && i <= 264240) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H43",
                "provers.h:TwoWesolowskiProver:GetForm_h43_after_copy",
                "H43 micro window after GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"cached_valid\":" + (cached_form.check_valid(D) ? "true" : "false") +
                    ",\"cached_a_bits\":" + std::to_string(cached_form.a.num_bits()) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 250001 && i <= 250060) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H44",
                "provers.h:TwoWesolowskiProver:GetForm_h44_after_copy",
                "H44 micro window after GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"cached_valid\":" + (cached_form.check_valid(D) ? "true" : "false") +
                    ",\"cached_a_bits\":" + std::to_string(cached_form.a.num_bits()) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 260701 && i <= 260760) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H45",
                "provers.h:TwoWesolowskiProver:GetForm_h45_after_copy",
                "H45 micro window after GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"cached_valid\":" + (cached_form.check_valid(D) ? "true" : "false") +
                    ",\"cached_a_bits\":" + std::to_string(cached_form.a.num_bits()) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 250061 && i <= 250120) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H46",
                "provers.h:TwoWesolowskiProver:GetForm_h46_after_copy",
                "H46 micro window after GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"cached_valid\":" + (cached_form.check_valid(D) ? "true" : "false") +
                    ",\"cached_a_bits\":" + std::to_string(cached_form.a.num_bits()) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 250058 && i <= 250062) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H47",
                "provers.h:TwoWesolowskiProver:GetForm_h47_after_copy",
                "H47 tight boundary after GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"cached_valid\":" + (cached_form.check_valid(D) ? "true" : "false") +
                    ",\"cached_a_bits\":" + std::to_string(cached_form.a.num_bits()) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 250090 && i <= 250100) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H50",
                "provers.h:TwoWesolowskiProver:GetForm_h50_after_copy",
                "H50 boundary after GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"cached_valid\":" + (cached_form.check_valid(D) ? "true" : "false") +
                    ",\"cached_a_bits\":" + std::to_string(cached_form.a.num_bits()) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 250061 && i <= 250063) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H51",
                "provers.h:TwoWesolowskiProver:GetForm_h51_after_copy",
                "H51 boundary after GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"cached_valid\":" + (cached_form.check_valid(D) ? "true" : "false") +
                    ",\"cached_a_bits\":" + std::to_string(cached_form.a.num_bits()) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 250030 && i <= 250040) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H49",
                "provers.h:TwoWesolowskiProver:GetForm_h49_after_copy",
                "H49 boundary after GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"cached_valid\":" + (cached_form.check_valid(D) ? "true" : "false") +
                    ",\"cached_a_bits\":" + std::to_string(cached_form.a.num_bits()) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 265001 && i <= 265200) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H30",
                "provers.h:TwoWesolowskiProver:GetForm_micro_after_copy",
                "Micro window after GetFormCopy",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) + "}");
            // #endregion
        }
        if (done_iterations >= 10000000 && i >= 265001 && i <= 265200) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H32",
                "provers.h:TwoWesolowskiProver:GetForm_micro_before_return",
                "Micro window before returning cached form pointer",
                std::string("{\"done_iterations\":") + std::to_string(done_iterations) +
                    ",\"i\":" + std::to_string(i) +
                    ",\"power\":" + std::to_string(power) +
                    ",\"cached_valid\":" + (cached_form.check_valid(D) ? "true" : "false") +
                    ",\"cached_a_bits\":" + std::to_string(cached_form.a.num_bits()) + "}");
            // #endregion
        }
        return &cached_form;
    }

    void stop() {
    }

    bool PerformExtraStep() {
        return !stop_signal;
    }

    void OnFinish() {
        is_finished = true;
    }

  private:
    TwoWesolowskiCallback* weso;
    std::atomic<bool>& stop_signal;
    uint64_t done_iterations;
    form cached_form;
};

extern bool new_event;
extern std::mutex new_event_mutex;
extern std::condition_variable new_event_cv;

class InterruptableProver: public Prover {
  public:
    InterruptableProver(Segment segm, integer D, FastAlgorithmCallback* weso) : Prover(segm, D) {
        this->weso = weso;
        this->done_iterations = segm.start;
        this->bucket = segm.GetSegmentBucket();
        if (segm.length <= (1 << 16))
            k = 10;
        else
            k = 12;
        if (segm.length <= (1 << 18))
            l = 1;
        else
            l = (segm.length >> 18);
        is_paused = false;
        is_fully_finished = false;
        joined = false;
    }

    ~InterruptableProver() {
        if (!joined) {
            th->join();
        }
        delete(th);
    }

    form* GetForm(uint64_t i) {
        return weso->GetForm(done_iterations + i * k * l, bucket);
    }

    void start() {
        th = new std::thread([=] { GenerateProof(); });
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lk(m);
            is_finished = true;
            is_fully_finished = true;
            if (is_paused) {
                is_paused = false;
            }
        }
        cv.notify_one();
        th->join();
        joined = true;
    }

    bool PerformExtraStep() {
        if (is_finished) {
            return false;
        }
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] {
            return !is_paused;
        });
        return true;
    }

    void pause() {
        std::lock_guard<std::mutex> lk(m);
        is_paused = true;
    }

    void resume() {
        {
            std::lock_guard<std::mutex> lk(m);
            is_paused = false;
        }
        cv.notify_one();
    }

    bool IsRunning() {
        return !is_paused;
    }

    bool IsFullyFinished() {
        return is_fully_finished;
    }

    void OnFinish() {
        is_finished = true;
        if (!is_fully_finished) {
            // Notify event loop a proving thread is free.
            {
                std::lock_guard<std::mutex> lk(new_event_mutex);
                new_event = true;
            }
            new_event_cv.notify_one();
            is_fully_finished = true;
        }
    }

  private:
    std::thread* th;
    FastAlgorithmCallback* weso;
    std::condition_variable cv;
    std::mutex m;
    bool is_paused;
    std::atomic<bool> is_fully_finished;
    bool joined;
    uint64_t done_iterations;
    int bucket;
};

#endif // PROVERS_H
