#include "director_speed_contracts.h"

struct DirectorSpeedLayout {
    uintptr_t function = 0, vtable = 0, literal = 0;
    size_t delta = 0, cap = 0;
    bool valid = false;
};
DirectorSpeedLayout g_director_speed;
std::atomic<bool> g_director_speed_ready{false};
void *g_director_speed_stub = nullptr;
void *g_director_speed_original = nullptr;
#if defined(AE_DEVICE_LAB)
float scaled_director_delta(float delta,float cap);
struct DirectorSpeedProbe {
    bool active=false;
    uint64_t original_calls=0,enabled=0,disabled=0,mismatches=0,capped=0;
    float before=0,after=0,cap=0;
};
DirectorSpeedProbe g_lab_speed;
void lab_record_speed(void *self,bool enabled,float before) {
    if(!g_lab_speed.active) return;
    auto &s=g_lab_speed;
    const auto object=reinterpret_cast<uintptr_t>(self);
    s.before=before;s.after=instance_member<float>(object+g_director_speed.delta);
    s.cap=instance_member<float>(object+g_director_speed.cap);
    const float expected=enabled ? scaled_director_delta(before,s.cap) : before;
    ++s.original_calls;enabled ? ++s.enabled : ++s.disabled;
    s.mismatches+=std::memcmp(&expected,&s.after,sizeof(float))!=0;
    s.capped+=enabled && s.cap>0 && before*32>s.cap && s.after==s.cap;
}
#endif

DirectorSpeedLayout resolve_director_speed(const std::vector<MemoryRange> &r, const std::vector<MemoryRange> &x) {
    using namespace item_removal_contracts;
    DirectorSpeedLayout out;
    const auto render = reinterpret_cast<uintptr_t>(resolve_app_symbol("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender"));
    auto contract = [&](uintptr_t f, size_t n, const auto &p) { return f ? removal_contract(r, x, f, n, p) : 0; };
    const auto entry = contract(render, removal_code_size(speed_native_render), speed_native_render);
    if (!entry) return out;
    const auto getter = removal_call(entry, speed_native_render, Role::repository);
    const auto loop_slot = removal_slot(entry, speed_native_render, Role::main_loop);
    out.vtable = unique_primary_vtable(r, x, "N7cocos2d19DisplayLinkDirectorE");
    const auto ctor = contract(getter, 512, speed_director_ctor);
    uintptr_t page = 0, table = 0, main_loop = 0; uint32_t reg = 0;
    if (!out.vtable || !ctor || !decode_adrp_page(read_u32(removal_capture(ctor, speed_director_ctor, Role::page)),
            removal_capture(ctor, speed_director_ctor, Role::page), &page, &reg) ||
        !decode_add_immediate(read_u32(removal_capture(ctor, speed_director_ctor, Role::page_add)), reg, page, &table) ||
        table != out.vtable || loop_slot > 4096 || !read_range_ptr(r, table + loop_slot, &main_loop)) return out;
    const auto loop = contract(main_loop, 512, speed_main_loop);
    const auto draw = removal_call(loop, speed_main_loop, Role::draw);
    const auto calculate = contract(draw, 1024, speed_draw_delta);
    const auto scheduler = contract(draw, 1024, speed_scheduler);
    const auto scene = contract(draw, 1024, speed_scene);
    if (!loop || !calculate || !scheduler || !scene ||
        removal_slot(scheduler, speed_scheduler, Role::dispatcher1) != removal_slot(scheduler, speed_scheduler, Role::dispatcher2) ||
        removal_call(scheduler, speed_scheduler, Role::dispatch1) != removal_call(scheduler, speed_scheduler, Role::dispatch2)) return out;
    out.function = removal_call(calculate, speed_draw_delta, Role::calculate);
    const auto zero = contract(out.function, 512, speed_zero);
    const auto cap = contract(out.function, 512, speed_cap);
    const auto tail = contract(out.function, 512, speed_tail);
    if (!zero || !cap || !tail) return out;
    auto field = [](uintptr_t at, const auto &pattern, Role role, unsigned scale) {
        return item_catalog::unsigned_offset(read_u32(removal_capture(at, pattern, role)), scale);
    };
    out.delta = field(tail, speed_tail, Role::delta, 2);
    out.cap = field(cap, speed_cap, Role::cap, 2);
    if (!out.delta || !out.cap || out.delta == out.cap || std::max(out.delta, out.cap) > 4096 ||
        out.delta != field(zero, speed_zero, Role::delta, 2) || out.delta != field(scheduler, speed_scheduler, Role::delta, 2) ||
        out.delta != field(scene, speed_scene, Role::delta, 2) ||
        field(cap, speed_cap, Role::stamp, 3) != field(tail, speed_tail, Role::stamp, 3) ||
        field(zero, speed_zero, Role::flag, 0) != field(zero, speed_zero, Role::flag_store, 0)) return out;
    const auto literal_page = removal_capture(tail, speed_tail, Role::page);
    const auto literal_load = removal_capture(tail, speed_tail, Role::literal);
    if (!decode_adrp_page(read_u32(literal_page), literal_page, &page, &reg) ||
        ((read_u32(literal_load) >> 5U) & 31U) != reg) return out;
    out.literal = page + field(tail, speed_tail, Role::literal, 2);
    float constant = 0;
    // An old module's live literal patch must not compound this hook. Fresh
    // process loading restores immutable image data; never adopt another patch.
    if (!range_contains(r, out.literal, sizeof(constant)) || vm_read_partial(out.literal, &constant, sizeof(constant)) != sizeof(constant) ||
        constant != 1000000.0f) return out;
    out.valid = true;
    return out;
}

float scaled_director_delta(float delta, float cap) {
    if (!std::isfinite(delta) || delta <= 0.0f || delta > std::numeric_limits<float>::max() / 32.0f) return delta;
    const float scaled = delta * 32.0f;
    return cap > 0.0f && scaled > cap ? cap : scaled;
}
