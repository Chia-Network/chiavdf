#ifndef CALLBACK_H
#define CALLBACK_H

#include "util.h"
#include "nudupl_listener.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>

// Applies to n-weso.
const int kWindowSize = 20;

// Applies only to 2-weso.
const int kMaxItersAllowed = 8e8;
const int kSwitchIters = 91000000;

inline const char* CallbackAgentDebugLogPath() {
    const char* env_path = std::getenv("CHIAVDF_AGENT_DEBUG_LOG");
    if (env_path != nullptr && env_path[0] != '\0') {
        return env_path;
    }
    return "/Users/hoffmang/src/chiavdf/.cursor/debug.log";
}

inline bool CallbackAgentDebugShouldMirror(const char* hypothesis_id) {
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
           std::strcmp(hypothesis_id, "H39") == 0 ||
           std::strcmp(hypothesis_id, "H40") == 0 ||
           std::strcmp(hypothesis_id, "H41") == 0 ||
           std::strcmp(hypothesis_id, "H42") == 0 ||
           std::strcmp(hypothesis_id, "H43") == 0 ||
           std::strcmp(hypothesis_id, "H44") == 0 ||
           std::strcmp(hypothesis_id, "H45") == 0 ||
           std::strcmp(hypothesis_id, "H46") == 0 ||
           std::strcmp(hypothesis_id, "H47") == 0 ||
           std::strcmp(hypothesis_id, "H48") == 0 ||
           std::strcmp(hypothesis_id, "H49") == 0 ||
           std::strcmp(hypothesis_id, "H50") == 0 ||
           std::strcmp(hypothesis_id, "H51") == 0 ||
           std::strcmp(hypothesis_id, "H53") == 0 ||
           std::strcmp(hypothesis_id, "H54") == 0 ||
           std::strcmp(hypothesis_id, "H55") == 0;
}

