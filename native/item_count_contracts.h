#pragma once
#include "item_removal_contracts.h"

// Count-only paths stop before the native result vector is cloned.
namespace item_removal_contracts {
inline constexpr Word count_tree[] = {
    {0xF9400288, 0xFFC003FF, Role::root},
    {0xB4000008, 0xFF00001F, Role::none},
    {0xB9400109, 0xFFC003FF, Role::key},
    {0x6B09001F, 0xFFFFFFFF, Role::none},
    {0x5400000B, 0xFF00001F, Role::none},
    {0x6B00013F, 0xFFFFFFFF, Role::none},
    {0x5400000A, 0xFF00001F, Role::none},
    {0x91000108, 0xFFC003FF, Role::right},
    {0xF9400108, 0xFFC003FF, Role::left},
    {0xB5000008, 0xFF00001F, Role::none},
    {0x14000000, 0xFC000000, Role::none},
};
inline constexpr Word count_query_at[] = {
    {0x910002A0, 0xFFC003FF, Role::input_field},
    {0x94000000, 0xFC000000, Role::decoder},
    {0xB90003E0, 0xFFC003FF, Role::none},
    {0x91000280, 0xFFC003FF, Role::map_field},
    {0x910003E1, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::inner},
    {0xEB13001F, 0xFFFFFFFF, Role::none},
    {0x54000000, 0xFF00001F, Role::none},
};
inline constexpr Word count_at_tree[] = {
    {0xF9400008, 0xFFC003FF, Role::root},
    {0xB4000008, 0xFF00001F, Role::none},
    {0xB9400029, 0xFFC003FF, Role::none},
    {0xB940010A, 0xFFC003FF, Role::key},
    {0x6B0A013F, 0xFFFFFFFF, Role::none},
    {0x5400000A, 0xFF00001F, Role::none},
    {0xF9400108, 0xFFC003FF, Role::left},
    {0xB5000008, 0xFF00001F, Role::none},
    {0x14000000, 0xFC000000, Role::none},
    {0x6B09015F, 0xFFFFFFFF, Role::none},
    {0x5400000A, 0xFF00001F, Role::none},
    {0xF9400108, 0xFFC003FF, Role::right},
    {0xB5000008, 0xFF00001F, Role::none},
};
inline constexpr Word count_at_value[] = {
    {0x91000100, 0xFFC003FF, Role::payload},
    {0xA8C07BFD, 0xFFC07FFF, Role::none},
    {0xD65F03C0, 0xFFFFFFFF, Role::none},
};
inline constexpr Word count_copy[] = {
    {0xA9400801, 0xFFC07FFF, Role::source_field},
    {0xCB010048, 0xFFFFFFFF, Role::none},
    {0x9344FD03, 0xFFFFFFFF, Role::none},
    {0xAA1303E0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
};
inline constexpr Word count_pet_map[] = {
    {0x91000260, 0xFFC003FF, Role::map_field},
    {0x910003E1, 0xFFC003FF, Role::none},
    {0x910003E3, 0xFFC003FF, Role::none},
    {0x910003E4, 0xFFC003FF, Role::none},
    {0xF90003E8, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::inner},
    {0xA9007E9F, 0xFFC07FFF, Role::none},
    {0xF900029F, 0xFFC003FF, Role::none},
    {0xA9400801, 0xFFC07FFF, Role::payload},
    {0xAA1403E0, 0xFFFFFFFF, Role::none},
    {0xCB010048, 0xFFFFFFFF, Role::none},
    {0x9344FD03, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
};
inline constexpr Word count_pet_tree[] = {
    {0xF8400EC9, 0xFFE00FFF, Role::root},
    {0xB4000009, 0xFF00001F, Role::none},
    {0xB9400028, 0xFFC003FF, Role::none},
    {0xAA0903F4, 0xFFFFFFFF, Role::none},
    {0xB9400129, 0xFFC003FF, Role::key},
    {0x6B09011F, 0xFFFFFFFF, Role::none},
    {0x5400000A, 0xFF00001F, Role::none},
    {0xF9400289, 0xFFC003FF, Role::left},
    {0xAA1403F6, 0xFFFFFFFF, Role::none},
    {0xB5000009, 0xFF00001F, Role::none},
    {0x14000000, 0xFC000000, Role::none},
    {0x6B08013F, 0xFFFFFFFF, Role::none},
    {0x5400000A, 0xFF00001F, Role::none},
    {0xF9400289, 0xFFC003FF, Role::right},
    {0xB5000009, 0xFF00001F, Role::none},
    {0x91000296, 0xFFC003FF, Role::source_field},
    {0x14000000, 0xFC000000, Role::none},
};
inline constexpr Word count_pet_node[] = {
    {0xB9400108, 0xFFC003FF, Role::none},
    {0xA9007C1F, 0xFFC07FFF, Role::end_field},
    {0xF900001F, 0xFFC003FF, Role::payload},
    {0xB9000008, 0xFFC003FF, Role::key},
};
inline constexpr Word count_unknown_live[] = {
    {0xA9406418, 0xFFC07FFF, Role::source_field},
    {0xA9007E7F, 0xFFC07FFF, Role::none},
    {0xF900027F, 0xFFC003FF, Role::none},
    {0xEB19031F, 0xFFFFFFFF, Role::none},
    {0x54000000, 0xFF00001F, Role::none},
    {0x2A0203F4, 0xFFFFFFFF, Role::none},
    {0xAA0103F5, 0xFFFFFFFF, Role::none},
    {0xF9400316, 0xFFC003FF, Role::none},
};
} // namespace item_removal_contracts
