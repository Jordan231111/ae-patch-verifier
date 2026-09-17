#pragma once

#include <cstddef>
#include <cstdint>

// Instruction signatures, not object layouts. Every game field immediate is masked
// and decoded from the matched instruction. Keep the verifier on this same contract.
namespace item_catalog {

enum class Role {
    actor_type_slot, hp_model_slot, saved_value, self_argument, virtual_call, window_aux, window_member, window_ready, window_rendering, window_started,
    bridge_receiver_page, bridge_receiver_load, bridge_callback_slot, ad_state_branch,
    model_control, model_forward, model_inner, model_object, model_quantity, selected_commodity_call, selected_cost_call, selected_currency_call, selected_item_call, selected_quantity_call, view_commodity, view_currency, view_item, view_marker, map_value,
    none, global_page, global_load, global_store, allocation_size,
    root, key, key_check, right_delta, left, item, name_slot,
    string_result, string_alias, string_tag, string_data, user_control, user_ptr_check,
    achievement_end, achievement_map, amount_slot, commodity_dirty, commodity_id, commodity_total, domain_user_call, event_label, event_zero_pair, master_ptr, secure_array, secure_call, secure_index, secure_key, secure_seed, tree_call, user_ptr, user_setter_call, userdata_dirty, userdata_map, userdata_vector
};

struct Word {
    uint32_t value;
    uint32_t mask;
    Role role;
};

inline constexpr Word purchase_token_read[] = {
    {0xF9400008, 0xFFFFFFFF, Role::none},
    {0xF9400108, 0xFFC003FF, Role::none},
    {0xD63F0100, 0xFFFFFFFF, Role::none},
    {0xF94002E8, 0xFFFFFFFF, Role::none},
    {0x1B16FEA1, 0xFFFFFFFF, Role::none},
    {0x2A0003E2, 0xFFFFFFFF, Role::none},
    {0xF9400108, 0xFFC003FF, Role::none},
    {0xAA1703E0, 0xFFFFFFFF, Role::none},
    {0xD63F0100, 0xFFFFFFFF, Role::none},
};

inline constexpr Word singleton[] = {
    {0xA9BE7BFD, 0xFFFFFFFF, Role::none},
    {0xA9014FF4, 0xFFFFFFFF, Role::none},
    {0x910003FD, 0xFFFFFFFF, Role::none},
    {0x90000014, 0x9F00001F, Role::global_page},
    {0xF9400293, 0xFFC003FF, Role::global_load},
    {0xB5000013, 0xFF00001F, Role::none},
    {0x52800000, 0xFFE0001F, Role::allocation_size},
    {0x94000000, 0xFC000000, Role::none},
    {0xAA0003F3, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
    {0xF9000293, 0xFFC003FF, Role::global_store},
    {0xAA1303E0, 0xFFFFFFFF, Role::none},
};

// Lower-bound search and shared_ptr return in the assertion-anchored numeric getter.
// The second key load must agree with the first; the loop and comparison registers
// must retain their relationships. Broad byte wildcards alone would not prove that.
inline constexpr Word numeric_lookup[] = {
    {0xF8400EA9, 0xFFE00FFF, Role::root},
    {0xB4000009, 0xFF00001F, Role::none},
    {0xAA1503E8, 0xFFFFFFFF, Role::none},
    {0xB940012A, 0xFFC003FF, Role::key},
    {0x9100012B, 0xFFC003FF, Role::right_delta},
    {0x6B00015F, 0xFFFFFFFF, Role::none},
    {0x9A89B16A, 0xFFFFFFFF, Role::none},
    {0x9A89B108, 0xFFFFFFFF, Role::none},
    {0xF9400149, 0xFFC003FF, Role::left},
    {0xB5000009, 0xFF00001F, Role::none},
    {0xEB15011F, 0xFFFFFFFF, Role::none},
    {0x54000000, 0xFF00001F, Role::none},
    {0xB9400109, 0xFFC003FF, Role::key_check},
    {0x6B09001F, 0xFFFFFFFF, Role::none},
    {0x5400000B, 0xFF00001F, Role::none},
    {0xA9402109, 0xFFC07FFF, Role::item},
    {0xA9002289, 0xFFFFFFFF, Role::none},
};

// The only string-returning virtual call in the getItemName wrapper. Besides the
// slot, capture stack operands to verify the supported libc++ return convention.
inline constexpr Word name_call[] = {
    {0xF9400008, 0xFFFFFFFF, Role::none},
    {0xF9400109, 0xFFC003FF, Role::name_slot},
    {0x910003E8, 0xFFC003FF, Role::string_result},
    {0x910003F5, 0xFFC003FF, Role::string_alias},
    {0xD63F0120, 0xFFFFFFFF, Role::none},
    {0x394003E8, 0xFFC003FF, Role::string_tag},
    {0xF94003E9, 0xFFC003FF, Role::string_data},
    {0xB24002AA, 0xFFFFFFFF, Role::none},
    {0x7200011F, 0xFFFFFFFF, Role::none},
    {0x9A890141, 0xFFFFFFFF, Role::none},
    {0xAA1303E0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
};

inline constexpr Word secure_integer[] = {
    {0xF9400268, 0xFFC003FF, Role::secure_index},
    {0xF9400269, 0xFFC003FF, Role::secure_key},
    {0xF940026A, 0xFFC003FF, Role::secure_array},
    {0xBD400261, 0xFFC003FF, Role::secure_seed},
    {0xCA290108, 0xFFFFFFFF, Role::none},
    {0xBC687940, 0xFFFFFFFF, Role::none},
    {0x2F08A421, 0xFFFFFFFF, Role::none},
    {0x2F08A400, 0xFFFFFFFF, Role::none},
    {0x2E201C20, 0xFFFFFFFF, Role::none},
    {0x0E212800, 0xFFFFFFFF, Role::none},
    {0x1E260000, 0xFFFFFFFF, Role::none},
};

inline constexpr Word userdata_members[] = {
    {0x52800008, 0xFFE0001F, Role::userdata_vector},
    {0x8B080260, 0xFFFFFFFF, Role::none},
    {0x910003E1, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
    {0xF9400288, 0xFFC003FF, Role::none},
    {0x91000100, 0xFFC003FF, Role::commodity_id},
    {0x94000000, 0xFC000000, Role::secure_call},
    {0x52800016, 0xFFE0001F, Role::userdata_map},
    {0xB81F03A0, 0xFFFFFFFF, Role::none},
    {0x8B160260, 0xFFFFFFFF, Role::none},
    {0xD10043A1, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
};

inline constexpr Word userdata_dirty[] = {
    {0x52800008, 0xFFE0001F, Role::userdata_dirty},
    {0x52800029, 0xFFFFFFFF, Role::none},
    {0x38286A69, 0xFFFFFFFF, Role::none},
};

inline constexpr Word shop_user_calls[] = {
    {0x910003E8, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::domain_user_call},
    {0xF94003E0, 0xFFC003FF, Role::none},
    {0x2A1303E1, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::user_setter_call},
};

inline constexpr Word shop_user_member[] = {
    {0xAA0003F5, 0xFFFFFFFF, Role::none},
    {0xAA0003F3, 0xFFFFFFFF, Role::none},
    {0xF94002E9, 0xFFC003FF, Role::none},
    {0xAA0803F4, 0xFFFFFFFF, Role::none},
    {0xF80003A9, 0xFFE00FFF, Role::none},
    {0xF8400EA9, 0xFFE00FFF, Role::user_ptr},
    {0xB5000009, 0xFF00001F, Role::none},
};

inline constexpr Word shop_user_return[] = {
    {0xF9400269, 0xFFC003FF, Role::user_ptr_check},
    {0xF9400268, 0xFFC003FF, Role::user_control},
    {0xA9002289, 0xFFFFFFFF, Role::none},
    {0xB4000008, 0xFF00001F, Role::none},
};

inline constexpr Word shop_master_member[] = {
    {0xA9405A60, 0xFFC07FFF, Role::master_ptr},
    {0x52800081, 0xFFFFFFFF, Role::none},
    {0x2A1F03E2, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
};

inline constexpr Word shop_user_total[] = {
    {0x91000274, 0xFFC003FF, Role::commodity_total},
    {0xEB16029F, 0xFFFFFFFF, Role::none},
    {0x54000000, 0xFF00001F, Role::none},
    {0x910003E0, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::secure_call},
    {0x2A0003E1, 0xFFFFFFFF, Role::none},
    {0xAA1403E0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
    {0xF94003E0, 0xFFC003FF, Role::none},
    {0x52800028, 0xFFFFFFFF, Role::none},
    {0x39000268, 0xFFC003FF, Role::commodity_dirty},
};

inline constexpr Word item_amount_call[] = {
    {0xF9400008, 0xFFC003FF, Role::none},
    {0x2A0203F3, 0xFFFFFFFF, Role::none},
    {0xAA0003F4, 0xFFFFFFFF, Role::none},
    {0x2A0103F5, 0xFFFFFFFF, Role::none},
    {0xF9400108, 0xFFC003FF, Role::amount_slot},
    {0xD63F0100, 0xFFFFFFFF, Role::none},
    {0x93407C08, 0xFFFFFFFF, Role::none},
};

inline constexpr Word achievement_members[] = {
    {0xAA0803F4, 0xFFFFFFFF, Role::none},
    {0xAA0003F5, 0xFFFFFFFF, Role::none},
    {0xF94002C8, 0xFFC003FF, Role::none},
    {0x91000000, 0xFFC003FF, Role::achievement_map},
    {0xAA0103F3, 0xFFFFFFFF, Role::none},
    {0xF80003A8, 0xFFE00FFF, Role::none},
    {0x94000000, 0xFC000000, Role::tree_call},
    {0x910002A8, 0xFFC003FF, Role::achievement_end},
    {0xEB00011F, 0xFFFFFFFF, Role::none},
    {0x54000000, 0xFF00001F, Role::none},
    {0xA9402009, 0xFFC07FFF, Role::map_value},
    {0xA9002289, 0xFFFFFFFF, Role::none},
};

inline constexpr Word achievement_tree[] = {
    {0xF8400E77, 0xFFE00FFF, Role::root},
    {0xB4000017, 0xFF00001F, Role::none},
    {0xAA0003F4, 0xFFFFFFFF, Role::none},
    {0xAA0103F5, 0xFFFFFFFF, Role::none},
    {0xAA1303F6, 0xFFFFFFFF, Role::none},
    {0x91000280, 0xFFC003FF, Role::none},
    {0x910002E1, 0xFFC003FF, Role::key},
    {0xAA1503E2, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
    {0x910002E8, 0xFFC003FF, Role::right_delta},
    {0x7200001F, 0xFFFFFFFF, Role::none},
    {0x9A971108, 0xFFFFFFFF, Role::none},
    {0x9A9712D6, 0xFFFFFFFF, Role::none},
    {0xF9400117, 0xFFC003FF, Role::left},
    {0xB5000017, 0xFF00001F, Role::none},
    {0xEB1302DF, 0xFFFFFFFF, Role::none},
    {0x54000000, 0xFF00001F, Role::none},
    {0x91000280, 0xFFC003FF, Role::none},
    {0x910002C2, 0xFFC003FF, Role::key_check},
    {0xAA1503E1, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
    {0x7200001F, 0xFFFFFFFF, Role::none},
    {0x9A961273, 0xFFFFFFFF, Role::none},
};

inline constexpr Word achievement_event[] = {
    {0xAA1503F3, 0xFFFFFFFF, Role::none},
    {0xA9007EBF, 0xFFC07FFF, Role::event_zero_pair},
    {0xF8000E7F, 0xFFE00FFF, Role::event_label},
    {0xF80003B5, 0xFFE00FFF, Role::none},
};

inline constexpr Word window_open[] = {
    {0x39400008, 0xFFC003FF, Role::window_aux},
    {0x39000009, 0xFFC003FF, Role::window_ready},
    {0x34000008, 0xFF00001F, Role::none},
    {0xF9400260, 0xFFC003FF, Role::none},
    {0x2A1F03E1, 0xFFFFFFFF, Role::none},
    {0xF9400008, 0xFFC003FF, Role::none},
    {0xF9400108, 0xFFC003FF, Role::none},
    {0xD63F0100, 0xFFFFFFFF, Role::none},
    {0x910003E8, 0xFFC003FF, Role::none},
    {0xAA1403E0, 0xFFFFFFFF, Role::none},
    {0x3900027F, 0xFFC003FF, Role::window_rendering},
    {0x94000000, 0xFC000000, Role::none},
};

inline constexpr Word window_update[] = {
    {0x39400268, 0xFFC003FF, Role::window_aux},
    {0x7900027F, 0xFFC003FF, Role::window_ready},
    {0x34000008, 0xFF00001F, Role::none},
    {0xF9400260, 0xFFC003FF, Role::none},
    {0xF9400008, 0xFFC003FF, Role::none},
    {0xF9400108, 0xFFC003FF, Role::none},
    {0x52800021, 0xFFFFFFFF, Role::none},
    {0xD63F0100, 0xFFFFFFFF, Role::none},
    {0x14000000, 0xFC000000, Role::none},
    {0x39400268, 0xFFC003FF, Role::window_started},
    {0x34000008, 0xFF00001F, Role::none},
};

inline constexpr Word window_ready_getter[] = {
    {0xF9400008, 0xFFC003FF, Role::window_member},
    {0xB4000008, 0xFF00001F, Role::none},
    {0x39400108, 0xFFC003FF, Role::window_ready},
    {0x7100011F, 0xFFFFFFFF, Role::none},
    {0x1A9F07E0, 0xFFFFFFFF, Role::none},
    {0xD65F03C0, 0xFFFFFFFF, Role::none},
};

inline constexpr Word team_branch_tail[] = {
    {0xAA0003F8, 0xFFFFFFFF, Role::saved_value},
    {0xAA1303E0, 0xFFFFFFFF, Role::self_argument},
    {0xF9400108, 0xFFC003FF, Role::hp_model_slot},
    {0xD63F0100, 0xFFFFFFFF, Role::virtual_call},
    {0xF9400000, 0xFFC003FF, Role::none},
    {0xCB1703E1, 0xFFFFFFFF, Role::none},
};

inline constexpr Word team_actor_type[] = {
    {0xB400001A, 0xFF00001F, Role::none},
    {0xF9400268, 0xFFC003FF, Role::none},
    {0xF9400108, 0xFFC003FF, Role::actor_type_slot},
    {0xAA1303E0, 0xFFFFFFFF, Role::none},
    {0xD63F0100, 0xFFFFFFFF, Role::none},
    {0xCB190301, 0xFFFFFFFF, Role::none},
    {0x2A0003E2, 0xFFFFFFFF, Role::none},
    {0xAA1A03E0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
};

inline constexpr Word bridge_simple[] = {
    {0xA9BF7BFD, 0xFFFFFFFF, Role::none},
    {0x910003FD, 0xFFC003FF, Role::none},
    {0x90000000, 0x9F00001F, Role::none},
    {0x91000000, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
    {0x90000008, 0x9F00001F, Role::bridge_receiver_page},
    {0xF9400100, 0xFFC003FF, Role::bridge_receiver_load},
    {0xF9400008, 0xFFFFFFFF, Role::none},
    {0xF9400101, 0xFFC003FF, Role::bridge_callback_slot},
    {0xA8C17BFD, 0xFFFFFFFF, Role::none},
    {0xD61F0020, 0xFFFFFFFF, Role::none},
};

inline constexpr Word bridge_available[] = {
    {0xA9BE7BFD, 0xFFFFFFFF, Role::none},
    {0xF9000BF3, 0xFFFFFFFF, Role::none},
    {0x910003FD, 0xFFC003FF, Role::none},
    {0x90000000, 0x9F00001F, Role::none},
    {0x91000000, 0xFFC003FF, Role::none},
    {0x12001C41, 0xFFFFFFFF, Role::none},
    {0x2A0203F3, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
    {0x90000008, 0x9F00001F, Role::bridge_receiver_page},
    {0xF9400100, 0xFFC003FF, Role::bridge_receiver_load},
    {0xF9400008, 0xFFFFFFFF, Role::none},
    {0xF9400102, 0xFFC003FF, Role::bridge_callback_slot},
    {0x2A1303E1, 0xFFFFFFFF, Role::none},
    {0xF9400BF3, 0xFFFFFFFF, Role::none},
    {0xA8C27BFD, 0xFFFFFFFF, Role::none},
    {0xD61F0040, 0xFFFFFFFF, Role::none},
};

inline constexpr Word bridge_reward[] = {
    {0xA9BF7BFD, 0xFFFFFFFF, Role::none},
    {0x910003FD, 0xFFC003FF, Role::none},
    {0x90000000, 0x9F00001F, Role::none},
    {0x91000000, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
    {0x90000008, 0x9F00001F, Role::bridge_receiver_page},
    {0xF9400100, 0xFFC003FF, Role::bridge_receiver_load},
    {0xF9400008, 0xFFFFFFFF, Role::none},
    {0xF9400102, 0xFFC003FF, Role::bridge_callback_slot},
    {0xAA1F03E1, 0xFFFFFFFF, Role::none},
    {0xA8C17BFD, 0xFFFFFFFF, Role::none},
    {0xD61F0040, 0xFFFFFFFF, Role::none},
};

inline constexpr Word bridge_reward_legacy[] = {
    {0xA9BE7BFD, 0xFFFFFFFF, Role::none},
    {0xF9000BF3, 0xFFFFFFFF, Role::none},
    {0x910003FD, 0xFFC003FF, Role::none},
    {0x90000000, 0x9F00001F, Role::none},
    {0x91000000, 0xFFC003FF, Role::none},
    {0xAA0203F3, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
    {0x90000008, 0x9F00001F, Role::bridge_receiver_page},
    {0xAA1303E1, 0xFFFFFFFF, Role::none},
    {0xF9400100, 0xFFC003FF, Role::bridge_receiver_load},
    {0xF9400008, 0xFFFFFFFF, Role::none},
    {0xF9400102, 0xFFC003FF, Role::bridge_callback_slot},
    {0xF9400BF3, 0xFFFFFFFF, Role::none},
    {0xA8C27BFD, 0xFFFFFFFF, Role::none},
    {0xD61F0040, 0xFFFFFFFF, Role::none},
};

inline constexpr Word ad_state[] = {
    {0x52800028, 0xFFFFFFFF, Role::none},
    {0x39000268, 0xFFC003FF, Role::none},
    {0x14000000, 0xFC000000, Role::ad_state_branch},
    {0x2A1F03E8, 0xFFFFFFFF, Role::none},
};
inline constexpr Word shop_accessors[] = {
    {0xD10063A8, 0xFFFFFFFF, Role::none},
    {0xF9400013, 0xFFC003FF, Role::none},
    {0xF9400260, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::selected_item_call},
    {0xF9400260, 0xFFC003FF, Role::none},
    {0xD100A3A8, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::selected_currency_call},
    {0xF9400260, 0xFFC003FF, Role::none},
    {0x910163E8, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::selected_commodity_call},
    {0xF9402FE0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::selected_cost_call},
    {0x2A0003F5, 0xFFFFFFFF, Role::none},
    {0xF9400260, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::selected_quantity_call},
    {0xF94033F7, 0xFFFFFFFF, Role::none},
    {0x2A0003F6, 0xFFFFFFFF, Role::none},
    {0xB4000017, 0xFF00001F, Role::none},
    {0x910022E1, 0xFFFFFFFF, Role::none},
    {0x92800000, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
    {0xB5000000, 0xFF00001F, Role::none},
    {0xF94002E8, 0xFFC003FF, Role::none},
    {0xAA1703E0, 0xFFFFFFFF, Role::none},
    {0xF9400108, 0xFFC003FF, Role::none},
    {0xD63F0100, 0xFFFFFFFF, Role::none},
    {0xAA1703E0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
    {0xF85E83A0, 0xFFFFFFFF, Role::none},
    {0xF85D83B7, 0xFFFFFFFF, Role::none},
    {0xF9400008, 0xFFC003FF, Role::none},
    {0xF9400108, 0xFFC003FF, Role::none},
    {0xD63F0100, 0xFFFFFFFF, Role::none},
    {0xF94002E8, 0xFFC003FF, Role::none},
    {0x1B16FEA1, 0xFFFFFFFF, Role::none},
    {0x2A0003E2, 0xFFFFFFFF, Role::none},
    {0xF9400108, 0xFFC003FF, Role::none},
    {0xAA1703E0, 0xFFFFFFFF, Role::none},
    {0xD63F0100, 0xFFFFFFFF, Role::none},
};

inline constexpr Word shop_proxy[] = {
    {0xF9400000, 0xFFC003FF, Role::model_inner},
    {0x14000000, 0xFC000000, Role::model_forward},
};

inline constexpr Word shop_ref[] = {
    {0xF940000A, 0xFFC003FF, Role::model_object},
    {0xF9400009, 0xFFC003FF, Role::model_control},
    {0xA900250A, 0xFFFFFFFF, Role::none},
    {0xB4000009, 0xFF00001F, Role::none},
};

inline constexpr Word shop_quantity[] = {
    {0xB9400000, 0xFFC003FF, Role::model_quantity},
    {0xD65F03C0, 0xFFFFFFFF, Role::none},
};

inline constexpr Word shop_refresh[] = {
    {0xF9400008, 0xFFC003FF, Role::view_item},
    {0xB4000008, 0xFF00001F, Role::none},
    {0xAA0003F3, 0xFFFFFFFF, Role::none},
    {0xF9400000, 0xFFC003FF, Role::view_currency},
    {0xB4000000, 0xFF00001F, Role::none},
    {0x39400268, 0xFFC003FF, Role::view_marker},
    {0x34000008, 0xFF00001F, Role::none},
    {0xF9400008, 0xFFC003FF, Role::none},
    {0xF9400108, 0xFFC003FF, Role::amount_slot},
    {0xD63F0100, 0xFFFFFFFF, Role::none},
    {0xF9400268, 0xFFC003FF, Role::view_commodity},
    {0x2A0003F4, 0xFFFFFFFF, Role::none},
    {0xAA0803E0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::selected_cost_call},
    {0xB9400268, 0xFFC003FF, Role::model_quantity},
    {0x1B007D08, 0xFFFFFFFF, Role::none},
    {0x6B08029F, 0xFFFFFFFF, Role::none},
    {0x5400000A, 0xFF00001F, Role::none},
};
struct Layout {
    uintptr_t global = 0;
    uintptr_t lookup = 0;
    uintptr_t name_callsite = 0;
    size_t allocation_size = 0;
    size_t root = 0;
    size_t left = 0;
    size_t right = 0;
    size_t key = 0;
    size_t item = 0;
    size_t name_slot = 0;
    size_t node_bytes = 0;
    size_t string_pair_gap = 0;
};

struct SecureLayout {
    bool valid = false;
    uintptr_t getter = 0;
    size_t array = 0, seed = 0, key = 0, index = 0;
};

struct ShopContext {
    bool valid = false;
    size_t state = 0, selection = 0;
};

struct ShopLayout {
    bool valid = false;
    size_t master = 0, user = 0, control = 0, id = 0, total = 0, dirty = 0;
    size_t userdata_vector = 0, userdata_map = 0, userdata_dirty = 0;
    ShopContext token{}, appraisal{};
};

struct AchievementLayout {
    bool map_valid = false, event_valid = false;
    size_t root = 0, left = 0, right = 0, key = 0, node_bytes = 0, event_label = 0;
};

struct LiveShopLayout {
    bool valid = false;
    size_t item = 0, control = 0, currency = 0, currency_control = 0, commodity = 0;
    size_t quantity = 0, marker = 0, extent = 0;
};

struct CatalogReference {
    uintptr_t key_address = 0, object = 0, control = 0;
};

struct ObjectLayouts {
    LiveShopLayout live{};
    SecureLayout secure{};
    ShopLayout shop{};
    AchievementLayout achievement{};
    bool amount_valid = false;
    size_t amount_slot = 0;
};

inline int64_t signed_bits(uint64_t value, unsigned bits) {
    const uint64_t sign = uint64_t{1} << (bits - 1);
    return static_cast<int64_t>((value ^ sign) - sign);
}

inline size_t unsigned_offset(uint32_t word, unsigned shift) {
    return static_cast<size_t>((word >> 10) & 0xFFFU) << shift;
}

}  // namespace item_catalog
