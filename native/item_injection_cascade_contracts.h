#pragma once
#include <cstdint>

// Native achievement continuations: ownership, eligibility, one signed reward,
// and manager lifecycle. Addresses are decoded from unique FDE-owned call edges.
namespace injection_cascade_contracts {
enum class Role { none, eligible, complete, remove, reward, assign, manager, set_triggers, slot_page, slot_load, slot_store, destroy, clear, repository, currency_repo, text_page, text_add, currency_get };
struct Word { uint32_t value, mask; Role role; };
inline constexpr Word callback[] = {
    {0xF9400814,0xFFFFFFFF,Role::none},
    {0xF9400000,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::eligible},
    {0x36000000,0xFFF8001F,Role::none},
    {0xF9400260,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::complete},
    {0xF9400260,0xFFFFFFFF,Role::none},
    {0xF9400294,0xFFFFFFFF,Role::none},
    {0x910003F6,0xFFFFFFFF,Role::none},
    {0xF9400008,0xFFFFFFFF,Role::none},
    {0xF9400909,0xFFFFFFFF,Role::none},
    {0x910003E8,0xFFFFFFFF,Role::none},
    {0xD63F0120,0xFFFFFFFF,Role::none},
    {0x910022C0,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::none},
    {0xAA0003E2,0xFFFFFFFF,Role::none},
    {0x91006261,0xFFFFFFFF,Role::none},
    {0xAA1403E0,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::remove},
};
inline constexpr Word eligible_call[] = {
    {0xF81F83A8,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::eligible},
    {0x36000000,0xFFF8001F,Role::none},
    {0xF9400268,0xFFFFFFFF,Role::none},
};
inline constexpr Word completion[] = {
    {0xAA1303E0,0xFFFFFFFF,Role::none},
    {0x52800061,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::reward},
};
inline constexpr Word reward_state[] = {
    {0xF9400008,0xFFFFFFFF,Role::none},
    {0xF9401509,0xFFFFFFFF,Role::none},
    {0xD10003A8,0xFFC003FF,Role::none},
    {0xD63F0120,0xFFFFFFFF,Role::none},
    {0xF84003A0,0xFFE00FFF,Role::none},
    {0xF9400008,0xFFFFFFFF,Role::none},
    {0xF9402D08,0xFFFFFFFF,Role::none},
    {0x2A1403E1,0xFFFFFFFF,Role::none},
    {0xD63F0100,0xFFFFFFFF,Role::none},
    {0x12001E88,0xFFFFFFFF,Role::none},
    {0x71000D1F,0xFFFFFFFF,Role::none},
    {0x54000001,0xFF00001F,Role::none},
    {0x94000000,0xFC000000,Role::repository},
    {0xAA0003F4,0xFFFFFFFF,Role::none},
};
inline constexpr Word reward_assign[] = {
    {0x52800001,0xFFE0001F,Role::none},
    {0xD10003A3,0xFFC003FF,Role::none},
    {0xAA1403E0,0xFFFFFFFF,Role::none},
    {0x72A00001,0xFFE0001F,Role::none},
    {0xAA1503E2,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::assign},
};
inline constexpr Word reward_currency[] = {
    {0xF84003A0,0xFFE00FFF,Role::none},
    {0xF9400008,0xFFFFFFFF,Role::none},
    {0xF9403908,0xFFFFFFFF,Role::none},
    {0x2A1403E1,0xFFFFFFFF,Role::none},
    {0xD63F0100,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::currency_repo},
    {0xAA0003F5,0xFFFFFFFF,Role::none},
    {0x90000001,0x9F00001F,Role::text_page},
    {0x91000021,0xFFC003FF,Role::text_add},
    {0xD10003A0,0xFFC003FF,Role::none},
    {0x94000000,0xFC000000,Role::none},
    {0xD10003A8,0xFFC003FF,Role::none},
    {0xD10003A1,0xFFC003FF,Role::none},
    {0xAA1503E0,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::currency_get},
};
inline constexpr Word reward_notify[] = {
    {0xF94002E8,0xFFFFFFFF,Role::none},
    {0xF9402908,0xFFFFFFFF,Role::none},
    {0xAA1703E0,0xFFFFFFFF,Role::none},
    {0xD63F0100,0xFFFFFFFF,Role::none},
    {0x2A0003E1,0xFFFFFFFF,Role::none},
    {0xF94002C8,0xFFFFFFFF,Role::none},
    {0xF9402908,0xFFFFFFFF,Role::none},
    {0xAA1603E0,0xFFFFFFFF,Role::none},
    {0x2A1403E2,0xFFFFFFFF,Role::none},
    {0xD63F0100,0xFFFFFFFF,Role::none},
};
// Complete successful copy-constructor path, including its return. Internal
// branch distances stay exact; only external call destinations move. This
// rejects additional capture fields instead of accepting a matching prefix.
inline constexpr Word capture_copy[] = {
    {0xA9BD7BFD,0xFFFFFFFF,Role::none},
    {0xF9000BF5,0xFFFFFFFF,Role::none},
    {0xA9024FF4,0xFFFFFFFF,Role::none},
    {0x910003FD,0xFFFFFFFF,Role::none},
    {0xA9402029,0xFFFFFFFF,Role::none},
    {0xAA0103F4,0xFFFFFFFF,Role::none},
    {0xAA0003F3,0xFFFFFFFF,Role::none},
    {0xA9002009,0xFFFFFFFF,Role::none},
    {0xB4000088,0xFFFFFFFF,Role::none},
    {0x91002101,0xFFFFFFFF,Role::none},
    {0x52800020,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::none},
    {0xA9412289,0xFFFFFFFF,Role::none},
    {0xAA1303F5,0xFFFFFFFF,Role::none},
    {0xA98122A9,0xFFFFFFFF,Role::none},
    {0xB4000088,0xFFFFFFFF,Role::none},
    {0x91002101,0xFFFFFFFF,Role::none},
    {0x52800020,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::none},
    {0xF9401288,0xFFFFFFFF,Role::none},
    {0xF9001268,0xFFFFFFFF,Role::none},
    {0xAA1403E8,0xFFFFFFFF,Role::none},
    {0x38428D09,0xFFFFFFFF,Role::none},
    {0x370000C9,0xFFFFFFFF,Role::none},
    {0xF9400909,0xFFFFFFFF,Role::none},
    {0x3DC00100,0xFFFFFFFF,Role::none},
    {0xF8038269,0xFFFFFFFF,Role::none},
    {0x3C828260,0xFFFFFFFF,Role::none},
    {0x14000004,0xFFFFFFFF,Role::none},
    {0xA9430682,0xFFFFFFFF,Role::none},
    {0x9100A260,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::none},
    {0xA9424FF4,0xFFFFFFFF,Role::none},
    {0xF9400BF5,0xFFFFFFFF,Role::none},
    {0xA8C37BFD,0xFFFFFFFF,Role::none},
    {0xD65F03C0,0xFFFFFFFF,Role::none},
};
// The native singleton receiver flows straight into setTriggers; the temporary
// vector's stack slot is decoded by the compiler and is not an object contract.
inline constexpr Word manager_caller[] = {
    {0x94000000,0xFC000000,Role::manager},
    {0x910003E1,0xFFC003FF,Role::none},
    {0x94000000,0xFC000000,Role::set_triggers},
};
inline constexpr Word accessor_slot[] = {
    {0x90000013,0x9F00001F,Role::slot_page},
    {0xF9400260,0xFFC003FF,Role::slot_load},
    {0xB5000000,0xFF00001F,Role::none},
};
inline constexpr Word manager_destroy[] = {
    {0x90000014,0x9F00001F,Role::slot_page},
    {0xF9400293,0xFFC003FF,Role::slot_load},
    {0xB4000013,0xFF00001F,Role::none},
    {0xAA1303E0,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::destroy},
    {0xAA1303E0,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::none},
    {0xF900029F,0xFFC003FF,Role::slot_store},
};
// Refresh obtains the native achievement vector and passes it to setTriggers.
// Older builds retain the existing subscription map; newer builds clear it first.
// The common call relation identifies the lifecycle entry in both implementations.
inline constexpr Word manager_reset[] = {
    {0x94000000,0xFC000000,Role::none},
    {0x910023E8,0xFFFFFFFF,Role::none},
    {0x910023F5,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::none},
    {0x910023E1,0xFFFFFFFF,Role::none},
    {0xAA1303E0,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::set_triggers},
};
// Earlier manager release inlines destruction of its two dispatcher owners.
inline constexpr Word manager_destroy_inline[] = {
    {0x90000014,0x9F00001F,Role::slot_page},
    {0xF9400293,0xFFC003FF,Role::slot_load},
    {0xB4000013,0xFF00001F,Role::none},
    {0x91004260,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::destroy},
    {0xAA1303E0,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::clear},
    {0xAA1303E0,0xFFFFFFFF,Role::none},
    {0x94000000,0xFC000000,Role::none},
    {0xF900029F,0xFFC003FF,Role::slot_store},
};
}
