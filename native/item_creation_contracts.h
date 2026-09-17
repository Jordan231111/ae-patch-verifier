#pragma once
#include "item_removal_contracts.h"

// Native per-instance creation boundaries and their owning return-value cleanup.
namespace item_removal_contracts {
inline constexpr Word create_equipment[] = {
    {0xD10003A8, 0xFFC003FF, Role::return_slot},
    {0xAA1403E0, 0xFFFFFFFF, Role::none},
    {0x2A1F03E1, 0xFFFFFFFF, Role::none},
    {0x2A1503E2, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::factory},
    {0xF84003B6, 0xFFE00FFF, Role::control_slot},
    {0xB4000016, 0xFF00001F, Role::empty_branch},
    {0x910022C1, 0xFFFFFFFF, Role::none},
    {0x92800000, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::release_shared},
    {0xB5000000, 0xFF00001F, Role::retained_branch},
    {0xF94002C8, 0xFFFFFFFF, Role::none},
    {0xAA1603E0, 0xFFFFFFFF, Role::none},
    {0xF9400908, 0xFFFFFFFF, Role::none},
    {0xD63F0100, 0xFFFFFFFF, Role::none},
    {0xAA1603E0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::release_weak},
    {0x71000718, 0xFFFFFFFF, Role::none},
    {0x54000001, 0xFF00001F, Role::loop_branch},
};
inline constexpr Word create_pet[] = {
    {0xD10003A8, 0xFFC003FF, Role::return_slot},
    {0xAA1403E0, 0xFFFFFFFF, Role::none},
    {0x2A1503E1, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::factory},
    {0xF84003B6, 0xFFE00FFF, Role::control_slot},
    {0xB4000016, 0xFF00001F, Role::empty_branch},
    {0x910022C1, 0xFFFFFFFF, Role::none},
    {0x92800000, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::release_shared},
    {0xB5000000, 0xFF00001F, Role::retained_branch},
    {0xF94002C8, 0xFFFFFFFF, Role::none},
    {0xAA1603E0, 0xFFFFFFFF, Role::none},
    {0xF9400908, 0xFFFFFFFF, Role::none},
    {0xD63F0100, 0xFFFFFFFF, Role::none},
    {0xAA1603E0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::release_weak},
    {0x71000718, 0xFFFFFFFF, Role::none},
    {0x54000001, 0xFF00001F, Role::loop_branch},
};
inline constexpr Word prepare_buddy[] = {
    {0xD10003A8, 0xFFC003FF, Role::return_slot},
    {0x94000000, 0xFC000000, Role::prepare},
    {0xF84003B6, 0xFFE00FFF, Role::control_slot},
    {0xB4000016, 0xFF00001F, Role::empty_branch},
    {0x910022C1, 0xFFFFFFFF, Role::none},
    {0x92800000, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::release_shared},
    {0xB5000000, 0xFF00001F, Role::retained_branch},
    {0xF94002C8, 0xFFFFFFFF, Role::none},
    {0xAA1603E0, 0xFFFFFFFF, Role::none},
    {0xF9400908, 0xFFFFFFFF, Role::none},
    {0xD63F0100, 0xFFFFFFFF, Role::none},
    {0xAA1603E0, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::release_weak},
};
inline constexpr Word create_equipment_adapter[] = {
    {0x2A0103E2, 0xFFFFFFFF, Role::none},
    {0x2A1F03E1, 0xFFFFFFFF, Role::none},
    {0x14000000, 0xFC000000, Role::factory},
};
} // namespace item_removal_contracts
