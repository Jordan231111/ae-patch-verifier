#pragma once
#include "item_removal_contracts.h"
// Native Pet creation publication order and rollback boundaries.
namespace item_removal_contracts {
inline constexpr Word pet_create_inner[] = {
    {0x910003E1, 0xFFC003FF, Role::none},
    {0xAA1303E8, 0xFFFFFFFF, Role::none},
    {0xAA1603E0, 0xFFFFFFFF, Role::none},
    {0x2A1503E2, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::inner},
};
inline constexpr Word pet_create_counter[] = {
    {0x910003E8, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::accessor},
    {0xF94003E8, 0xFFC003FF, Role::none},
    {0x91000100, 0xFFC003FF, Role::secure_field},
    {0x94000000, 0xFC000000, Role::decoder},
    {0xF94003F8, 0xFFC003FF, Role::none},
};
inline constexpr Word pet_create_increment[] = {
    {0x910003E8, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::accessor},
    {0xF94003E0, 0xFFC003FF, Role::none},
    {0x110006E1, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::setter},
};
inline constexpr Word pet_create_basic_pair[] = {
    {0xAA0803F3, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::manager},
    {0xF9400008, 0xFFC003FF, Role::source_field},
    {0xA9402109, 0xFFC07FFF, Role::userdata_field},
    {0xA9002269, 0xFFFFFFFF, Role::none},
    {0xB4000088, 0xFFFFFFFF, Role::none},
    {0x91000101, 0xFFC003FF, Role::none},
    {0x52800020, 0xFFFFFFFF, Role::none},
};
inline constexpr Word pet_create_main[] = {
    {0xAA1403E0, 0xFFFFFFFF, Role::none},
    {0xAA1303E1, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::append},
};
inline constexpr Word pet_create_id_index[] = {
    {0x91000280, 0xFFC003FF, Role::map_field},
    {0xD10003A1, 0xFFC003FF, Role::none},
    {0x910003E3, 0xFFC003FF, Role::none},
    {0x910003E4, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::emplace},
    {0x91000000, 0xFFC003FF, Role::payload},
    {0xAA1303E1, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::assign},
};
inline constexpr Word pet_create_species_index[] = {
    {0x91000280, 0xFFC003FF, Role::map_field},
    {0x910003E1, 0xFFC003FF, Role::none},
    {0xD10003A3, 0xFFC003FF, Role::none},
    {0xD10003A4, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::emplace},
    {0x91000000, 0xFFC003FF, Role::payload},
    {0xAA1303E1, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::append},
};
inline constexpr Word pet_create_fbs[] = {
    {0xD10003A0, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::wrapper},
    {0xF84003B3, 0xFFE00FFF, Role::none},
};
inline constexpr Word pet_create_counter_setter[] = {
    {0x91000274, 0xffc003ff, Role::secure_field},
    {0xeb16029f, 0xffffffff, Role::none},
    {0x54000000, 0xff00001f, Role::none},
    {0x910003e0, 0xffc003ff, Role::none},
    {0x94000000, 0xfc000000, Role::decoder},
    {0x2a0003e1, 0xffffffff, Role::none},
    {0xaa1403e0, 0xffffffff, Role::none},
    {0x94000000, 0xfc000000, Role::setter},
    {0xf94003e0, 0xffc003ff, Role::none},
    {0x52800028, 0xffffffff, Role::none},
    {0x39000268, 0xffc003ff, Role::dirty_field},
};
} // namespace item_removal_contracts