inline void CallbackAgentDebugLog(const char* run_id, const char* hypothesis_id, const char* location, const char* message, const std::string& data_json) {
    static std::atomic<uint64_t> seq{0};
    std::ofstream out(CallbackAgentDebugLogPath(), std::ios::app);
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
    if (CallbackAgentDebugShouldMirror(hypothesis_id)) {
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

class WesolowskiCallback :public INUDUPLListener {
public:
    WesolowskiCallback(integer& D) {
        // #region agent log
        CallbackAgentDebugLog("pre-fix", "H11", "callback.h:WesolowskiCallback:ctor_enter", "Entering WesolowskiCallback constructor", "{}");
        // #endregion
        vdfo = new vdf_original();
        // #region agent log
        CallbackAgentDebugLog("pre-fix", "H11", "callback.h:WesolowskiCallback:after_vdfo_new", "Allocated vdf_original", "{}");
        // #endregion
        reducer = new PulmarkReducer();
        // #region agent log
        CallbackAgentDebugLog("pre-fix", "H11", "callback.h:WesolowskiCallback:after_reducer_new", "Allocated PulmarkReducer", "{}");
        // #endregion
        this->D = D;
        this->L = root(-D, 4);
        // #region agent log
        CallbackAgentDebugLog("pre-fix", "H11", "callback.h:WesolowskiCallback:ctor_exit", "Leaving WesolowskiCallback constructor", "{}");
        // #endregion
    }

    virtual ~WesolowskiCallback() {
        delete(vdfo);
        delete(reducer);
    }

    void reduce(form& inf) {
        reducer->reduce(inf);
    }

    void SetForm(int type, void *data, form* mulf, bool reduced = true) {
        switch(type) {
            case NL_SQUARESTATE:
            {
#if (defined(ARCH_X86) || defined(ARCH_X64)) && !defined(CHIA_DISABLE_ASM)
                //cout << "NL_SQUARESTATE" << endl;
                uint64 res;

                square_state_type *square_state=(square_state_type *)data;

                if(!square_state->assign(mulf->a, mulf->b, mulf->c, res))
                    cout << "square_state->assign failed" << endl;
#else
                // Phased pipeline is x86/x64-only.
                (void)data;
                cout << "NL_SQUARESTATE unsupported on this architecture" << endl;
#endif
                break;
            }
            case NL_FORM:
            {
                //cout << "NL_FORM" << endl;

                vdf_original::form *f=(vdf_original::form *)data;

                mpz_set(mulf->a.impl, f->a);
                mpz_set(mulf->b.impl, f->b);
                mpz_set(mulf->c.impl, f->c);
                break;
            }
            default:
                cout << "Unknown case" << endl;
        }
        if (reduced) {
            reduce(*mulf);
        }
    }

    virtual void OnIteration(int type, void *data, uint64_t iteration) = 0;

    std::unique_ptr<form[]> forms;
    size_t forms_capacity = 0;
    std::atomic<int64_t> iterations{0};
    integer D;
    integer L;
    PulmarkReducer* reducer;
    vdf_original* vdfo;
};

class OneWesolowskiCallback: public WesolowskiCallback {
  public:
    OneWesolowskiCallback(integer& D, form& f, uint64_t wanted_iter) : WesolowskiCallback(D) {
        uint32_t k, l;
        this->wanted_iter = wanted_iter;
        if (wanted_iter >= (1 << 16)) {
            ApproximateParameters(wanted_iter, l, k);
        } else {
            k = 10;
            l = 1;
        }
        kl = k * l;
        uint64_t space_needed = wanted_iter / (k * l) + 100;
        forms_capacity = static_cast<size_t>(space_needed);
        forms.reset(new form[space_needed]);
        forms[0] = f;
    }

    void OnIteration(int type, void *data, uint64_t iteration) {
        iteration++;
        if (iteration > wanted_iter)
            return ;

        if (iteration % kl == 0) {
            uint64_t pos = iteration / kl;
            form* mulf = &forms[pos];
            SetForm(type, data, mulf);
        }
        if (iteration == wanted_iter) {
            SetForm(type, data, &result);
        }
    }

    uint64_t wanted_iter;
    uint32_t kl;
    form result;
};

class TwoWesolowskiCallback: public WesolowskiCallback {
  public:
    TwoWesolowskiCallback(integer& D, const form& f) : WesolowskiCallback(D) {
        // #region agent log
        CallbackAgentDebugLog("pre-fix", "H12", "callback.h:TwoWesolowskiCallback:ctor_enter", "Entering TwoWesolowskiCallback constructor", "{}");
        // #endregion
        const uint64_t early_points = static_cast<uint64_t>(kSwitchIters) / 10;
        const uint64_t late_points =
            (static_cast<uint64_t>(kMaxItersAllowed) - static_cast<uint64_t>(kSwitchIters)) / 100;
        const size_t space_needed = static_cast<size_t>(early_points + late_points);
        forms_capacity = space_needed;
        // #region agent log
        CallbackAgentDebugLog(
            "pre-fix",
            "H12",
            "callback.h:TwoWesolowskiCallback:before_forms_alloc",
            "Allocating forms array",
            std::string("{\"space_needed\":") + std::to_string(space_needed) +
                ",\"max_iters_fixed\":" + std::to_string(kMaxItersAllowed) + "}");
        // #endregion
        forms.reset(new form[space_needed]);
        // #region agent log
        CallbackAgentDebugLog("pre-fix", "H12", "callback.h:TwoWesolowskiCallback:after_forms_alloc", "Allocated forms array", "{}");
        // #endregion
        // #region agent log
        CallbackAgentDebugLog("pre-fix", "H13", "callback.h:TwoWesolowskiCallback:before_seed_form_assign", "Assigning initial form to forms[0]", "{}");
        // #endregion
        // #region agent log
        CallbackAgentDebugLog(
            "post-fix",
            "H54",
            "callback.h:TwoWesolowskiCallback:seed_h54_stage0_pre_source_reads",
            "H54 before reading source num_bits",
            "{}");
        // #endregion
        const int source_a_size = f.a.impl[0]._mp_size;
        const int source_a_alloc = f.a.impl[0]._mp_alloc;
        const uint64_t source_a_ptr = static_cast<uint64_t>(
            reinterpret_cast<uintptr_t>(f.a.impl[0]._mp_d));
        // #region agent log
        CallbackAgentDebugLog(
            "post-fix",
            "H55",
            "callback.h:TwoWesolowskiCallback:seed_h55_stage0_a_raw",
            "H55 raw a metadata before num_bits",
            std::string("{\"a_size\":") + std::to_string(source_a_size) +
                ",\"a_alloc\":" + std::to_string(source_a_alloc) +
                ",\"a_ptr\":" + std::to_string(source_a_ptr) + "}");
        // #endregion
        const uint64_t source_a_bits = static_cast<uint64_t>(f.a.num_bits());
        // #region agent log
        CallbackAgentDebugLog(
            "post-fix",
            "H54",
            "callback.h:TwoWesolowskiCallback:seed_h54_stage1_after_a_bits",
            "H54 after reading source a bits",
            std::string("{\"source_a_bits\":") + std::to_string(source_a_bits) + "}");
        // #endregion
        const uint64_t source_b_bits = static_cast<uint64_t>(f.b.num_bits());
        // #region agent log
        CallbackAgentDebugLog(
            "post-fix",
            "H54",
            "callback.h:TwoWesolowskiCallback:seed_h54_stage2_after_b_bits",
            "H54 after reading source b bits",
            std::string("{\"source_b_bits\":") + std::to_string(source_b_bits) + "}");
        // #endregion
        const uint64_t source_c_bits = static_cast<uint64_t>(f.c.num_bits());
        // #region agent log
        CallbackAgentDebugLog(
            "post-fix",
            "H54",
            "callback.h:TwoWesolowskiCallback:seed_h54_stage3_after_c_bits",
            "H54 after reading source c bits",
            std::string("{\"source_c_bits\":") + std::to_string(source_c_bits) + "}");
        // #endregion
        // #region agent log
        CallbackAgentDebugLog(
            "post-fix",
            "H53",
            "callback.h:TwoWesolowskiCallback:seed_stage0_source_state",
            "H53 source form state before seed assignment",
            std::string("{\"source_a_bits\":") + std::to_string(source_a_bits) +
                ",\"source_b_bits\":" + std::to_string(source_b_bits) +
                ",\"source_c_bits\":" + std::to_string(source_c_bits) + "}");
        // #endregion
        // #region agent log
        CallbackAgentDebugLog(
            "post-fix",
            "H53",
            "callback.h:TwoWesolowskiCallback:seed_stage1_dest_slot",
            "H53 destination slot prepared for seed assignment",
            std::string("{\"forms_capacity\":") + std::to_string(forms_capacity) + "}");
        // #endregion
        forms[0] = f;
        // #region agent log
        CallbackAgentDebugLog(
            "post-fix",
            "H53",
            "callback.h:TwoWesolowskiCallback:seed_stage2_after_assign",
            "H53 seed assignment completed",
            std::string("{\"stored_valid\":") + (forms[0].check_valid(D) ? "true" : "false") +
                ",\"stored_a_bits\":" + std::to_string(forms[0].a.num_bits()) + "}");
        // #endregion
        // #region agent log
        CallbackAgentDebugLog("pre-fix", "H13", "callback.h:TwoWesolowskiCallback:after_seed_form_assign", "Assigned initial form to forms[0]", "{}");
        // #endregion
        kl = 10;
        switch_iters = -1;
        // #region agent log
        CallbackAgentDebugLog("pre-fix", "H12", "callback.h:TwoWesolowskiCallback:ctor_exit", "Leaving TwoWesolowskiCallback constructor", "{}");
        // #endregion
    }

    void IncreaseConstants(uint64_t num_iters) {
        kl = 100;
        switch_iters = num_iters;
        switch_index = num_iters / 10;
    }

    int GetPosition(uint64_t power) {
        if (switch_iters == -1 || power < switch_iters) {
            return power / 10;
        } else {
            return (switch_index + (power - switch_iters) / 100);
        }
    }

    form GetFormCopy(uint64_t power) {
        if (power >= 12500590 && power <= 12500610) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H47",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h47_stage0_pre_lock",
                "H47 stage0 pre-lock",
                std::string("{\"power\":") + std::to_string(power) + "}");
            // #endregion
        }
        if (power >= 12602260 && power <= 12602280) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H48",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h48_stage0_pre_lock",
                "H48 stage0 pre-lock",
                std::string("{\"power\":") + std::to_string(power) + "}");
            // #endregion
        }
        if (power >= 12500360 && power <= 12500390) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H49",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h49_stage0_pre_lock",
                "H49 stage0 pre-lock",
                std::string("{\"power\":") + std::to_string(power) + "}");
            // #endregion
        }
        if (power >= 12500920 && power <= 12500960) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H50",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h50_stage0_pre_lock",
                "H50 stage0 pre-lock",
                std::string("{\"power\":") + std::to_string(power) + "}");
            // #endregion
        }
        if (power >= 12500610 && power <= 12500630) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H51",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h51_stage0_pre_lock",
                "H51 stage0 pre-lock",
                std::string("{\"power\":") + std::to_string(power) + "}");
            // #endregion
        }
        std::lock_guard<std::mutex> lk(forms_mutex);
        if (power >= 12500590 && power <= 12500610) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H47",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h47_stage1_post_lock",
                "H47 stage1 post-lock",
                std::string("{\"power\":") + std::to_string(power) + "}");
            // #endregion
        }
        if (power >= 12602260 && power <= 12602280) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H48",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h48_stage1_post_lock",
                "H48 stage1 post-lock",
                std::string("{\"power\":") + std::to_string(power) + "}");
            // #endregion
        }
        if (power >= 12500360 && power <= 12500390) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H49",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h49_stage1_post_lock",
                "H49 stage1 post-lock",
                std::string("{\"power\":") + std::to_string(power) + "}");
            // #endregion
        }
        if (power >= 12500920 && power <= 12500960) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H50",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h50_stage1_post_lock",
                "H50 stage1 post-lock",
                std::string("{\"power\":") + std::to_string(power) + "}");
            // #endregion
        }
        if (power >= 12500610 && power <= 12500630) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H51",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h51_stage1_post_lock",
                "H51 stage1 post-lock",
                std::string("{\"power\":") + std::to_string(power) + "}");
            // #endregion
        }
        const int pos = GetPosition(power);
        if (power >= 12500590 && power <= 12500610) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H47",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h47_stage2_after_getposition",
                "H47 stage2 after GetPosition",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"forms_capacity\":" + std::to_string(forms_capacity) + "}");
            // #endregion
        }
        if (power >= 12602260 && power <= 12602280) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H48",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h48_stage2_after_getposition",
                "H48 stage2 after GetPosition",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"forms_capacity\":" + std::to_string(forms_capacity) + "}");
            // #endregion
        }
        if (power >= 12500360 && power <= 12500390) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H49",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h49_stage2_after_getposition",
                "H49 stage2 after GetPosition",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"forms_capacity\":" + std::to_string(forms_capacity) + "}");
            // #endregion
        }
        if (power >= 12500920 && power <= 12500960) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H50",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h50_stage2_after_getposition",
                "H50 stage2 after GetPosition",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"forms_capacity\":" + std::to_string(forms_capacity) + "}");
            // #endregion
        }
        if (power >= 12500610 && power <= 12500630) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H51",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h51_stage2_after_getposition",
                "H51 stage2 after GetPosition",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"forms_capacity\":" + std::to_string(forms_capacity) + "}");
            // #endregion
        }
        if (pos < 0 || static_cast<size_t>(pos) >= forms_capacity) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H16",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_oob",
                "GetFormCopy out of bounds",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"forms_capacity\":" + std::to_string(forms_capacity) +
                    ",\"switch_iters\":" + std::to_string(switch_iters) +
                    ",\"switch_index\":" + std::to_string(switch_index) + "}");
            // #endregion
            throw std::runtime_error("TwoWesolowskiCallback::GetFormCopy out of bounds");
        }
        if (power >= 12500590 && power <= 12500610) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H47",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h47_stage3_before_return_copy",
                "H47 stage3 before return copy",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) + "}");
            // #endregion
        }
        if (power >= 12500360 && power <= 12500390) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H49",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h49_stage3_before_source_deref",
                "H49 stage3 before source dereference",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) + "}");
            // #endregion
        }
        if (power >= 12500920 && power <= 12500960) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H50",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h50_stage3_before_source_deref",
                "H50 stage3 before source dereference",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) + "}");
            // #endregion
        }
        if (power >= 12500610 && power <= 12500630) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H51",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h51_stage3_before_source_deref",
                "H51 stage3 before source dereference",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) + "}");
            // #endregion
        }
        if (power >= 12650010 && power <= 12652000) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H32",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_micro_source_state",
                "Micro window source form state before copy",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"source_valid\":" + (forms[static_cast<size_t>(pos)].check_valid(D) ? "true" : "false") +
                    ",\"source_a_bits\":" + std::to_string(forms[static_cast<size_t>(pos)].a.num_bits()) + "}");
            // #endregion
        }
        if (power >= 12644200 && power <= 12644450) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H34",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_focused_source_state",
                "Focused window source form state before copy",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"source_valid\":" + (forms[static_cast<size_t>(pos)].check_valid(D) ? "true" : "false") +
                    ",\"source_a_bits\":" + std::to_string(forms[static_cast<size_t>(pos)].a.num_bits()) + "}");
            // #endregion
        }
        if (power >= 12647010 && power <= 12647600 &&
            !(power >= 12647080 && power <= 12647220)) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H35",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_post700_source_state",
                "Post-264700 focused source form state before copy",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"source_valid\":" + (forms[static_cast<size_t>(pos)].check_valid(D) ? "true" : "false") +
                    ",\"source_a_bits\":" + std::to_string(forms[static_cast<size_t>(pos)].a.num_bits()) + "}");
            // #endregion
        }
        if (power >= 12647080 && power <= 12647220) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H39",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_stage0_before_source_deref",
                "H39 stage0 before source dereference",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) + "}");
            // #endregion
            form& source = forms[static_cast<size_t>(pos)];
            const bool source_valid = source.check_valid(D);
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H39",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_stage1_after_check_valid",
                "H39 stage1 after check_valid",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"source_valid\":" + (source_valid ? "true" : "false") + "}");
            // #endregion
            const uint64_t source_a_bits = static_cast<uint64_t>(source.a.num_bits());
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H39",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_stage2_after_num_bits",
                "H39 stage2 after source num_bits",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"source_a_bits\":" + std::to_string(source_a_bits) + "}");
            // #endregion
        }
        if (power >= 12699800 && power <= 12700600) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H36",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_270k_source_state",
                "270k window source form state before copy",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"source_valid\":" + (forms[static_cast<size_t>(pos)].check_valid(D) ? "true" : "false") +
                    ",\"source_a_bits\":" + std::to_string(forms[static_cast<size_t>(pos)].a.num_bits()) + "}");
            // #endregion
        }
        if (power >= 12643180 && power <= 12643240) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H38",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_crash_window_source_state",
                "Crash-window source form state before copy",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"source_valid\":" + (forms[static_cast<size_t>(pos)].check_valid(D) ? "true" : "false") +
                    ",\"source_a_bits\":" + std::to_string(forms[static_cast<size_t>(pos)].a.num_bits()) + "}");
            // #endregion
        }
        if (power >= 12644100 && power <= 12644300) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H40",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h40_source_state",
                "H40 window source form state before copy",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"source_valid\":" + (forms[static_cast<size_t>(pos)].check_valid(D) ? "true" : "false") +
                    ",\"source_a_bits\":" + std::to_string(forms[static_cast<size_t>(pos)].a.num_bits()) + "}");
            // #endregion
        }
        if (power >= 12635000 && power <= 12635200) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H41",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h41_stage0_before_source_deref",
                "H41 stage0 before source dereference",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) + "}");
            // #endregion
            form& source = forms[static_cast<size_t>(pos)];
            const bool source_valid = source.check_valid(D);
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H41",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h41_stage1_after_check_valid",
                "H41 stage1 after check_valid",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"source_valid\":" + (source_valid ? "true" : "false") + "}");
            // #endregion
            const uint64_t source_a_bits = static_cast<uint64_t>(source.a.num_bits());
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H41",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h41_stage2_after_num_bits",
                "H41 stage2 after source num_bits",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"source_a_bits\":" + std::to_string(source_a_bits) + "}");
            // #endregion
        }
        if (power >= 12602010 && power <= 12602400) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H42",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h42_stage0_before_source_deref",
                "H42 stage0 before source dereference",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) + "}");
            // #endregion
            form& source = forms[static_cast<size_t>(pos)];
            const bool source_valid = source.check_valid(D);
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H42",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h42_stage1_after_check_valid",
                "H42 stage1 after check_valid",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"source_valid\":" + (source_valid ? "true" : "false") + "}");
            // #endregion
            const uint64_t source_a_bits = static_cast<uint64_t>(source.a.num_bits());
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H42",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h42_stage2_after_num_bits",
                "H42 stage2 after source num_bits",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"source_a_bits\":" + std::to_string(source_a_bits) + "}");
            // #endregion
        }
        if (power >= 12642010 && power <= 12642400) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H43",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h43_stage0_before_source_deref",
                "H43 stage0 before source dereference",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) + "}");
            // #endregion
            form& source = forms[static_cast<size_t>(pos)];
            const uint64_t source_a_bits = static_cast<uint64_t>(source.a.num_bits());
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H43",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h43_stage1_after_num_bits",
                "H43 stage1 after source num_bits",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"source_a_bits\":" + std::to_string(source_a_bits) + "}");
            // #endregion
        }
        if (power >= 12500010 && power <= 12500600) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H44",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h44_stage0_before_source_deref",
                "H44 stage0 before source dereference",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) + "}");
            // #endregion
            form& source = forms[static_cast<size_t>(pos)];
            const uint64_t source_a_bits = static_cast<uint64_t>(source.a.num_bits());
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H44",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h44_stage1_after_num_bits",
                "H44 stage1 after source num_bits",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"source_a_bits\":" + std::to_string(source_a_bits) + "}");
            // #endregion
        }
        if (power >= 12607010 && power <= 12607600) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H45",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h45_stage0_before_source_deref",
                "H45 stage0 before source dereference",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) + "}");
            // #endregion
            form& source = forms[static_cast<size_t>(pos)];
            const uint64_t source_a_bits = static_cast<uint64_t>(source.a.num_bits());
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H45",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h45_stage1_after_num_bits",
                "H45 stage1 after source num_bits",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"source_a_bits\":" + std::to_string(source_a_bits) + "}");
            // #endregion
        }
        if (power >= 12500610 && power <= 12501200) {
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H46",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h46_stage0_before_source_deref",
                "H46 stage0 before source dereference",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) + "}");
            // #endregion
            form& source = forms[static_cast<size_t>(pos)];
            const uint64_t source_a_bits = static_cast<uint64_t>(source.a.num_bits());
            // #region agent log
            CallbackAgentDebugLog(
                "post-fix",
                "H46",
                "callback.h:TwoWesolowskiCallback:GetFormCopy_h46_stage1_after_num_bits",
                "H46 stage1 after source num_bits",
                std::string("{\"power\":") + std::to_string(power) +
                    ",\"pos\":" + std::to_string(pos) +
                    ",\"source_a_bits\":" + std::to_string(source_a_bits) + "}");
            // #endregion
        }
        return forms[static_cast<size_t>(pos)];
    }

    bool LargeConstants() {
        return kl == 100;
    }

    void OnIteration(int type, void *data, uint64_t iteration) {
        iteration++;
        if (iteration % kl == 0) {
            const int pos = GetPosition(iteration);
            if (iteration >= 12647080 && iteration <= 12647220) {
                // #region agent log
                CallbackAgentDebugLog(
                    "post-fix",
                    "H39",
                    "callback.h:TwoWesolowskiCallback:OnIteration_h39_before_setform",
                    "H39 producer before SetForm",
                    std::string("{\"iteration\":") + std::to_string(iteration) +
                        ",\"pos\":" + std::to_string(pos) +
                        ",\"forms_capacity\":" + std::to_string(forms_capacity) + "}");
                // #endregion
            }
            if (iteration % 1000000 == 0) {
                // #region agent log
                CallbackAgentDebugLog(
                    "post-fix",
                    "H19",
                    "callback.h:TwoWesolowskiCallback:OnIteration_progress",
                    "OnIteration progress marker",
                    std::string("{\"iteration\":") + std::to_string(iteration) +
                        ",\"pos\":" + std::to_string(pos) +
                        ",\"forms_capacity\":" + std::to_string(forms_capacity) +
                        ",\"kl\":" + std::to_string(kl) +
                        ",\"switch_iters\":" + std::to_string(switch_iters) + "}");
                // #endregion
            }
            if (pos < 0 || static_cast<size_t>(pos) >= forms_capacity) {
                // #region agent log
                CallbackAgentDebugLog(
                    "post-fix",
                    "H19",
                    "callback.h:TwoWesolowskiCallback:OnIteration_oob",
                    "OnIteration write position out of bounds",
                    std::string("{\"iteration\":") + std::to_string(iteration) +
                        ",\"pos\":" + std::to_string(pos) +
                        ",\"forms_capacity\":" + std::to_string(forms_capacity) +
                        ",\"kl\":" + std::to_string(kl) +
                        ",\"switch_iters\":" + std::to_string(switch_iters) +
                        ",\"switch_index\":" + std::to_string(switch_index) + "}");
                // #endregion
                throw std::runtime_error("TwoWesolowskiCallback::OnIteration out of bounds");
            }
            std::lock_guard<std::mutex> lk(forms_mutex);
            form* mulf = &forms[static_cast<size_t>(pos)];
            SetForm(type, data, mulf);
            if (iteration >= 12647080 && iteration <= 12647220) {
                // #region agent log
                CallbackAgentDebugLog(
                    "post-fix",
                    "H39",
                    "callback.h:TwoWesolowskiCallback:OnIteration_h39_after_setform",
                    "H39 producer after SetForm",
                    std::string("{\"iteration\":") + std::to_string(iteration) +
                        ",\"pos\":" + std::to_string(pos) +
                        ",\"stored_valid\":" + (mulf->check_valid(D) ? "true" : "false") +
                        ",\"stored_a_bits\":" + std::to_string(mulf->a.num_bits()) + "}");
                // #endregion
            }
        }
    }

  private:
    uint64_t switch_index;
    int64_t switch_iters;
    uint32_t kl;
    std::mutex forms_mutex;
};

