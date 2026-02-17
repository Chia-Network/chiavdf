namespace asm_code {


string vpermq_mask(array<int, 4> lanes) {
    int res=0;
    for (int x=0;x<4;++x) {
        int lane=lanes[x];
        assert(lane>=0 && lane<4);
        res|=lane << (2*x);
    }
    return to_hex(res);
}

string vpblendd_mask_4(array<int, 4> lanes) {
    int res=0;
    for (int x=0;x<4;++x) {
        int lane=lanes[x];
        assert(lane>=0 && lane<2);
        res|=((lane==1)? 3 : 0) << (2*x);
    }
    return to_hex(res);
}

string vpblendd_mask_8(array<int, 8> lanes) {
    int res=0;
    for (int x=0;x<8;++x) {
        int lane=lanes[x];
        assert(lane>=0 && lane<2);
        res|=((lane==1)? 1 : 0) << x;
    }
    return to_hex(res);
}

struct asm_function {
    string name;

    //this excludes the argument regs (if any). can add them after they are done being used
    reg_alloc regs;

    vector<reg_scalar> args;

    vector<reg_scalar> pop_regs;
#ifdef CHIA_WINDOWS
    const vector<reg_scalar> all_save_regs={reg_rbp, reg_rbx, reg_rsi, reg_rdi, reg_r12, reg_r13, reg_r14, reg_r15};
#else
    const vector<reg_scalar> all_save_regs={reg_rbp, reg_rbx, reg_r12, reg_r13, reg_r14, reg_r15};
#endif
#ifdef CHIA_WINDOWS
    const vector<reg_scalar> all_arg_regs={reg_rcx, reg_rdx, reg_r8, reg_r9, reg_r10, reg_r11};
#else
    const vector<reg_scalar> all_arg_regs={reg_rdi, reg_rsi, reg_rdx, reg_rcx, reg_r8, reg_r9};
#endif
    const reg_scalar return_reg=reg_rax;

    bool d_align_stack=true;
    bool d_return_error_code=true;
    bool d_windows_stack_args=true;

    //the scratch area ends at RSP (i.e. the last byte is at address RSP-1)
    //RSP is 64-byte aligned
    //RSP must be preserved but all other registers can be changed
    //
    //non-Windows: args are in RDI, RSI, RDX, RCX, R8, R9
    //Windows: args 1-4 are in RCX, RDX, R8, R9; args 5+ are ABI stack args
    //that may be loaded into R10/R11 by the prologue for convenience.
    //each argument is up to 8 bytes
    asm_function(
        string t_name,
        int num_args=0,
        int num_regs=15,
        bool align_stack=true,
        bool return_error_code=true,
        bool windows_stack_args=true
    ) {
        EXPAND_MACROS_SCOPE;

        d_align_stack=align_stack;
        d_return_error_code=return_error_code;
        d_windows_stack_args=windows_stack_args;

        static bool outputted_header=false;
        if (!outputted_header) {
            APPEND_M(str( ".intel_syntax noprefix" ));
            outputted_header=true;
        }

        name=t_name;

#ifdef CHIAOSX
        APPEND_M(str( ".global _asm_")+asmprefix+str("func_#", t_name ));
        APPEND_M(str( "_asm_")+asmprefix+str("func_#:", t_name ));
#else
        APPEND_M(str( ".global asm_")+asmprefix+str("func_#", t_name ));
        APPEND_M(str( "asm_")+asmprefix+str("func_#:", t_name ));
#endif

        assert(num_regs<=15);
        regs.init();

        for (int x=0;x<num_args;++x) {
            reg_scalar r=all_arg_regs.at(x);
            regs.get_scalar(r);
            args.push_back(r);
        }
#ifdef CHIA_WINDOWS
        // For ABI entry points, pull args 5+ from the caller stack per Win64 ABI.
        // Internal asm-to-asm calls may pass args 5/6 in R10/R11 and opt out.
        if (d_windows_stack_args && num_args > 4) {
            APPEND_M(str( "MOV R10, [RSP+0x28]" ));
        }
        if (d_windows_stack_args && num_args > 5) {
            APPEND_M(str( "MOV R11, [RSP+0x30]" ));
        }
#endif

        //takes 6 cycles max if nothing else to do
        int num_available_regs=15-all_save_regs.size();
        for (reg_scalar s : all_save_regs) {
            if (num_regs>num_available_regs) {
                APPEND_M(str( "PUSH #", s.name() ));
                pop_regs.push_back(s);
                ++num_available_regs;
            } else {
                regs.get_scalar(s);
            }
        }
        assert(num_available_regs==num_regs);

        if (align_stack) {
#ifdef CHIA_WINDOWS
            // Windows x64: reserve spill area + shadow space + xmm save area + align to 64.
            const int windows_saved_rsp_bytes = 8;
            const int windows_shadow_space_bytes = 0x20;
            const int windows_xmm_slot_bytes = enable_all_instructions ? 32 : 16;
            const int windows_xmm_save_bytes = 10 * windows_xmm_slot_bytes; // XMM6-15 (or YMM6-15 for AVX codegen)
            const int windows_frame_bytes =
                windows_saved_rsp_bytes + spill_bytes + windows_shadow_space_bytes + windows_xmm_save_bytes;
            APPEND_M(str( "MOV RAX, RSP" ));
            APPEND_M(str( "SUB RSP, #", to_hex(windows_frame_bytes + 63) ));
            APPEND_M(str( "AND RSP, -64" ));
            // Keep spill base (RSP+8) 64-byte aligned.
            APPEND_M(str( "SUB RSP, 8" ));
            APPEND_M(str( "MOV [RSP], RAX" ));
            const int windows_xmm_save_base = windows_saved_rsp_bytes + spill_bytes + windows_shadow_space_bytes;
            if (enable_all_instructions) {
                APPEND_M(str( "VMOVDQU [RSP+#], YMM6", to_hex(windows_xmm_save_base + 0 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "VMOVDQU [RSP+#], YMM7", to_hex(windows_xmm_save_base + 1 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "VMOVDQU [RSP+#], YMM8", to_hex(windows_xmm_save_base + 2 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "VMOVDQU [RSP+#], YMM9", to_hex(windows_xmm_save_base + 3 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "VMOVDQU [RSP+#], YMM10", to_hex(windows_xmm_save_base + 4 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "VMOVDQU [RSP+#], YMM11", to_hex(windows_xmm_save_base + 5 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "VMOVDQU [RSP+#], YMM12", to_hex(windows_xmm_save_base + 6 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "VMOVDQU [RSP+#], YMM13", to_hex(windows_xmm_save_base + 7 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "VMOVDQU [RSP+#], YMM14", to_hex(windows_xmm_save_base + 8 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "VMOVDQU [RSP+#], YMM15", to_hex(windows_xmm_save_base + 9 * windows_xmm_slot_bytes) ));
            } else {
                APPEND_M(str( "MOVDQU [RSP+#], XMM6", to_hex(windows_xmm_save_base + 0 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "MOVDQU [RSP+#], XMM7", to_hex(windows_xmm_save_base + 1 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "MOVDQU [RSP+#], XMM8", to_hex(windows_xmm_save_base + 2 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "MOVDQU [RSP+#], XMM9", to_hex(windows_xmm_save_base + 3 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "MOVDQU [RSP+#], XMM10", to_hex(windows_xmm_save_base + 4 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "MOVDQU [RSP+#], XMM11", to_hex(windows_xmm_save_base + 5 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "MOVDQU [RSP+#], XMM12", to_hex(windows_xmm_save_base + 6 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "MOVDQU [RSP+#], XMM13", to_hex(windows_xmm_save_base + 7 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "MOVDQU [RSP+#], XMM14", to_hex(windows_xmm_save_base + 8 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "MOVDQU [RSP+#], XMM15", to_hex(windows_xmm_save_base + 9 * windows_xmm_slot_bytes) ));
            }
#else
            // RSP'=RSP&(~63) ; this makes it 64-aligned and can only reduce its value
            // RSP''=RSP'-64 ; still 64-aligned but now there is at least 64 bytes of unused stuff
            // [RSP'']=RSP ; store old value in unused area
            APPEND_M(str( "MOV RAX, RSP" ));
            APPEND_M(str( "AND RSP, -64" )); //-64 equals ~63
            APPEND_M(str( "SUB RSP, 64" ));
            APPEND_M(str( "MOV [RSP], RAX" ));
#endif
        }
    }

    //the return value is the error code (0 if no error). it is put in RAX
    ~asm_function() {
        EXPAND_MACROS_SCOPE;

        if (d_return_error_code) {
            //default return value of 0
            APPEND_M(str( "MOV RAX, 0" ));
        }

        string end_label=m.alloc_label();
        APPEND_M(str( "#:", end_label ));

        //this takes 4 cycles including ret, if there is nothing else to do
        if (d_align_stack) {
#ifdef CHIA_WINDOWS
            // Windows ABI: restore XMM6-XMM15 (or YMM6-YMM15 for AVX codegen).
            const int windows_saved_rsp_bytes = 8;
            const int windows_shadow_space_bytes = 0x20;
            const int windows_xmm_slot_bytes = enable_all_instructions ? 32 : 16;
            const int windows_xmm_save_base = windows_saved_rsp_bytes + spill_bytes + windows_shadow_space_bytes;
            if (enable_all_instructions) {
                APPEND_M(str( "VMOVDQU YMM6, [RSP+#]", to_hex(windows_xmm_save_base + 0 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "VMOVDQU YMM7, [RSP+#]", to_hex(windows_xmm_save_base + 1 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "VMOVDQU YMM8, [RSP+#]", to_hex(windows_xmm_save_base + 2 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "VMOVDQU YMM9, [RSP+#]", to_hex(windows_xmm_save_base + 3 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "VMOVDQU YMM10, [RSP+#]", to_hex(windows_xmm_save_base + 4 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "VMOVDQU YMM11, [RSP+#]", to_hex(windows_xmm_save_base + 5 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "VMOVDQU YMM12, [RSP+#]", to_hex(windows_xmm_save_base + 6 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "VMOVDQU YMM13, [RSP+#]", to_hex(windows_xmm_save_base + 7 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "VMOVDQU YMM14, [RSP+#]", to_hex(windows_xmm_save_base + 8 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "VMOVDQU YMM15, [RSP+#]", to_hex(windows_xmm_save_base + 9 * windows_xmm_slot_bytes) ));
            } else {
                APPEND_M(str( "MOVDQU XMM6, [RSP+#]", to_hex(windows_xmm_save_base + 0 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "MOVDQU XMM7, [RSP+#]", to_hex(windows_xmm_save_base + 1 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "MOVDQU XMM8, [RSP+#]", to_hex(windows_xmm_save_base + 2 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "MOVDQU XMM9, [RSP+#]", to_hex(windows_xmm_save_base + 3 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "MOVDQU XMM10, [RSP+#]", to_hex(windows_xmm_save_base + 4 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "MOVDQU XMM11, [RSP+#]", to_hex(windows_xmm_save_base + 5 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "MOVDQU XMM12, [RSP+#]", to_hex(windows_xmm_save_base + 6 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "MOVDQU XMM13, [RSP+#]", to_hex(windows_xmm_save_base + 7 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "MOVDQU XMM14, [RSP+#]", to_hex(windows_xmm_save_base + 8 * windows_xmm_slot_bytes) ));
                APPEND_M(str( "MOVDQU XMM15, [RSP+#]", to_hex(windows_xmm_save_base + 9 * windows_xmm_slot_bytes) ));
            }
#endif
            APPEND_M(str( "MOV RSP, [RSP]" ));
        }
        for (int x=pop_regs.size()-1;x>=0;--x) {
            APPEND_M(str( "POP #", pop_regs[x].name() ));
        }
        APPEND_M(str( "RET" ));

        while (m.next_output_error_label_id<m.next_error_label_id) {
            //can't allocate any error labels if the error code isn't being returned
            assert(d_return_error_code);

            APPEND_M(asmprefix+str( "label_error_#:", m.next_output_error_label_id ));

            assert(m.next_output_error_label_id!=0);
            APPEND_M(str( "MOV RAX, #", to_hex(m.next_output_error_label_id) ));
            APPEND_M(str( "JMP #", end_label ));

            ++m.next_output_error_label_id;
        }
    }
};


}
