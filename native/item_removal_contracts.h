#pragma once
#include <cstdint>

// Semantic ARM64 contracts reached from exact class RTTI and native call edges.
// Address, stack and object-field immediates are captured, never version offsets.
namespace item_removal_contracts {
enum class Role { none, bool_getter, copy2, decoder, dispose, id_getter, inner, input_field, notify_slot, query, remove, repository, secure_copy, secure_field, source_field, type_getter, userdata_field, wrapper,
    accessor, active_pet, append, assign, clear_type, constructor, deleted_field, destructor, dirty_field, emplace, find,
    live_field, manager, map_field, page, page_add, payload, reload, setter, slot1, slot3, slot4, slot5, slot_getter, unlink,
    main_loop, draw, calculate, dispatcher1, dispatcher2, dispatch1, dispatch2, delta, flag, flag_store, stamp, cap, literal,
    root, key, left, right, end_field, factory, return_slot, control_slot, empty_branch, retained_branch,
    release_shared, release_weak, loop_branch, prepare };
struct Word { uint32_t value, mask; Role role; };
inline constexpr Word stock_amount[] = {
    {0x94000000, 0xFC000000, Role::repository},
    {0x910003E8, 0xFFC003FF, Role::none},
    {0xAA0003F4, 0xFFFFFFFF, Role::none},
    {0x90000016, 0x9F00001F, Role::none},
    {0x910002D6, 0xFFC003FF, Role::none},
    {0x91000100, 0xFFC003FF, Role::input_field},
    {0x91000261, 0xFFC003FF, Role::source_field},
    {0xF90003F6, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::secure_copy},
    {0xD10003A8, 0xFFC003FF, Role::none},
    {0x910003E1, 0xFFC003FF, Role::none},
    {0xAA1403E0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::query},
};
inline constexpr Word stock_dispose[] = {
    {0xA9404FB4, 0xFFC07FFF, Role::none},
    {0xD10003A8, 0xFFC003FF, Role::none},
    {0xD10003A0, 0xFFC003FF, Role::none},
    {0xF90003FF, 0xFFC003FF, Role::none},
    {0xF80003A8, 0xFFE00FFF, Role::none},
    {0x94000000, 0xFC000000, Role::dispose},
};
inline constexpr Word stock_remove[] = {
    {0xF8696908, 0xFFFFFFFF, Role::none},
    {0xF90003F6, 0xFFC003FF, Role::none},
    {0x91000320, 0xFFC003FF, Role::input_field},
    {0x91000101, 0xFFC003FF, Role::source_field},
    {0x94000000, 0xFC000000, Role::secure_copy},
    {0x94000000, 0xFC000000, Role::repository},
    {0xAA0003F5, 0xFFFFFFFF, Role::none},
    {0xF90003F6, 0xFFC003FF, Role::none},
    {0x91000340, 0xFFC003FF, Role::none},
    {0x91000321, 0xFFC003FF, Role::secure_field},
    {0x94000000, 0xFC000000, Role::copy2},
    {0x910003E1, 0xFFC003FF, Role::none},
    {0xAA1503E0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::remove},
};
inline constexpr Word stock_notify[] = {
    {0xF9400288, 0xFFFFFFFF, Role::none},
    {0xAA1403E0, 0xFFFFFFFF, Role::none},
    {0x2A1303E1, 0xFFFFFFFF, Role::none},
    {0xF9400108, 0xFFC003FF, Role::notify_slot},
    {0xD63F0100, 0xFFFFFFFF, Role::none},
};
inline constexpr Word buddy_amount[] = {
    {0x91000101, 0xFFC003FF, Role::source_field},
    {0xF80003A9, 0xFFE00FFF, Role::none},
    {0x910003E9, 0xFFC003FF, Role::none},
    {0x91000120, 0xFFC003FF, Role::input_field},
    {0xF90003F3, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::secure_copy},
    {0xD10003A8, 0xFFC003FF, Role::none},
    {0x910003E0, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::wrapper},
};
inline constexpr Word buddy_query[] = {
    {0x94000000, 0xFC000000, Role::repository},
    {0x910003E8, 0xFFC003FF, Role::none},
    {0xAA0003F5, 0xFFFFFFFF, Role::none},
    {0x90000017, 0x9F00001F, Role::none},
    {0x910002F7, 0xFFC003FF, Role::none},
    {0x91000100, 0xFFC003FF, Role::none},
    {0x91000261, 0xFFC003FF, Role::input_field},
    {0xF90003F7, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::secure_copy},
    {0x910003E1, 0xFFC003FF, Role::none},
    {0xAA1403E8, 0xFFFFFFFF, Role::none},
    {0xAA1503E0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::query},
};
inline constexpr Word buddy_inner[] = {
    {0x910003E0, 0xFFC003FF, Role::none},
    {0x2A1303E1, 0xFFFFFFFF, Role::none},
    {0x2A1503E2, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::inner},
};
inline constexpr Word buddy_remove[] = {
    {0xF8696908, 0xFFFFFFFF, Role::none},
    {0xF90003F4, 0xFFC003FF, Role::none},
    {0x910002C0, 0xFFC003FF, Role::none},
    {0x91000101, 0xFFC003FF, Role::source_field},
    {0x94000000, 0xFC000000, Role::secure_copy},
    {0x94000000, 0xFC000000, Role::repository},
    {0xAA0003F3, 0xFFFFFFFF, Role::none},
    {0xF90003F4, 0xFFC003FF, Role::none},
    {0x91000300, 0xFFC003FF, Role::none},
    {0x910002C1, 0xFFC003FF, Role::input_field},
    {0x94000000, 0xFC000000, Role::copy2},
    {0x910003E1, 0xFFC003FF, Role::none},
    {0xAA1303E0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::remove},
};
inline constexpr Word buddy_dispose[] = {
    {0xA9404FB5, 0xFFC07FFF, Role::none},
    {0xD10003A8, 0xFFC003FF, Role::none},
    {0xD10003A0, 0xFFC003FF, Role::none},
    {0xF90003FF, 0xFFC003FF, Role::none},
    {0xF80003A8, 0xFFE00FFF, Role::none},
    {0x94000000, 0xFC000000, Role::dispose},
};
inline constexpr Word buddy_notify[] = {
    {0xF9400288, 0xFFFFFFFF, Role::none},
    {0xAA1403E0, 0xFFFFFFFF, Role::none},
    {0x2A1303E1, 0xFFFFFFFF, Role::none},
    {0xF90003FF, 0xFFC003FF, Role::none},
    {0xF9400108, 0xFFC003FF, Role::notify_slot},
    {0xD63F0100, 0xFFFFFFFF, Role::none},
};
inline constexpr Word query_decoder[] = {
    {0x91000020, 0xFFC003FF, Role::input_field},
    {0x94000000, 0xFC000000, Role::decoder},
    {0xF9400288, 0xFFC003FF, Role::none},
    {0xB4000008, 0xFF00001F, Role::none},
};
inline constexpr Word unknown_amount[] = {
    {0x91000101, 0xFFC003FF, Role::source_field},
    {0xF80003A9, 0xFFE00FFF, Role::none},
    {0x910003E9, 0xFFC003FF, Role::none},
    {0x91000120, 0xFFC003FF, Role::input_field},
    {0xF90003F5, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::secure_copy},
    {0x910003E0, 0xFFC003FF, Role::none},
    {0x2A1F03E1, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::wrapper},
};
inline constexpr Word unknown_query[] = {
    {0x94000000, 0xFC000000, Role::repository},
    {0x910003E8, 0xFFC003FF, Role::none},
    {0xAA0003F5, 0xFFFFFFFF, Role::none},
    {0x90000017, 0x9F00001F, Role::none},
    {0x910002F7, 0xFFC003FF, Role::none},
    {0x91000100, 0xFFC003FF, Role::none},
    {0x91000281, 0xFFC003FF, Role::input_field},
    {0xF90003F7, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::secure_copy},
    {0xD10003A8, 0xFFC003FF, Role::none},
    {0x910003E1, 0xFFC003FF, Role::none},
    {0xAA1503E0, 0xFFFFFFFF, Role::none},
    {0x2A1303E2, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::query},
};
inline constexpr Word unknown_predicate[] = {
    {0xAA1603E0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::bool_getter},
    {0x4A140008, 0xFFFFFFFF, Role::none},
    {0x37000008, 0xFFF8001F, Role::none},
    {0xF94003E0, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::type_getter},
    {0x2A0003F6, 0xFFFFFFFF, Role::none},
    {0x910002A0, 0xFFC003FF, Role::input_field},
    {0x94000000, 0xFC000000, Role::decoder},
    {0x6B0002DF, 0xFFFFFFFF, Role::none},
    {0x54000001, 0xFF00001F, Role::none},
    {0x910003E1, 0xFFC003FF, Role::none},
    {0xAA1303E0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
};
inline constexpr Word unknown_dispose[] = {
    {0xA9404FB4, 0xFFC07FFF, Role::none},
    {0xD10003A8, 0xFFC003FF, Role::none},
    {0xD10003A0, 0xFFC003FF, Role::none},
    {0xF90003FF, 0xFFC003FF, Role::none},
    {0xF80003A8, 0xFFE00FFF, Role::none},
    {0x94000000, 0xFC000000, Role::dispose},
};
inline constexpr Word unknown_remove[] = {
    {0x94000000, 0xFC000000, Role::repository},
    {0xAA0003F5, 0xFFFFFFFF, Role::none},
    {0xAA1403E0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::id_getter},
    {0x2A0003E1, 0xFFFFFFFF, Role::none},
    {0xAA1503E0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::remove},
};
inline constexpr Word unknown_getter[] = {
    {0xF9400008, 0xFFC003FF, Role::userdata_field},
    {0x91000100, 0xFFC003FF, Role::secure_field},
    {0x14000000, 0xFC000000, Role::decoder},
};
} // namespace item_removal_contracts
