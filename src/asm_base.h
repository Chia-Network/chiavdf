#include <chrono>

#ifdef GENERATE_ASM_TRACKING_DATA
    const bool generate_asm_tracking_data=true;
#else
    const bool generate_asm_tracking_data=false;
#endif

namespace asm_code {

inline void agent_debug_log_ndjson_file(const char* hypothesis_id, const char* location, const char* message, const string& data_json) {
    // #region agent log
    std::ofstream agent_debug_log("/Users/hoffmang/src/chiavdf/.cursor/debug.log", std::ios::app);
    if (!agent_debug_log.is_open()) {
        return;
    }
    const auto timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    agent_debug_log
        << "{\"id\":\"log_" << timestamp_ms << "_" << hypothesis_id
        << "\",\"timestamp\":" << timestamp_ms
        << ",\"runId\":\"windows-link-debug\""
        << ",\"hypothesisId\":\"" << hypothesis_id << "\""
        << ",\"location\":\"" << location << "\""
        << ",\"message\":\"" << message << "\""
        << ",\"data\":" << data_json
        << "}\n";
    // #endregion
}


string track_asm(string comment, string jump_to = "") {
    if (!generate_asm_tracking_data) {
        return jump_to;
    }

    mark_vdf_test();

    static map<string, int> id_map;
    static int next_id=1;

    int& id=id_map[comment];
    if (id==0) {
        id=next_id;
        ++next_id;
    }

    assert(id>=1 && id<=num_asm_tracking_data);

    //
    //

    static bool init=false;
    if (!init) {
        APPEND_M(str( ".data" ));
        APPEND_M(str( ".balign 8" ));

        APPEND_M(str( "track_asm_rax: .quad 0" ));

        //APPEND_M(str( ".global asm_tracking_data" ));
        //APPEND_M(str( "asm_tracking_data:" ));
        //for (int x=0;x<num_asm_tracking_data;++x) {
            //APPEND_M(str( ".quad 0" ));
        //}

        //APPEND_M(str( ".global asm_tracking_data_comments" ));
        //APPEND_M(str( "asm_tracking_data_comments:" ));
        //for (int x=0;x<num_asm_tracking_data;++x) {
            //APPEND_M(str( ".quad 0" ));
        //}

        APPEND_M(str( ".text" ));

        init=true;
    }

    string comment_label=m.alloc_label();
    // #region agent log
    agent_debug_log_ndjson_file(
        "H2",
        "src/asm_base.h:track_asm:alloc_label",
        "allocated_comment_label_for_tracking",
        std::string("{\"comment_label\":\"") + comment_label +
            "\",\"asmprefix\":\"" + asmprefix +
            "\",\"starts_with_prefix\":" +
            ((comment_label.rfind("_" + asmprefix, 0) == 0) ? "1" : "0") + "}"
    );
    // #endregion
#if defined(CHIA_WINDOWS)
    // #region agent log
    agent_debug_log_ndjson_file(
        "H3",
        "src/asm_base.h:track_asm:section_select",
        "emitting_comment_string_in_windows_rdata",
        "{\"selected_section\":\".section .rdata,\\\"dr\\\"\"}"
    );
    // #endregion
    APPEND_M(str( ".section .rdata,\"dr\"" ));
#elif defined(CHIAOSX)
    APPEND_M(str( ".text " ));
#else
    APPEND_M(str( ".text 1" ));
#endif
    APPEND_M(str( "#:", comment_label ));
    APPEND_M(str( ".string \"#\"", comment ));
    APPEND_M(str( ".text" ));

    string skip_label;
    if (!jump_to.empty()) {
        skip_label=m.alloc_label();
        APPEND_M(str( "JMP #", skip_label ));
    }

    string c_label;

    if (!jump_to.empty()) {
        c_label=m.alloc_label();
        APPEND_M(str( "#:", c_label ));
    }

    assert(!enable_threads); //this code isn't atomic

    APPEND_M(str( "MOV [track_asm_rax], RAX" ));
    APPEND_M(str( "MOV RAX, [asm_tracking_data+#]", to_hex(8*(id-1)) ));
    APPEND_M(str( "LEA RAX, [RAX+1]" ));
    APPEND_M(str( "MOV [asm_tracking_data+#], RAX", to_hex(8*(id-1)) ));
#if defined(CHIAOSX) || defined(CHIA_WINDOWS)
    // #region agent log
    agent_debug_log_ndjson_file(
        "H4",
        "src/asm_base.h:track_asm:comment_pointer_emit",
        "about_to_emit_comment_label_pointer",
        std::string("{\"comment_label\":\"") + comment_label +
            "\",\"is_windows\":" +
#ifdef CHIA_WINDOWS
            "1"
#else
            "0"
#endif
            + ",\"operand_literal\":\"RIP+comment_label\"}"
    );
    // #endregion
    APPEND_M(str( "LEA RAX, [RIP+#] ", comment_label ));
#else
    APPEND_M(str( "MOV RAX, OFFSET FLAT:#", comment_label ));
#endif
    APPEND_M(str( "MOV [asm_tracking_data_comments+#], RAX", to_hex(8*(id-1)) ));
    APPEND_M(str( "MOV RAX, [track_asm_rax]" ));

    if (!jump_to.empty()) {
        APPEND_M(str( "JMP #", jump_to ));
        APPEND_M(str( "#:", skip_label ));
    }

    return c_label;
}

//16-byte aligned; value is in both lanes
string constant_address_uint64(uint64 value_bits_0, uint64 value_bits_1, bool use_brackets=true) {
    static map<pair<uint64, uint64>, string> constant_map;
    string& name=constant_map[make_pair(value_bits_0, value_bits_1)];

    if (name.empty()) {
        name=m.alloc_label();

#if defined(CHIA_WINDOWS)
        APPEND_M(str( ".section .rdata,\"dr\"" ));
#elif defined(CHIAOSX)
        APPEND_M(str( ".text " ));
#else
        APPEND_M(str( ".text 1" ));
#endif
        APPEND_M(str( ".balign 16" ));
        APPEND_M(str( "#:", name ));
        APPEND_M(str( ".quad #", to_hex(value_bits_0) )); //lane 0
        APPEND_M(str( ".quad #", to_hex(value_bits_1) )); //lane 1
        APPEND_M(str( ".text" ));
    }
#if defined(CHIAOSX) || defined(CHIA_WINDOWS)
    return (use_brackets)? str( "[RIP+#]", name ) : name;
#else
    return (use_brackets)? str( "[#]", name ) : name;
#endif
}

string constant_address_double(double value_0, double value_1, bool use_brackets=true) {
    uint64 value_bits_0=*(uint64*)&value_0;
    uint64 value_bits_1=*(uint64*)&value_1;
    return constant_address_uint64(value_bits_0, value_bits_1, use_brackets);
}

string constant_address_avx512_uint64(array<uint64, 8> value, bool use_brackets=true) {
    static map<array<uint64, 8>, string> constant_map;
    string& name=constant_map[value];

    if (name.empty()) {
        name=m.alloc_label();

#if defined(CHIA_WINDOWS)
        APPEND_M(str( ".section .rdata,\"dr\"" ));
#elif defined(CHIAOSX)
        APPEND_M(str( ".text " ));
#else
        APPEND_M(str( ".text 1" ));
#endif
        APPEND_M(str( ".balign 64" ));
        APPEND_M(str( "#:", name ));
        for (int x=0;x<8;++x) {
            APPEND_M(str( ".quad #", to_hex(value[x]) )); //lane x
        }
        APPEND_M(str( ".text" ));
    }
#if defined(CHIAOSX) || defined(CHIA_WINDOWS)
    return (use_brackets)? str( "ZMMWORD PTR [RIP+#]", name ) : name;
#else
    return (use_brackets)? str( "ZMMWORD PTR [#]", name ) : name;
#endif
}

string constant_address_avx512_uint64(uint64 value, bool use_brackets=true) {
    array<uint64, 8> value_array;
    for (int x=0;x<8;++x) {
        value_array[x]=value;
    }

    return constant_address_avx512_uint64(value_array);
}


}