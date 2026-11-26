#pragma once

namespace runtime_context_contracts {
using item_catalog::Word;
using item_catalog::Role;
inline constexpr Word ad_bridge_entry[] = {
    {0xD10183FF, 0xFFFFFFFF, Role::none},
    {0xA9047BFD, 0xFFFFFFFF, Role::none},
    {0xA9054FF4, 0xFFFFFFFF, Role::none},
    {0x910103FD, 0xFFFFFFFF, Role::none},
    {0xD53BD054, 0xFFFFFFFF, Role::none},
    {0x90000001, 0x9F00001F, Role::none},
    {0x91000021, 0xFFC003FF, Role::none},
    {0xF9400288, 0xFFC003FF, Role::none},
    {0x910083E0, 0xFFFFFFFF, Role::none},
    {0xF81F83A8, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
};
inline constexpr Word ad_outer[] = {
    {0xAA0103F4, 0xFFFFFFFF, Role::none},
    {0xAA0003F5, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
    {0x2A0003F3, 0xFFFFFFFF, Role::none},
    {0x36000000, 0xFFF8001F, Role::none},
    {0xF94002A8, 0xFFC003FF, Role::none},
    {0xAA1403E1, 0xFFFFFFFF, Role::none},
    {0x91000100, 0xFFC003FF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
    {0x94000000, 0xFC000000, Role::none},
    {0x12000260, 0xFFFFFFFF, Role::none},
    {0xA9424FF4, 0xFFFFFFFF, Role::none},
    {0xF9400BF5, 0xFFFFFFFF, Role::none},
    {0xA8C37BFD, 0xFFFFFFFF, Role::none},
    {0xD65F03C0, 0xFFFFFFFF, Role::none},
};
inline constexpr Word ad_owner_entry[] = {
    {0xD53BD056, 0xFFFFFFFF, Role::none},
    {0x5281B5A1, 0xFFFFFFFF, Role::none},
    {0xAA0003F3, 0xFFFFFFFF, Role::none},
    {0xF94002C8, 0xFFC003FF, Role::none},
    {0xF81F83A8, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
    {0x2A0003F4, 0xFFFFFFFF, Role::none},
    {0x36000000, 0xFFF8001F, Role::none},
};
inline constexpr Word ad_callback[] = {
    {0x94000000, 0xFC000000, Role::none},
    {0x90000008, 0x9F00001F, Role::none},
    {0x91000108, 0xFFC003FF, Role::none},
    {0x9100C3F7, 0xFFFFFFFF, Role::none},
    {0xA9034FE8, 0xFFFFFFFF, Role::none},
    {0xF9002BF7, 0xFFFFFFFF, Role::none},
    {0x9100C3E1, 0xFFFFFFFF, Role::none},
    {0x94000000, 0xFC000000, Role::none},
    {0x2A0003F5, 0xFFFFFFFF, Role::none},
};
inline constexpr Word ad_state_epilogue[] = {
    {0xF94002C8, 0xFFC003FF, Role::none},
    {0xF85F83A9, 0xFFFFFFFF, Role::none},
    {0xEB09011F, 0xFFFFFFFF, Role::none},
    {0x54000001, 0xFF00001F, Role::none},
    {0x12000280, 0xFFFFFFFF, Role::none},
    {0xA94A4FF4, 0xFFFFFFFF, Role::none},
    {0xF94043F7, 0xFFFFFFFF, Role::none},
    {0xA94957F6, 0xFFFFFFFF, Role::none},
    {0xA9477BFD, 0xFFFFFFFF, Role::none},
    {0x9102C3FF, 0xFFFFFFFF, Role::none},
    {0xD65F03C0, 0xFFFFFFFF, Role::none},
};
inline constexpr Word ad_availability_return[] = {
    {0x12000260, 0xFFFFFFFF, Role::none},
    {0xA9454FF4, 0xFFFFFFFF, Role::none},
    {0xA9447BFD, 0xFFFFFFFF, Role::none},
    {0x910183FF, 0xFFFFFFFF, Role::none},
    {0xD65F03C0, 0xFFFFFFFF, Role::none},
};

}
