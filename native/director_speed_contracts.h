#pragma once
#include "item_removal_contracts.h"

// JNI entry, constructed Director type, and independent engine consumers agree
// on the frame delta field. Address/field immediates are decoded per build.
namespace item_removal_contracts {
inline constexpr Word speed_native_render[] = {
    {0xA9BF7BFD, 0xFFFFFFFF, Role::none},
    {0x910003FD, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::repository},
    {0xF9400008, 0xFFC003FF, Role::none},
    {0xF9400101, 0xFFC003FF, Role::main_loop},
    {0xA8C17BFD, 0xFFFFFFFF, Role::none},
    {0xD61F0020, 0xFFFFFFFF, Role::none},
};
inline constexpr Word speed_director_ctor[] = {
    {0x90000008, 0x9F00001F, Role::page},
    {0x91000108, 0xFFC003FF, Role::page_add},
    {0xAA1303E0, 0xFFFFFFFF, Role::none},
    {0xF9000268, 0xFFC003FF, Role::none},
    {0x3900027F, 0xFFC003FF, Role::none},
    {0xF9000293, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
};
inline constexpr Word speed_main_loop[] = {
    {0xA9BF7BFD, 0xFFFFFFFF, Role::none},
    {0x910003FD, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::draw},
    {0x94000000, 0xFC000000, Role::none},
    {0x94000000, 0xFC000000, Role::none},
    {0xA8C17BFD, 0xFFFFFFFF, Role::none},
    {0x14000000, 0xFC000000, Role::none},
};
inline constexpr Word speed_draw_delta[] = {
    {0xAA0003F3, 0xFFFFFFFF, Role::none},
    {0xF94002A8, 0xFFC003FF, Role::none},
    {0xF80003A8, 0xFFE00FFF, Role::none},
    {0x94000000, 0xFC000000, Role::calculate},
};
inline constexpr Word speed_scheduler[] = {
    {0x39400268, 0xFFC003FF, Role::none},
    {0x35000008, 0xFF00001F, Role::none},
    {0xF9400260, 0xFFC003FF, Role::dispatcher1},
    {0xF9400261, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::dispatch1},
    {0xF9400260, 0xFFC003FF, Role::source_field},
    {0xBD400260, 0xFFC003FF, Role::delta},
    {0x94000000, 0xFC000000, Role::inner},
    {0xF9400260, 0xFFC003FF, Role::dispatcher2},
    {0xF9400261, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::dispatch2},
};
inline constexpr Word speed_scene[] = {
    {0xF9400260, 0xFFC003FF, Role::none},
    {0xB4000000, 0xFF00001F, Role::none},
    {0xBD400260, 0xFFC003FF, Role::delta},
    {0x94000000, 0xFC000000, Role::inner},
    {0x6F00E400, 0xFFFFFFFF, Role::none},
};
inline constexpr Word speed_zero[] = {
    {0x39400268, 0xFFC003FF, Role::flag},
    {0x34000008, 0xFF00001F, Role::none},
    {0xB900027F, 0xFFC003FF, Role::delta},
    {0x3900027F, 0xFFC003FF, Role::flag_store},
    {0x14000000, 0xFC000000, Role::none},
};
inline constexpr Word speed_cap[] = {
    {0xF9400268, 0xFFC003FF, Role::stamp},
    {0xBD400262, 0xFFC003FF, Role::cap},
    {0xF2BC6A69, 0xFFFFFFFF, Role::none},
    {0xF2D374A9, 0xFFFFFFFF, Role::none},
    {0xCB080008, 0xFFFFFFFF, Role::none},
    {0xF2E41889, 0xFFFFFFFF, Role::none},
};
inline constexpr Word speed_tail[] = {
    {0x9E220100, 0xFFFFFFFF, Role::none},
    {0x90000008, 0x9F00001F, Role::page},
    {0xBD400101, 0xFFC003FF, Role::literal},
    {0x1E211800, 0xFFFFFFFF, Role::none},
    {0x2F00E401, 0xFFFFFFFF, Role::none},
    {0x1E216800, 0xFFFFFFFF, Role::none},
    {0x1E222000, 0xFFFFFFFF, Role::none},
    {0x1E21C444, 0xFFFFFFFF, Role::none},
    {0x1E20CC40, 0xFFFFFFFF, Role::none},
    {0xBD000260, 0xFFC003FF, Role::delta},
    {0xF9000260, 0xFFC003FF, Role::stamp},
};
} // namespace item_removal_contracts
