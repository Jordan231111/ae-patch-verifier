// Recover the engine's own input barriers from TalkUI::processWait. Their UI
// kinds and packet member move with the binary; none are fixed game offsets.
struct DialogueInputLayout {
    uintptr_t ui_get=0,ui_find=0,skip_get=0,skip_active=0,packet_kind=0,packet_selection=0;
    size_t packet=0,countdown=0;
    int32_t overlay1=0,overlay2=0,manual_kind=0;
    bool valid=false;
};
DialogueInputLayout g_dialogue_input;
std::atomic<bool> g_dialogue_input_ready{false};

DialogueInputLayout resolve_dialogue_input(const std::vector<MemoryRange> &r,
                                            const std::vector<MemoryRange> &x,
                                            const DialogueNativeResolution &native) {
    using item_catalog::Word;
    using item_catalog::Role;
    static constexpr Word barriers[] = {
        {0x94000000,0xFC000000,Role::none}, // UI manager
        {0x52800001,0xFFE0001F,Role::none}, // first overlay kind
        {0x94000000,0xFC000000,Role::none}, // find state
        {0xAA0003F4,0xFFFFFFFF,Role::none},
        {0x94000000,0xFC000000,Role::none},
        {0x52800001,0xFFE0001F,Role::none}, // second overlay kind
        {0x94000000,0xFC000000,Role::none},
        {0xAA000288,0xFFFFFFFF,Role::none},
        {0xB5000008,0xFF00001F,Role::none}, // either overlay blocks
        {0x94000000,0xFC000000,Role::none}, // native skip state
        {0x94000000,0xFC000000,Role::none},
        {0x37000000,0xFFF8001F,Role::none}, // skip permits manual packet
        {0xF9400260,0xFFC003FF,Role::none}, // packet member
        {0x94000000,0xFC000000,Role::none}, // packet type getter
        {0x7100001F,0xFFC003FF,Role::none}, // native manual type
        {0x54000000,0xFF00001F,Role::none}, // manual packet blocks
        {0xF9400260,0xFFC003FF,Role::none},
        {0x94000000,0xFC000000,Role::none}, // independently resolved timer
    };
    uint32_t w[std::size(barriers)]{};
    const auto at=scoped_layout_contract(x,native.talk_ui_process_wait,barriers,w);
    if (!at) return {};
    DialogueInputLayout out;
    uintptr_t other_get=0,other_find=0,timer=0;
    if (!decode_branch_target(at,true,&out.ui_get) || !decode_branch_target(at+8,true,&out.ui_find) ||
        !decode_branch_target(at+16,true,&other_get) || !decode_branch_target(at+24,true,&other_find) ||
        out.ui_get!=other_get || out.ui_find!=other_find ||
        !decode_branch_target(at+36,true,&out.skip_get) || !decode_branch_target(at+40,true,&out.skip_active) ||
        !decode_branch_target(at+52,true,&out.packet_kind) || !decode_branch_target(at+68,true,&timer) ||
        timer!=native.action_timer_value || w[12]!=w[16]) return {};
    for (const auto fn:{out.ui_get,out.ui_find,out.skip_get,out.skip_active,out.packet_kind})
        if (!range_contains(x,fn,4)) return {};
    // Both branches must converge on the native false return, not an arbitrary
    // control-flow edge in another routine.
    const auto overlay_exit=at+32+sign_extend_u64((w[8]>>5)&0x7FFFFU,19)*4;
    const auto manual_exit=at+60+sign_extend_u64((w[15]>>5)&0x7FFFFU,19)*4;
    if (overlay_exit!=manual_exit || !range_contains(x,manual_exit,4) || read_u32(manual_exit)!=0x2A1F03E0) return {};
    out.packet=item_catalog::unsigned_offset(w[12],3);
    out.overlay1=(w[1]>>5)&0xFFFFU;out.overlay2=(w[5]>>5)&0xFFFFU;
    out.manual_kind=(w[14]>>10)&0xFFFU;
    if (!out.packet || out.packet>4096 || !out.overlay1 || !out.overlay2 || !out.manual_kind) return {};
    out.valid=true;
    // Native resource-backed messages calculate their reading delay before the
    // wait callback. Validate that per-window timer against its renderer gate
    // and auto-feed query before changing it, independently of Lua arguments.
    static constexpr Word countdown[] = {
        {0xF9400260,0xFFC003FF,Role::none},
        {0x94000000,0xFC000000,Role::none},
        {0x36000000,0xFFF8001F,Role::none},
        {0xBD400260,0xFFC003FF,Role::none},
        {0x1E283800,0xFFFFFFFF,Role::none},
        {0xBD000260,0xFFC003FF,Role::none},
        {0x94000000,0xFC000000,Role::none},
    };
    uint32_t timer_words[std::size(countdown)]{};
    const auto timer_at=scoped_layout_contract(x,native.talk_ui_process_wait,countdown,timer_words);
    uintptr_t renderer=0,mode=0;
    if (timer_at && decode_branch_target(timer_at+4,true,&renderer) && renderer==native.rendering_checker &&
        decode_branch_target(timer_at+24,true,&mode) && mode==native.get_auto_feed_mode &&
        item_catalog::unsigned_offset(timer_words[0],3)==native.talk_ui_state_talk_layer_offset &&
        item_catalog::unsigned_offset(timer_words[3],2)==item_catalog::unsigned_offset(timer_words[5],2)) {
        const auto field=item_catalog::unsigned_offset(timer_words[3],2);
        if (field && field<4096) out.countdown=field;
    }
    // This extra capability is independent of the existing Lua-owned path.
    out.packet_selection=[&]() -> uintptr_t {
        // Native NPC messages never call Lua's talk binding. Recover the packet's
        // selection getter from the same tagged payload layout as its type getter,
        // and cross-check the member written by the public reply-registration API.
        uintptr_t callback=0;
        if (!resolve_lua_registered_function(r,x,"setTalkCallback",false,&callback)) return {};
        static constexpr Word selection_write[] = {
            {0xB8685928,0xFFFFFFFF,Role::none}, // callback enum -> selection mode
            {0xF84003A9,0xFFE00FFF,Role::none}, // registered talk object's shared pointer
            {0xB9000128,0xFFC003FF,Role::none},
        };
        uint32_t sw[std::size(selection_write)]{};
        if (!scoped_layout_contract(x,callback,selection_write,sw)) return {};
        const auto selection_member=item_catalog::unsigned_offset(sw[2],2);
        if (!selection_member || selection_member>4096) return {};
        static constexpr Word getter[] = {
            {0xB9400008,0xFFC003FF,Role::none},
            {0x71000D1F,0xFFFFFFFF,Role::none},
            {0x54000120,0xFFFFFFFF,Role::none},
            {0x7100091F,0xFFFFFFFF,Role::none},
            {0x540000A0,0xFFFFFFFF,Role::none},
            {0x7100051F,0xFFFFFFFF,Role::none},
            {0x54000101,0xFFFFFFFF,Role::none},
            {0xF9400000,0xFFC003FF,Role::none},
            {0x14000000,0xFC000000,Role::none},
            {0xF9400008,0xFFC003FF,Role::none},
            {0x14000002,0xFFFFFFFF,Role::none},
            {0xF9400008,0xFFC003FF,Role::none},
            {0xB9400100,0xFFC003FF,Role::none},
            {0xD65F03C0,0xFFFFFFFF,Role::none},
        };
        uint32_t kw[std::size(getter)]{};
        if (!read_item_contract(out.packet_kind,getter,kw)) return {};
        for (const auto candidate:find_item_contract(x,getter)) {
            uint32_t cw[std::size(getter)]{};uintptr_t domain_get=0;
            if (!read_item_contract(candidate,getter,cw) || cw[0]!=kw[0] || cw[7]!=kw[7] ||
                cw[9]!=kw[9] || cw[11]!=kw[11] ||
                item_catalog::unsigned_offset(cw[12],2)!=selection_member ||
                !range_contains(x,candidate+56,8) || read_u32(candidate+56)!=0x2A1F03E0 ||
                read_u32(candidate+60)!=0xD65F03C0 ||
                !decode_branch_target(candidate+32,false,&domain_get) || !range_contains(x,domain_get,4)) continue;
            if (out.packet_selection) return {}; // ambiguous layout: do not guess
            out.packet_selection=candidate;
        }
        return out.packet_selection;
    }();
    return out;
}
