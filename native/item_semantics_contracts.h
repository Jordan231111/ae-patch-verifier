#pragma once
#include "item_removal_contracts.h"
// Ordinary scalar tickets and server-issued expiry records are distinct ABIs.
namespace item_removal_contracts {
inline constexpr Word legacy_ticket_load[] = {
    {0xF9400008, 0xFFC003FF, Role::userdata_field},
    {0x91000100, 0xFFC003FF, Role::secure_field},
    {0x94000000, 0xFC000000, Role::decoder},
    {0x35000015, 0xFF00001F, Role::none},
    {0x6B14001F, 0xFFFFFFFF, Role::none},
    {0x5400000A, 0xFF00001F, Role::none},
};
inline constexpr Word legacy_ticket_write[] = {
    {0xF9400260, 0xFFC003FF, Role::userdata_field},
    {0x2A1403E1, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::setter},
    {0x34000015, 0xFF00001F, Role::none},
    {0xF9400260, 0xFFC003FF, Role::source_field},
    {0x2A1503E1, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::wrapper},
};
inline constexpr Word limited_ticket_records[] = {
    {0xA9405AD7, 0xFFC07FFF, Role::live_field},
    {0xEB1602FF, 0xFFFFFFFF, Role::none},
    {0x54000000, 0xFF00001F, Role::none},
    {0x2A1F03F3, 0xFFFFFFFF, Role::none},
    {0xF94002E8, 0xFFFFFFFF, Role::none},
    {0xB4000008, 0xFF00001F, Role::none},
    {0x91000100, 0xFFC003FF, Role::secure_field},
    {0x94000000, 0xFC000000, Role::decoder},
    {0xF100041F, 0xFFFFFFFF, Role::none},
    {0xFA55A000, 0xFFFFFFFF, Role::none},
    {0x1A93D673, 0xFFFFFFFF, Role::none},
    {0x910042F7, 0xFFFFFFFF, Role::none},
    {0xEB1602FF, 0xFFFFFFFF, Role::none},
    {0x54000001, 0xFF00001F, Role::none},
};
} // namespace item_removal_contracts
