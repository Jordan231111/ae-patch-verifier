// Dialogue advances through sequential native update passes before the normal
// scene render. The preceding scheduler pass has returned before another starts.
// The engine's original render and autorelease cleanup still run once per frame.
struct DialogueIterationLayout {
    uintptr_t render=0, director_get=0, dispatch=0, update=0, action_update=0;
    uintptr_t ui_get=0, ui_has=0;
    size_t dispatcher=0, scheduler=0, before=0, after=0, paused=0, next_scene=0, scene=0;
    size_t purge=0, restart=0, stopped=0;
    int32_t talk_type=0;
    bool valid=false;
};
DialogueIterationLayout g_dialogue_iteration;
std::atomic<bool> g_dialogue_iteration_ready{false};
void *g_dialogue_render_stub=nullptr, *g_dialogue_render_original=nullptr;
void *g_dialogue_dispatch_stub=nullptr, *g_dialogue_dispatch_original=nullptr;
void *g_dialogue_action_stub=nullptr, *g_dialogue_action_original=nullptr;
thread_local uintptr_t g_dialogue_director=0;

DialogueIterationLayout resolve_dialogue_iteration(const std::vector<MemoryRange> &r,
                                                   const std::vector<MemoryRange> &x) {
    using namespace item_removal_contracts;
    if (!g_director_speed.valid) return {};
    DialogueIterationLayout out;
    out.render=reinterpret_cast<uintptr_t>(resolve_app_symbol("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender"));
    const auto entry=removal_contract(r,x,out.render,removal_code_size(speed_native_render),speed_native_render);
    if (!entry) return {};
    out.director_get=removal_call(entry,speed_native_render,Role::repository);
    uintptr_t main_loop=0;
    if (!read_range_ptr(r,g_director_speed.vtable+removal_slot(entry,speed_native_render,Role::main_loop),&main_loop)) return {};
    const auto loop=removal_contract(r,x,main_loop,512,speed_main_loop);
    const auto draw=removal_call(loop,speed_main_loop,Role::draw);
    const auto scheduler=removal_contract(r,x,draw,1024,speed_scheduler);
    if (!scheduler) return {};
    auto field=[](uintptr_t at,size_t instruction,unsigned scale) {
        return item_catalog::unsigned_offset(read_u32(at+instruction*4),scale);
    };
    out.dispatcher=field(scheduler,2,3);
    out.before=field(scheduler,3,3);
    out.scheduler=field(scheduler,5,3);
    out.after=field(scheduler,9,3);
    out.paused=field(scheduler,0,0);
    out.dispatch=removal_call(scheduler,speed_scheduler,Role::dispatch1);
    out.update=removal_call(scheduler,speed_scheduler,Role::inner);
    static constexpr Word transition[] = {
        {0xF9400268,0xFFC003FF,Role::none},
        {0xB4000008,0xFF00001F,Role::none},
        {0xAA1303E0,0xFFFFFFFF,Role::none},
        {0x94000000,0xFC000000,Role::none},
    };
    static constexpr Word lifecycle[] = {
        {0x39400008,0xFFC003FF,Role::none},
        {0x34000008,0xFF00001F,Role::none},
        {0x3900001F,0xFFC003FF,Role::none},
        {0x14000000,0xFC000000,Role::none},
        {0x39400008,0xFFC003FF,Role::none},
        {0x34000008,0xFF00001F,Role::none},
        {0x3900001F,0xFFC003FF,Role::none},
        {0x14000000,0xFC000000,Role::none},
        {0x39400008,0xFFC003FF,Role::none},
        {0x34000008,0xFF00001F,Role::none},
        {0xD65F03C0,0xFFFFFFFF,Role::none},
    };
    const auto change=removal_contract(r,x,draw,1024,transition);
    const auto life=removal_contract(r,x,main_loop,256,lifecycle);
    if (!change || !life || field(life,0,0)!=field(life,2,0) || field(life,4,0)!=field(life,6,0)) return {};
    out.next_scene=field(change,0,3);
    const auto scene=removal_contract(r,x,draw,1024,speed_scene);
    if (!scene) return {};
    out.scene=field(scene,0,3);
    out.purge=field(life,0,0);out.restart=field(life,4,0);out.stopped=field(life,8,0);
    for (const auto offset:{out.dispatcher,out.scheduler,out.before,out.after,out.paused,out.next_scene,
                            out.purge,out.restart,out.stopped,out.scene}) if (!offset || offset>4096) return {};
    if (out.before==out.after || out.dispatcher==out.scheduler ||
        field(scheduler,8,3)!=out.dispatcher || field(scheduler,6,2)!=g_director_speed.delta ||
        removal_call(scheduler,speed_scheduler,Role::dispatch2)!=out.dispatch ||
        !range_contains(x,out.dispatch,4) || !range_contains(x,out.update,4)) return {};

    // Two independent Lua bindings agree on the actual talk UI query and kind.
    // No Lua state is needed to check whether a window is still alive.
    static constexpr Word talking[] = {
        {0x94000000,0xFC000000,Role::none},
        {0x52800001,0xFFE0001F,Role::none},
        {0x94000000,0xFC000000,Role::none},
        {0x12000001,0xFFFFFFFF,Role::none},
        {0xAA1303E0,0xFFFFFFFF,Role::none},
        {0x94000000,0xFC000000,Role::none},
    };
    for (const char *name:{"isTalking","isFieldTalking"}) {
        uintptr_t binding=0,getter=0,query=0;
        if (!resolve_lua_registered_function(r,x,name,false,&binding)) return {};
        const auto at=removal_contract(r,x,binding,256,talking);
        if (!at || !decode_branch_target(at,true,&getter) || !decode_branch_target(at+8,true,&query) ||
            !range_contains(x,getter,4) || !range_contains(x,query,4)) return {};
        const int32_t kind=(read_u32(at+4)>>5U)&0xFFFFU;
        if (!kind || (out.ui_get && (getter!=out.ui_get || query!=out.ui_has || kind!=out.talk_type))) return {};
        out.ui_get=getter;out.ui_has=query;out.talk_type=kind;
    }
    // The scheduler's typed ActionManager callback forwards exactly (self,dt).
    // Use its RTTI and invocation body, not a version-specific code address.
    constexpr char action_callback[] = "NSt6__ndk110__function6__funcIZN7cocos2d9Scheduler14scheduleUpdateINS2_13ActionManagerEEEvPT_ibEUlfE_NS_9allocatorIS8_EEFvfEEE";
    const auto table=unique_primary_vtable(r,x,action_callback);
    if (table) {
        for (size_t slot=0;slot<9;++slot) {
            uintptr_t invoke=0,target=0;
            if (read_range_ptr(r,table+slot*sizeof(uintptr_t),&invoke) && range_contains(x,invoke,12) &&
                read_u32(invoke)==0xBD400020 && read_u32(invoke+4)==0xF9400400 &&
                decode_branch_target(invoke+8,false,&target) && range_contains(x,target,4)) {
                if (out.action_update && out.action_update!=target) return {};
                out.action_update=target;
            }
        }
    }
    out.valid=true;
    return out;
}

float dialogue_action_delta(float dt) {
    if (!std::isfinite(dt) || dt<=0 || dt>=1.0f) return dt;
    return std::min(dt*64.0f,1.0f);
}