class FastAlgorithmCallback : public WesolowskiCallback {
  public:
    FastAlgorithmCallback(int segments, integer& D, form f, bool multi_proc_machine) : WesolowskiCallback(D) {
        buckets_begin.push_back(0);
        buckets_begin.push_back(bucket_size1 * window_size);
        this->segments = segments;
        this->multi_proc_machine = multi_proc_machine;
        for (int i = 0; i < segments - 2; i++) {
            buckets_begin.push_back(buckets_begin[buckets_begin.size() - 1] + bucket_size2 * window_size);
        }
        int space_needed = window_size * (bucket_size1 + bucket_size2 * (segments - 1));
        forms.reset(new form[space_needed]);
        checkpoints.reset(new form[1 << 18]);

        y_ret = f;
        for (int i = 0; i < segments; i++)
            forms[buckets_begin[i]] = f;
        checkpoints[0] = f;
    }

    int GetPosition(uint64_t exponent, int bucket) {
        uint64_t power_2 = 1LL << (16 + 2 * bucket);
        int position = buckets_begin[bucket];
        int size = (bucket == 0) ? bucket_size1 : bucket_size2;
        int kl = (bucket == 0) ? 10 : (12 * (power_2 >> 18));
        position += ((exponent / power_2) % window_size) * size;
        position += (exponent % power_2) / kl;
        return position;
    }

