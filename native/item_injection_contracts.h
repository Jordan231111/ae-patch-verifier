#pragma once
#include <cstdint>

// Captured from the game's Lua changeItemAmount and token-shop purchase paths.
// Immediates that identify game addresses, stack locations and vtable fields are
// decoded at runtime. These contracts add no patches to either path.
namespace item_injection_contracts {
enum class Role { none, assign, cast_call, change_slot, check1, check2, check3, count_field, factory, high_half, id_constructor, id_getter, inner_factory, input_field, item_load, item_page, kind, low_half, manager, master1, master2, master3, master_getter, other_predicate, other_repository, pool_field, pool_predicate, repository, secure_copy, secure_field, source_field, sync, ticket_load, ticket_page, type_getter, watermark, writer,
    amount_slot, core_slot, notify_slot, state_slot, setter_slot, requested1, requested2, floor1, floor2 };
struct Word { uint32_t value, mask; Role role; };
inline constexpr Word pc_singleton_load[] = {
    {0x90000015,0x9F00001F,Role::none},{0xF9401688,0xFFFFFFFF,Role::none},
    {0xF81F83A8,0xFFFFFFFF,Role::none},{0xF94002B3,0xFFC003FF,Role::none},
    {0xB5000013,0xFF00001F,Role::none},{0x90000013,0x9F00001F,Role::none},
    {0x39400268,0xFFC003FF,Role::none},{0x34000008,0xFF00001F,Role::none}
};
inline constexpr Word pc_singleton_store[] = {
    {0x94000000,0xFC000000,Role::none},{0xAA0003F3,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::none},{0xF90002B3,0xFFC003FF,Role::none}
};
// Capture the currency ID explicitly forbidden by this build's native API.
inline constexpr Word currency_forbidden[] = {
    {0x52800017,0xFFE0001F,Role::low_half},{0xF94002C8,0xFFC003FF,Role::none},
    {0x91000000,0xFFC003FF,Role::source_field},{0x2A0203F3,0xFFFFFFFF,Role::none},
    {0x2A0103F4,0xFFFFFFFF,Role::none},{0x72A00017,0xFFE0001F,Role::high_half},
    {0xF80003A8,0xFFE00FFF,Role::none},{0x94000000,0xFC000000,Role::none},
    {0x6B17001F,0xFFFFFFFF,Role::none},{0x54000001,0xFF00001F,Role::none}
};
inline constexpr Word lua_token_repository[] = {
    {0x94000000, 0xFC000000, Role::repository}, {0xAA0003F5, 0xFFFFFFFF, Role::none},
    {0xF84003A0, 0xFFE00FFF, Role::none}, {0x910003E8, 0xFFC003FF, Role::none},
    {0x910003F6, 0xFFC003FF, Role::none}, {0x94000000, 0xFC000000, Role::id_getter},
    {0x910002C0, 0xFFC003FF, Role::none}, {0x94000000, 0xFC000000, Role::none},
    {0x2A0003F6, 0xFFFFFFFF, Role::none},
};
inline constexpr Word lua_token_assign[] = {
    {0x93407EC2, 0xFFFFFFFF, Role::none}, {0x910003E3, 0xFFFFFFFF, Role::none},
    {0xAA1503E0, 0xFFFFFFFF, Role::none}, {0x52938801, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::assign},
};
inline constexpr Word lua_change_dispatch[] = {
    {0xF94002A8, 0xFFFFFFFF, Role::none}, {0xF9400108, 0xFFC003FF, Role::change_slot},
    {0xAA1503E0, 0xFFFFFFFF, Role::none}, {0x2A1303E1, 0xFFFFFFFF, Role::none},
    {0x2A1403E2, 0xFFFFFFFF, Role::none}, {0xD63F0100, 0xFFFFFFFF, Role::none},
};
inline constexpr Word token_pool[] = {
    {0xA9402408, 0xFFC07FFF, Role::pool_field}, {0xEB09011F, 0xFFFFFFFF, Role::none},
    {0x54000000, 0xFF00001F, Role::none}, {0xA9405116, 0xFFFFFFFF, Role::none},
    {0xAA0303F3, 0xFFFFFFFF, Role::none}, {0xAA0203F7, 0xFFFFFFFF, Role::none},
    {0xAA0003F5, 0xFFFFFFFF, Role::none}, {0x2A0103F8, 0xFFFFFFFF, Role::none},
};
// Read the existing repository without invoking its lazy constructor during
// title-screen/loading state. The owner must be initialized by the host.
inline constexpr Word token_singleton_load[] = {
    {0x90000015,0x9F00001F,Role::item_page},
    {0xF9401688,0xFFFFFFFF,Role::none},
    {0xF81F83A8,0xFFFFFFFF,Role::none},
    {0xF94002B3,0xFFC003FF,Role::item_load},
    {0xB5000013,0xFF00001F,Role::none},
};
inline constexpr Word shop_token_kind[] = {
    {0x94000000, 0xFC000000, Role::repository}, {0xAA0003F5, 0xFFFFFFFF, Role::none},
    {0xD10003A0, 0xFFC003FF, Role::none}, {0x94000000, 0xFC000000, Role::kind},
    {0x2A0003F6, 0xFFFFFFFF, Role::none},
};

inline constexpr Word resource_gate[] = {
    {0xA9BF7BFD, 0xFFFFFFFF, Role::none}, {0x910003FD, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::repository}, {0x94000000, 0xFC000000, Role::pool_predicate},
    {0x36000000, 0xFFF8001F, Role::none}, {0x52800020, 0xFFFFFFFF, Role::none},
    {0xA8C17BFD, 0xFFFFFFFF, Role::none}, {0xD65F03C0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::other_repository}, {0xA8C17BFD, 0xFFFFFFFF, Role::none},
    {0x14000000, 0xFC000000, Role::other_predicate},
};
inline constexpr Word ticket_cast[] = {
    {0x90000001, 0x9F00001F, Role::item_page}, {0x90000002, 0x9F00001F, Role::ticket_page},
    {0xAA1503E0, 0xFFFFFFFF, Role::none}, {0xF9400021, 0xFFC003FF, Role::item_load},
    {0xF9400042, 0xFFC003FF, Role::ticket_load}, {0xAA1F03E3, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::cast_call}, {0xB4000000, 0xFF00001F, Role::none},
};
inline constexpr Word ticket_dispatch[] = {
    {0xAA1603E0, 0xFFFFFFFF, Role::none}, {0x2A1303E1, 0xFFFFFFFF, Role::none},
    {0x2A1403E2, 0xFFFFFFFF, Role::none}, {0x528000C3, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::writer},
};
inline constexpr Word initial_equipment[] = {
    {0xF9400100, 0xFFC003FF, Role::master1}, {0x94000000, 0xFC000000, Role::check1},
    {0x37000000, 0xFFF8001F, Role::none}, {0xF94003E8, 0xFFC003FF, Role::none},
    {0xF9400100, 0xFFC003FF, Role::master2}, {0x94000000, 0xFC000000, Role::check2},
    {0x37000000, 0xFFF8001F, Role::none}, {0xF94003E8, 0xFFC003FF, Role::none},
    {0xF9400100, 0xFFC003FF, Role::master3}, {0x94000000, 0xFC000000, Role::check3},
};
inline constexpr Word master_type[] = {
    {0x910003E8, 0xFFFFFFFF, Role::none}, {0x94000000, 0xFC000000, Role::master_getter},
    {0xF94003E0, 0xFFFFFFFF, Role::none}, {0x910003E8, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::type_getter}, {0xF94007F3, 0xFFFFFFFF, Role::none},
};
inline constexpr Word sync_bridge[] = {
    {0x94000000, 0xFC000000, Role::manager}, {0xAA0003F3, 0xFFFFFFFF, Role::none},
    {0x90000001, 0x9F00001F, Role::none}, {0x91000021, 0xFFC003FF, Role::none},
    {0x910003E0, 0xFFFFFFFF, Role::none}, {0x94000000, 0xFC000000, Role::none},
    {0x910003E1, 0xFFFFFFFF, Role::none}, {0xAA1303E0, 0xFFFFFFFF, Role::none},
    {0x2A1F03E2, 0xFFFFFFFF, Role::none}, {0x2A1F03E3, 0xFFFFFFFF, Role::none},
    {0x2A1F03E4, 0xFFFFFFFF, Role::none}, {0x94000000, 0xFC000000, Role::sync},
};
inline constexpr Word amount_ceiling[] = {
    {0x52800008, 0xFFE0001F, Role::low_half}, {0xF9400289, 0xFFFFFFFF, Role::none},
    {0xAA1403E0, 0xFFFFFFFF, Role::none}, {0x72A00008, 0xFFE0001F, Role::high_half},
    {0xEB0802BF, 0xFFFFFFFF, Role::none}, {0xF9400123, 0xFFC003FF, Role::setter_slot},
    {0x9A88B2A1, 0xFFFFFFFF, Role::none}, {0x2A1303E2, 0xFFFFFFFF, Role::none},
};
inline constexpr Word token_low_watermark[] = {
    {0xA9402009, 0xFFC07FFF, Role::pool_field}, {0xCB090108, 0xFFFFFFFF, Role::none},
    {0xF100011F, 0xFFC003FF, Role::watermark}, {0x1A9F27E0, 0xFFFFFFFF, Role::none},
    {0xD65F03C0, 0xFFFFFFFF, Role::none},
};
inline constexpr Word amount_floor[] = {
    {0x93407C08, 0xFFFFFFFF, Role::none}, {0xAA1403E0, 0xFFFFFFFF, Role::none},
    {0x8B35C115, 0xFFFFFFFF, Role::none}, {0x94000000, 0xFC000000, Role::floor1},
    {0xEB20C2BF, 0xFFFFFFFF, Role::none}, {0x5400000A, 0xFF00001F, Role::none},
    {0xAA1403E0, 0xFFFFFFFF, Role::none}, {0x94000000, 0xFC000000, Role::floor2},
    {0x93407C15, 0xFFFFFFFF, Role::none},
};
inline constexpr Word base_set_core[] = {
    {0xF9400268, 0xFFFFFFFF, Role::none}, {0xAA1303E0, 0xFFFFFFFF, Role::none},
    {0xF9400108, 0xFFC003FF, Role::amount_slot}, {0xD63F0100, 0xFFFFFFFF, Role::none},
    {0xF9400268, 0xFFFFFFFF, Role::none}, {0xB84003A1, 0xFFE00FFF, Role::requested1},
    {0x2A0003F5, 0xFFFFFFFF, Role::none}, {0xAA1303E0, 0xFFFFFFFF, Role::none},
    {0x2A1403E2, 0xFFFFFFFF, Role::none}, {0xF9400108, 0xFFC003FF, Role::core_slot},
    {0xD63F0100, 0xFFFFFFFF, Role::none}, {0xF9400268, 0xFFFFFFFF, Role::none},
    {0xB84003A9, 0xFFE00FFF, Role::requested2}, {0xAA1303E0, 0xFFFFFFFF, Role::none},
    {0xF9400108, 0xFFC003FF, Role::notify_slot}, {0x4B150121, 0xFFFFFFFF, Role::none},
    {0xD63F0100, 0xFFFFFFFF, Role::none},
};
inline constexpr Word base_set_state[] = {
    {0xF9400268, 0xFFFFFFFF, Role::none}, {0xAA1303E0, 0xFFFFFFFF, Role::none},
    {0xF9400108, 0xFFC003FF, Role::state_slot}, {0xD63F0100, 0xFFFFFFFF, Role::none},
    {0x12001C08, 0xFFFFFFFF, Role::none}, {0x7100051F, 0xFFFFFFFF, Role::none},
    {0x54000000, 0xFF00001F, Role::none},
};
inline constexpr Word other_resource_low_watermark[] = {
    {0xF9400008, 0xFFC003FF, Role::count_field}, {0xF100011F, 0xFFC003FF, Role::watermark},
    {0x1A9F27E0, 0xFFFFFFFF, Role::none}, {0xD65F03C0, 0xFFFFFFFF, Role::none},
};
inline constexpr Word unknown_shop[] = {
    {0xF84003A0, 0xFFE00FFF, Role::none},
    {0x910003E8, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::id_getter},
    {0x91000340, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
    {0x2A0003E1, 0xFFFFFFFF, Role::none},
    {0x910003E0, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::id_constructor},
    {0x910003E0, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::factory},
};
inline constexpr Word id_copy[] = {
    {0x91000281, 0xFFC003FF, Role::source_field},
    {0xF8000668, 0xFFE00FFF, Role::secure_field},
    {0xAA1303E0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::secure_copy},
};
inline constexpr Word unknown_adapter[] = {
    {0x91000101, 0xFFC003FF, Role::input_field},
    {0xF80003A9, 0xFFE00FFF, Role::none},
    {0x910003E9, 0xFFC003FF, Role::none},
    {0x91000120, 0xFFC003FF, Role::none},
    {0xF90003F5, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::secure_copy},
    {0x910003E0, 0xFFC003FF, Role::none},
    {0x2A1F03E1, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::inner_factory},
};
} // namespace item_injection_contracts
