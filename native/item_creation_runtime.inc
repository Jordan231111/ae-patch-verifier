#include "item_creation_contracts.h"

template<size_t N>
bool verify_creation_cleanup(uintptr_t at, const item_removal_contracts::Word (&p)[N],
                             bool loop, uintptr_t &release_shared, uintptr_t &release_weak) {
    using namespace item_removal_contracts;
    const auto frame = removal_add(at, p, Role::return_slot);
    const auto control = item_catalog::signed_bits((read_u32(removal_capture(at, p, Role::control_slot)) >> 12U) & 511U, 9);
    if (frame == SIZE_MAX || control != 8 - static_cast<int64_t>(frame)) return false;
    const auto after_cleanup = at + removal_code_size(p) - (loop ? 8 : 0);
    for (Role role : {Role::empty_branch, Role::retained_branch}) {
        const auto pc = removal_capture(at, p, role);
        const auto target = pc + static_cast<intptr_t>(item_catalog::signed_bits((read_u32(pc) >> 5U) & 0x7ffffU, 19) * 4);
        if (target != after_cleanup) return false;
    }
    if (loop) {
        const auto pc = removal_capture(at, p, Role::loop_branch);
        if (pc + static_cast<intptr_t>(item_catalog::signed_bits((read_u32(pc) >> 5U) & 0x7ffffU, 19) * 4) != at) return false;
    }
    const auto shared = removal_call(at, p, Role::release_shared), weak = removal_call(at, p, Role::release_weak);
    if (!shared || !weak || (release_shared && release_shared != shared) || (release_weak && release_weak != weak)) return false;
    release_shared = shared; release_weak = weak;
    return true;
}

bool resolve_instance_creation(const std::vector<MemoryRange> &r, const std::vector<MemoryRange> &x) {
    using namespace item_removal_contracts;
    auto &equipment = g_instance_layouts[static_cast<size_t>(InstanceKind::Equipment)];
    auto &pet = g_instance_layouts[static_cast<size_t>(InstanceKind::Pet)];
    auto &buddy = g_instance_layouts[static_cast<size_t>(InstanceKind::Buddy)];
    // A changed factory disables only its own family. The other independently
    // proved owning-return contracts remain usable in the same loaded image.
    if (equipment.ready) {
        uintptr_t shared=0,weak=0;
        const auto e=removal_contract(r,x,equipment.change,4096,create_equipment);
        equipment.ready=e && verify_creation_cleanup(e,create_equipment,true,shared,weak);
        if(equipment.ready) {
            const auto factory=removal_call(e,create_equipment,Role::factory);
            auto adapters=removal_candidates(r,x,0,0,create_equipment_adapter);
            adapters.erase(std::remove_if(adapters.begin(),adapters.end(),[&](uintptr_t at) {
                return removal_call(at,create_equipment_adapter,Role::factory)!=factory;
            }),adapters.end());
            equipment.ready=adapters.size()==1;
            if(equipment.ready) equipment.create_one=adapters.front();
        }
    }
    if(pet.ready) {
        uintptr_t shared=0,weak=0;
        const auto p=removal_contract(r,x,pet.change,4096,create_pet);
        pet.ready=p && verify_creation_cleanup(p,create_pet,true,shared,weak);
        if(pet.ready) {
            pet.create_one=removal_call(p,create_pet,Role::factory);
            pet.ready=range_contains(x,pet.create_one,4);
        }
    }
    if(buddy.ready) {
        uintptr_t shared=0,weak=0;
        const auto b=removal_contract(r,x,buddy.change,1024,prepare_buddy);
        const auto inner=removal_contract(r,x,buddy.change,1024,buddy_inner);
        buddy.ready=b && inner && verify_creation_cleanup(b,prepare_buddy,false,shared,weak);
        if(buddy.ready) {
            buddy.prepare=removal_call(b,prepare_buddy,Role::prepare);
            buddy.create_one=removal_call(inner,buddy_inner,Role::inner);
            buddy.ready=range_contains(x,buddy.prepare,4) && range_contains(x,buddy.create_one,4);
        }
    }
    return equipment.ready && pet.ready && buddy.ready;
}
