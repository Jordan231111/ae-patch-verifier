// Derive the script owner lookup from two independent Lua registrations. The
// stock getScriptType binding dereferences a missing owner during bootstrap;
// the bridge below deliberately retains the lookup's null result instead.
struct DialogueScriptContext {
    uintptr_t system_get=0, find_script=0;
    size_t kind_offset=0;
    bool valid=false;
};
DialogueScriptContext g_dialogue_script_context;
std::atomic<bool> g_dialogue_script_context_ready{false};

DialogueScriptContext resolve_dialogue_script_context(const std::vector<MemoryRange> &r,
                                                       const std::vector<MemoryRange> &x) {
    using item_catalog::Word;
    using item_catalog::Role;
    uintptr_t kind_binding=0, register_binding=0;
    if (!resolve_lua_registered_function(r,x,"getScriptType",false,&kind_binding) ||
        !resolve_lua_registered_function(r,x,"registLocalFunction",false,&register_binding)) return {};
    static constexpr Word kind[] = {
        {0x94000000,0xFC000000,Role::none}, // system accessor
        {0xAA1303E1,0xFFFFFFFF,Role::none}, // Lua state argument
        {0x94000000,0xFC000000,Role::none}, // owner lookup
        {0xB9800001,0xFFC003FF,Role::none}, // signed script kind member
        {0xAA1303E0,0xFFFFFFFF,Role::none},
        {0x94000000,0xFC000000,Role::none},
    };
    static constexpr Word registration[] = {
        {0x94000000,0xFC000000,Role::none},
        {0xAA1303E1,0xFFFFFFFF,Role::none},
        {0x94000000,0xFC000000,Role::none},
        {0xB4000000,0xFF00001F,Role::none}, // registrar accepts an absent owner
        {0xAA1303E1,0xFFFFFFFF,Role::none},
        {0x94000000,0xFC000000,Role::none},
    };
    static constexpr Word lookup[] = {
        {0xA9402408,0xFFC07FFF,Role::none}, // begin/end of script pointer vector
        {0xEB09011F,0xFFFFFFFF,Role::none},
        {0x54000000,0xFF00001F,Role::none},
        {0xF9400100,0xFFFFFFFF,Role::none},
        {0xF940000A,0xFFC003FF,Role::none}, // member holding the Lua state
        {0xEB01015F,0xFFFFFFFF,Role::none},
        {0x54000000,0xFF00001F,Role::none},
        {0x91002108,0xFFFFFFFF,Role::none},
        {0x14000000,0xFC000000,Role::none},
        {0xAA1F03E0,0xFFFFFFFF,Role::none}, // return nullptr when unmatched
        {0xD65F03C0,0xFFFFFFFF,Role::none},
    };
    uint32_t kw[std::size(kind)]{},rw[std::size(registration)]{},lw[std::size(lookup)]{};
    const auto k=scoped_layout_contract(x,kind_binding,kind,kw);
    const auto reg=scoped_layout_contract(x,register_binding,registration,rw);
    DialogueScriptContext result;
    uintptr_t other_get=0,other_find=0;
    if (!k || !reg || !decode_branch_target(k,true,&result.system_get) ||
        !decode_branch_target(k+8,true,&result.find_script) ||
        !decode_branch_target(reg,true,&other_get) || !decode_branch_target(reg+8,true,&other_find) ||
        result.system_get!=other_get || result.find_script!=other_find ||
        !range_contains(x,result.system_get,4) ||
        !read_item_contract(result.find_script,lookup,lw)) return {};
    result.kind_offset=((kw[3]>>10)&0xFFFU)*4U;
    const auto state_offset=((lw[4]>>10)&0xFFFU)*8U;
    if (!result.kind_offset || result.kind_offset>4096 || !state_offset || state_offset>4096 ||
        result.kind_offset+sizeof(int32_t)>state_offset) return {};
    result.valid=true;
    return result;
}
