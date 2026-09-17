#include "item_semantics_contracts.h"

struct SpecialInventoryLayout {
    uintptr_t scalar_ticket=0,record_ticket=0,growth=0,growth_receive=0;
    size_t scalar_userdata=0;
    bool scalar_ready=false,records_ready=false,growth_non_inventory=false;
};
SpecialInventoryLayout g_special_inventory;
SpecialInventoryLayout resolve_special_inventory(const std::vector<MemoryRange> &r,const std::vector<MemoryRange> &x,
                                                  size_t amount_slot,size_t core_slot) {
    using namespace item_removal_contracts;
    SpecialInventoryLayout out;
    out.scalar_ticket=unique_primary_vtable(r,x,"N6toybox19DomainLotteryTicketE");
    out.record_ticket=unique_primary_vtable(r,x,"N6toybox26DomainLimitedLotteryTicketE");
    out.growth=unique_primary_vtable(r,x,"N6toybox23DomainDestinyGrowthItemE");
    const auto method=[&](uintptr_t table,size_t slot) {
        uintptr_t fn=0;return table && read_range_ptr(r,table+slot,&fn) && range_contains(x,fn,4) ? fn : uintptr_t(0);
    };
    const auto one=[&](uintptr_t fn,const auto &p) {return fn ? removal_contract(r,x,fn,4096,p) : 0;};
    const auto legacy_amount=method(out.scalar_ticket,amount_slot),legacy_core=method(out.scalar_ticket,core_slot);
    if(legacy_amount && legacy_core && range_contains(x,legacy_amount,12) &&
       (read_u32(legacy_amount)&0xffc003ffU)==0xf9400008U && (read_u32(legacy_amount+4)&0xffc003ffU)==0x91000100U) {
        uintptr_t decode=0;
        const auto load=one(legacy_core,legacy_ticket_load),write=one(legacy_core,legacy_ticket_write);
        const auto setter=removal_call(write,legacy_ticket_write,Role::setter);
        const auto setter_body=one(setter,pet_create_counter_setter);
        out.scalar_userdata=item_catalog::unsigned_offset(read_u32(legacy_amount),3);
        out.scalar_ready=load && write && setter_body && decode_branch_target(legacy_amount+8,false,&decode) &&
            decode==removal_call(load,legacy_ticket_load,Role::decoder) &&
            decode==removal_call(setter_body,pet_create_counter_setter,Role::decoder) &&
            item_catalog::unsigned_offset(read_u32(legacy_amount),3)==removal_slot(load,legacy_ticket_load,Role::userdata_field) &&
            removal_slot(load,legacy_ticket_load,Role::userdata_field)==removal_slot(write,legacy_ticket_write,Role::userdata_field) &&
            removal_slot(write,legacy_ticket_write,Role::userdata_field)==removal_slot(write,legacy_ticket_write,Role::source_field) &&
            item_catalog::unsigned_offset(read_u32(legacy_amount+4),0)==removal_add(load,legacy_ticket_load,Role::secure_field) &&
            removal_add(load,legacy_ticket_load,Role::secure_field)==removal_add(setter_body,pet_create_counter_setter,Role::secure_field);
    }
    const auto record_amount=method(out.record_ticket,amount_slot),record_core=method(out.record_ticket,core_slot);
    out.records_ready=record_core && read_u32(record_core)==0xd65f03c0U && one(record_amount,limited_ticket_records);
    const auto growth_amount=method(out.growth,amount_slot),growth_core=method(out.growth,core_slot);
    out.growth_receive=injection_anchored_function(r,x,"void toybox::DestinyManager::receiveGrowthItem(const std::shared_ptr<DomainPC> &, std::function<void ()>)");
    out.growth_non_inventory=growth_core && read_u32(growth_core)==0xd65f03c0U && growth_amount &&
        range_contains(x,growth_amount,8) && read_u32(growth_amount)==0x2a1f03e0U && read_u32(growth_amount+4)==0xd65f03c0U && out.growth_receive;
    return out;
}