    form *GetForm(uint64_t exponent, int bucket) {
        uint64_t pos = GetPosition(exponent, bucket);
        return &(forms[pos]);
    }

    // We need to store:
    // 2^16 * k + 10 * l
    // 2^(18 + 2*m) * k + 12 * 2^(2*m) * l
    void OnIteration(int type, void *data, uint64_t iteration) {
        iteration++;
        if (multi_proc_machine) {
            if (iteration % (1 << 15) == 0) {
                SetForm(type, data, &y_ret);
            }
        } else {
            // If 'multi_proc_machine' is 0, we store the intermediates
            // right away.
            for (int i = 0; i < segments; i++) {
                uint64_t power_2 = 1LL << (16 + 2LL * i);
                int kl = (i == 0) ? 10 : (12 * (power_2 >> 18));
                if ((iteration % power_2) % kl == 0) {
                    form* mulf = GetForm(iteration, i);
                    SetForm(type, data, mulf);
                }
            }
        }

        if (iteration % (1 << 16) == 0) {
            form* mulf = (&checkpoints[(iteration / (1 << 16))]);
            SetForm(type, data, mulf);
        }
    }

    std::vector<int> buckets_begin;
    std::unique_ptr<form[]> checkpoints;
    form y_ret;
    int segments;
    // The intermediate values size of a 2^16 segment.
    const int bucket_size1 = 6554;
    // The intermediate values size of a >= 2^18 segment.
    const int bucket_size2 = 21846;
    // Assume provers won't be left behind by more than this # of segments.
    const int window_size = kWindowSize;
    bool multi_proc_machine;
};

#endif // CALLBACK_H
