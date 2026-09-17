#include "mass_shop_contracts.h"

struct MassShopLayout {
    uintptr_t destructors[2]{};
    uintptr_t commodity_label=0;
    size_t label_slot=0;
    uintptr_t binder=0, lookup_after=0, copy=0, move=0, destroy=0, refresh=0, cost=0;
    uintptr_t detail_table=0, list_table=0, state_table=0, manager=0, cast=0, state_type=0, ui_type=0, node_type=0;
    size_t result_sp=0, detail=0, visible=0, running=0, parent=0, states=0, skip=0, priority=0, modal=0;
    item_catalog::LiveShopLayout live{};
    size_t selection=0;
    bool result_from_fp=false, valid=false;
};
MassShopLayout g_mass_shop, g_mass_appraisal;

// Resolve helper bodies by their native call edges, and larger blocks by their
// ELF unwind owner. A generic helper may occur elsewhere; only its owner counts.
template<size_t N>
uintptr_t mass_owner_block(const std::vector<MemoryRange> &r,const std::vector<MemoryRange> &x,
                           uintptr_t owner,const item_catalog::Word (&pattern)[N]) {
    std::vector<uintptr_t> candidates;
    for(auto at:find_item_contract(x,pattern))
        if(injection_function_start(r,x,at)==owner && injection_function_start(r,x,at+(N-1)*4)==owner)
            candidates.push_back(at);
    return candidates.size()==1 ? candidates.front() : 0;
}
template<size_t N>
bool mass_entry(const std::vector<MemoryRange> &x,uintptr_t at,const item_catalog::Word (&pattern)[N]) {
    uint32_t words[N]{};
    return at && range_contains(x,at,sizeof(words)) && read_item_contract(at,pattern,words);
}
uintptr_t mass_call(uintptr_t at,size_t word,bool link=true) {
    uintptr_t target=0;
    return at && decode_branch_target(at+word*4,link,&target) ? target : 0;
}
size_t mass_field(uintptr_t at,size_t word,unsigned scale=0) {
    return item_catalog::unsigned_offset(read_u32(at+word*4),scale);
}
int64_t mass_pair(uintptr_t at,size_t word) {
    return item_catalog::signed_bits((read_u32(at+word*4)>>15U)&127U,7)*8;
}
uintptr_t mass_address(uintptr_t at,size_t adrp,size_t add) {
    uintptr_t page=0,result=0;uint32_t reg=0;
    return at && decode_adrp_page(read_u32(at+adrp*4),at+adrp*4,&page,&reg) &&
           decode_add_immediate(read_u32(at+add*4),reg,page,&result) ? result : 0;
}
std::vector<std::pair<size_t,uintptr_t>> mass_virtuals(const std::vector<MemoryRange> &r,const std::vector<MemoryRange> &x,uintptr_t table) {
    std::vector<std::pair<size_t,uintptr_t>> result;
    for(size_t slot=0;slot<4096;slot+=sizeof(uintptr_t)) {
        uintptr_t function=0;
        if(!read_range_ptr(r,table+slot,&function) || !range_contains(x,function,4)) break;
        result.emplace_back(slot,function);
    }
    return result;
}
MassShopLayout mass_shop_unresolved(unsigned line) {
#ifdef AE_TEST_SYMBOL_RESOLVER
    std::fprintf(stderr,"MASS_CONTRACT_FAILURE %u\n",line);
#else
    (void)line;
#endif
    return {};
}
bool resolve_mass_lifetime(const std::vector<MemoryRange> &r,const std::vector<MemoryRange> &x,MassShopLayout &out) {
    using namespace mass_shop_contracts;
    const auto ui=unique_primary_vtable(r,x,"N6toybox3gui7UIStateE");
    const auto layer=unique_primary_vtable(r,x,"N7cocos2d5LayerE");
    uintptr_t type=0,base=0,layer_type=0,ui_dtor=0;
    if(!ui || !layer || !read_range_ptr(r,ui,&ui_dtor) || !read_range_ptr(r,layer-8,&layer_type) ||
       !read_range_ptr(r,out.state_table-8,&type)) return false;
    for(const auto expected:{out.ui_type,out.state_type,layer_type,out.node_type}) {
        if(!read_range_ptr(r,type+2*sizeof(uintptr_t),&base) || base!=expected) return false;
        type=base;
    }
    if(!read_range_ptr(r,out.state_table,&out.destructors[0]) ||
       !read_range_ptr(r,out.state_table+8,&out.destructors[1])) return false;
    const auto complete=out.destructors[0],deleting=out.destructors[1];
    return mass_entry(x,complete,state_complete_dtor) && mass_entry(x,deleting,state_deleting_dtor) &&
        mass_address(complete,5,6)==out.state_table && mass_field(complete,4,3)==mass_field(complete,14,3) &&
        mass_call(complete,17,false)==ui_dtor && mass_call(deleting,4)==complete &&
        mass_call(complete,12)==mass_call(deleting,8,false);
}
MassShopLayout resolve_mass_shop(const std::vector<MemoryRange> &r,const std::vector<MemoryRange> &x) {
    using namespace mass_shop_contracts;
    MassShopLayout out;
    const auto &live=g_object_layouts.live;
    if(!live.valid || !g_object_layouts.shop.token.valid) return out;
    const auto hits=find_item_contract(x,lookup);
    if(hits.size()!=1) return out;
    const auto at=hits.front();
    out.binder=injection_function_start(r,x,at);
    out.commodity_label=mass_call(at,5);
    if(!mass_owner_block(r,x,out.commodity_label,label_sret) ||
       !mass_owner_block(r,x,out.commodity_label,label_build)) return mass_shop_unresolved(__LINE__);
    const auto base_set=injection_anchored_function(r,x,"virtual void toybox::DomainItem::setAmount(int, uint32_t)");
    const auto core=base_set ? injection_contract(x,base_set,1024,item_injection_contracts::base_set_core) : 0;
    const auto notify_pc=injection_capture(core,item_injection_contracts::base_set_core,item_injection_contracts::Role::notify_slot);
    uintptr_t material=unique_primary_vtable(r,x,"N6toybox14DomainMaterialE"),notify=0;
    if(!core || injection_function_start(r,x,core)!=base_set || !notify_pc || !material ||
       !read_range_ptr(r,material+item_catalog::unsigned_offset(read_u32(notify_pc),3),&notify))
        return mass_shop_unresolved(__LINE__);
    const auto label=mass_owner_block(r,x,notify,event_label);
    if(!label) return mass_shop_unresolved(__LINE__);
    out.label_slot=mass_field(label,3,3);
    for(const char *name:{"N6toybox14DomainMaterialE","N6toybox19DomainDungeonTicketE","N6toybox17DomainJobRankItemE"}) {
        const auto table=unique_primary_vtable(r,x,name);uintptr_t getter=0;
        if(!table || !read_range_ptr(r,table+out.label_slot,&getter) ||
           !mass_owner_block(r,x,getter,label_sret) || !mass_owner_block(r,x,getter,label_build)) return mass_shop_unresolved(__LINE__);
    }
    const auto copy_at=mass_owner_block(r,x,out.binder,copy_bind);
    const auto refresh_at=mass_owner_block(r,x,out.binder,refresh_call);
    if(!copy_at || !refresh_at || mass_field(at,6)!=mass_field(at,11) || mass_field(at,3)!=mass_field(at,7) ||
       mass_field(at,10)!=live.item || mass_field(copy_at,0)!=live.commodity) return mass_shop_unresolved(__LINE__);
    out.result_sp=mass_field(at,6); out.lookup_after=at+40;
    out.copy=mass_call(copy_at,6);out.move=mass_call(at,13);out.refresh=mass_call(refresh_at,1);
    if(!mass_entry(x,out.copy,ref_copy) || !mass_entry(x,out.move,ref_move) ||
       mass_pair(out.copy,4)!=0 || mass_pair(out.copy,11)!=0 || mass_field(out.copy,10,3)!=sizeof(uintptr_t) ||
       mass_field(out.copy,7)!=sizeof(uintptr_t) || mass_field(out.copy,13)!=sizeof(uintptr_t) ||
       mass_field(out.move,3,4)!=0 || mass_pair(out.move,4)!=0 || mass_field(out.move,7,4)!=0 ||
       mass_field(out.move,6,3)!=sizeof(uintptr_t) || mass_field(out.move,9)!=sizeof(uintptr_t)) return mass_shop_unresolved(__LINE__);
    const auto ref_refresh=mass_owner_block(r,x,out.refresh,item_catalog::shop_refresh);
    uint32_t refresh_words[std::size(item_catalog::shop_refresh)]{};
    if(!ref_refresh || !read_item_contract(ref_refresh,item_catalog::shop_refresh,refresh_words)) return mass_shop_unresolved(__LINE__);
    out.cost=layout_call_target(ref_refresh,item_catalog::shop_refresh,item_catalog::Role::selected_cost_call);
    if(mass_field(ref_refresh,0,3)!=live.item || mass_field(ref_refresh,3,3)!=live.currency ||
       mass_field(ref_refresh,10,3)!=live.commodity || mass_field(ref_refresh,14,2)!=live.quantity ||
       mass_field(ref_refresh,8,3)!=g_object_layouts.amount_slot) return mass_shop_unresolved(__LINE__);
    out.detail_table=unique_primary_vtable(r,x,"N6toybox3gui28TokenShopCommodityDetailViewE");
    out.list_table=unique_primary_vtable(r,x,"N6toybox3gui27TokenShopCommodityListLayerE");
    out.state_table=unique_primary_vtable(r,x,"N6toybox3gui16TokenShopUIStateE");
    const auto node_table=unique_primary_vtable(r,x,"N7cocos2d4NodeE");
    const auto state_table=unique_primary_vtable(r,x,"N6toybox5StateE");
    const auto ui_table=unique_primary_vtable(r,x,"N6toybox3gui7UIStateE");
    uintptr_t dtor=0;
    if(!out.detail_table || !out.list_table || !out.state_table || !node_table || !state_table || !ui_table ||
       !read_range_ptr(r,out.detail_table,&dtor) || !read_range_ptr(r,node_table-8,&out.node_type) ||
       !read_range_ptr(r,state_table-8,&out.state_type) || !read_range_ptr(r,ui_table-8,&out.ui_type)) return mass_shop_unresolved(__LINE__);
    const auto cleanup=mass_owner_block(r,x,injection_function_start(r,x,dtor),dtor_bind);
    if(!cleanup || mass_address(cleanup,0,1)!=out.detail_table || mass_field(cleanup,4)!=live.currency ||
       mass_field(cleanup,6)!=live.item || mass_field(cleanup,8)!=live.commodity) return mass_shop_unresolved(__LINE__);
    out.destroy=mass_call(cleanup,9);
    if(!mass_entry(x,out.destroy,ref_dtor) || mass_field(out.destroy,3,3)!=sizeof(uintptr_t) ||
       mass_field(out.destroy,5)!=sizeof(uintptr_t) || mass_call(out.copy,15)!=mass_call(out.move,11) ||
       mass_call(out.copy,15)!=mass_call(out.destroy,7) || mass_call(out.copy,22)!=mass_call(out.move,18) ||
       mass_call(out.copy,22)!=mass_call(out.destroy,19,false)) return mass_shop_unresolved(__LINE__);
    std::vector<uintptr_t> show;
    for(auto candidate:find_item_contract(x,show_detail))
        if(mass_call(candidate,2)==out.binder && mass_field(candidate,0,3)==mass_field(candidate,3,3)) show.push_back(candidate);
    if(show.size()!=1) return mass_shop_unresolved(__LINE__);
    out.detail=mass_field(show.front(),0,3);
    uintptr_t setter=0;
    if(!read_range_ptr(r,out.detail_table+mass_field(show.front(),5,3),&setter) || !mass_entry(x,setter,visible_set) ||
       mass_field(setter,0)!=mass_field(setter,4)) return mass_shop_unresolved(__LINE__);
    out.visible=mass_field(setter,0);
    std::vector<uintptr_t> parent_candidates;
    for(auto candidate:find_item_contract(x,parent)) {
        const auto node=decode_adrp_ldr_global(candidate+16,candidate+28);
        const auto ui=decode_adrp_ldr_global(candidate+20,candidate+32);
        uintptr_t node_type=0,ui_type=0;
        if(read_range_ptr(r,node,&node_type) && node_type==out.node_type &&
           read_range_ptr(r,ui,&ui_type) && ui_type==out.ui_type) parent_candidates.push_back(candidate);
    }
    if(parent_candidates.size()!=1) return mass_shop_unresolved(__LINE__);
    uintptr_t getter=0;
    if(!read_range_ptr(r,out.detail_table+mass_field(parent_candidates.front(),1,3),&getter) ||
       !range_contains(x,getter,8) || (read_u32(getter)&0xFFC003FFU)!=0xF9400000U || read_u32(getter+4)!=0xD65F03C0U) return mass_shop_unresolved(__LINE__);
    out.parent=mass_field(getter,0,3);
    const auto enters=find_item_contract(x,node_enter),exits=find_item_contract(x,node_exit);
    if(enters.size()!=1 || exits.size()!=1 || mass_field(enters[0],16)!=mass_field(exits[0],5)) return mass_shop_unresolved(__LINE__);
    const auto enter_owner=injection_function_start(r,x,enters[0]),exit_owner=injection_function_start(r,x,exits[0]);
    uintptr_t enter_slot=0,exit_slot=0;
    if(!read_range_ptr(r,node_table+mass_field(enters[0],6,3),&enter_slot) || enter_slot!=enter_owner ||
       !read_range_ptr(r,node_table+mass_field(exits[0],10,3),&exit_slot) || exit_slot!=exit_owner) return mass_shop_unresolved(__LINE__);
    out.running=mass_field(enters[0],16);
    for(auto field:{out.visible,out.running}) {
        size_t count=0;
        for(const auto &[slot,function]:mass_virtuals(r,x,node_table)) {
            (void)slot;
            if(range_contains(x,function,8) && (read_u32(function)&0xFFC003FFU)==0x39400000U &&
               read_u32(function+4)==0xD65F03C0U && mass_field(function,0)==field) ++count;
        }
        if(count!=1) return mass_shop_unresolved(__LINE__);
    }
    const auto fronts=find_item_contract(x,front), updates=find_item_contract(x,state_update);
    if(fronts.size()!=1 || updates.size()!=1 || fronts[0]<20 || !mass_entry(x,fronts[0]-20,front_init) ||
       mass_field(fronts[0],6,3)!=mass_field(fronts[0],12,3)) return mass_shop_unresolved(__LINE__);
    out.states=mass_field(fronts[0],0,3);out.skip=mass_field(fronts[0],2);out.priority=mass_field(fronts[0],6,3);
    if(mass_pair(updates[0],0)!=static_cast<int64_t>(out.states)) return mass_shop_unresolved(__LINE__);
    const auto blocker_at=mass_owner_block(r,x,injection_function_start(r,x,fronts[0]),blocker);
    uintptr_t state_type=0,ui_type=0;
    if(!blocker_at || !read_range_ptr(r,decode_adrp_ldr_global(blocker_at,blocker_at+12),&state_type) || state_type!=out.state_type ||
       !read_range_ptr(r,decode_adrp_ldr_global(blocker_at+4,blocker_at+16),&ui_type) || ui_type!=out.ui_type) return mass_shop_unresolved(__LINE__);
    out.cast=mass_call(blocker_at,6);out.modal=mass_field(blocker_at,8,2);
    std::vector<uintptr_t> modal_writers;
    for(auto candidate:find_item_contract(x,modal_writer))
        if(mass_field(candidate,0,2)==out.modal && mass_field(candidate,6,2)==out.modal &&
           mass_field(candidate,13,2)==out.modal && mass_field(candidate,15,2)==out.modal) modal_writers.push_back(candidate);
    if(modal_writers.size()!=1) return mass_shop_unresolved(__LINE__);
    std::vector<uintptr_t> queries;
    for(auto candidate:find_item_contract(x,state_query))
        if(mass_pair(candidate,0)==static_cast<int64_t>(out.states)) queries.push_back(candidate);
    if(queries.size()!=1) return mass_shop_unresolved(__LINE__);
    const auto query=queries[0];
    std::vector<uintptr_t> managers,query_owners;
    const auto methods=mass_virtuals(r,x,out.state_table);
    for_each_snapshot_segment(x,[&](uintptr_t base,const uint8_t *bytes,size_t length) {
        for(size_t offset=8;offset+4<=length;offset+=4) {
            uint32_t instruction=0;std::memcpy(&instruction,bytes+offset,4);
            if((instruction&0xFC000000U)!=0x94000000U) continue;
            const auto pc=base+offset;
            if(mass_call(pc,0)!=query) continue;
            const auto owner=injection_function_start(r,x,pc);
            if(std::none_of(methods.begin(),methods.end(),[&](const auto &v){return v.second==owner;})) continue;
            if((read_u32(pc-4)&0xFFE0001FU)!=0x52800001U) continue;
            const auto manager=mass_call(pc-8,0);
            if(manager) {push_unique(managers,manager);push_unique(query_owners,owner);}
        }
    });
    if(managers.size()!=1 || query_owners.size()!=1) return mass_shop_unresolved(__LINE__);
    out.manager=managers[0];
    out.live=live; out.selection=g_object_layouts.shop.token.selection;
    if(!resolve_mass_lifetime(r,x,out)) return mass_shop_unresolved(__LINE__);
    out.valid=true;
    return out;
}

MassShopLayout resolve_mass_appraisal(const std::vector<MemoryRange> &r,const std::vector<MemoryRange> &x,
                                      uintptr_t purchase,const MassShopLayout &common) {
    using namespace mass_shop_contracts;
    if(!common.valid || !purchase || !g_object_layouts.shop.appraisal.valid) return {};
    MassShopLayout out=common;out.valid=false;out.live={};
    const auto hits=find_item_contract(x,appraisal_lookup);
    if(hits.size()!=1) return mass_shop_unresolved(__LINE__);
    const auto at=hits[0];out.binder=injection_function_start(r,x,at);
    if(mass_call(at,5)!=common.commodity_label) return mass_shop_unresolved(__LINE__);
    const auto copy_at=mass_owner_block(r,x,out.binder,appraisal_copy_bind);
    const auto refresh_at=mass_owner_block(r,x,out.binder,appraisal_refresh_call);
    const auto access=mass_owner_block(r,x,purchase,appraisal_accessors);
    if(!copy_at || !refresh_at || !access || mass_field(at,6)!=mass_field(at,11) ||
       mass_field(access,2,3)!=g_object_layouts.shop.appraisal.selection ||
       mass_field(access,4,3)!=mass_field(access,2,3) || mass_field(access,7,3)!=mass_field(access,2,3) ||
       mass_field(access,13,3)!=mass_field(access,2,3)) return mass_shop_unresolved(__LINE__);
    out.selection=g_object_layouts.shop.appraisal.selection;
    out.copy=mass_call(copy_at,5);out.move=mass_call(at,12);out.refresh=mass_call(refresh_at,1);
    out.lookup_after=at+40;out.result_from_fp=true;out.result_sp=mass_field(at,6);
    if(out.copy!=common.copy || out.move!=common.move) return mass_shop_unresolved(__LINE__);
    uintptr_t leaves[4]{};size_t inner=SIZE_MAX;
    const size_t calls[]={3,6,9,14};
    for(size_t i=0;i<4;++i) {
        const auto proxy=mass_call(access,calls[i]);
        if(!mass_entry(x,proxy,item_catalog::shop_proxy)) return mass_shop_unresolved(__LINE__);
        const auto field=mass_field(proxy,0,3);
        if(inner!=SIZE_MAX && field!=inner) return mass_shop_unresolved(__LINE__);
        inner=field;leaves[i]=mass_call(proxy,1,false);
    }
    size_t objects[3]{},controls[3]{};
    for(size_t i=0;i<3;++i) {
        if(!mass_entry(x,leaves[i],item_catalog::shop_ref)) return mass_shop_unresolved(__LINE__);
        objects[i]=mass_field(leaves[i],0,3);controls[i]=mass_field(leaves[i],1,3);
        if(controls[i]!=objects[i]+sizeof(uintptr_t)) return mass_shop_unresolved(__LINE__);
    }
    if(!mass_entry(x,leaves[3],item_catalog::shop_quantity)) return mass_shop_unresolved(__LINE__);
    out.live.item=objects[0];out.live.control=controls[0];out.live.currency=objects[1];
    out.live.currency_control=controls[1];out.live.commodity=objects[2];out.live.quantity=mass_field(leaves[3],0,2);
    if(mass_field(at,10)!=out.live.item || mass_field(copy_at,0)!=out.live.commodity) return mass_shop_unresolved(__LINE__);
    const auto head=mass_owner_block(r,x,out.refresh,appraisal_refresh_head);
    const auto cost=mass_owner_block(r,x,out.refresh,appraisal_refresh_cost);
    if(!head || !cost || mass_field(head,0,3)!=out.live.item || mass_field(head,2,3)!=out.live.currency ||
       mass_field(cost,0,3)!=out.live.currency || mass_field(cost,2,3)!=g_object_layouts.amount_slot ||
       mass_field(cost,5,3)!=out.live.commodity || mass_field(cost,10,3)!=out.live.commodity ||
       mass_field(cost,8,2)!=out.live.quantity || mass_call(cost,6)!=mass_call(access,11)) return mass_shop_unresolved(__LINE__);
    out.cost=mass_call(cost,6);
    out.detail_table=unique_primary_vtable(r,x,"N6toybox3gui31AppraisalExchangeShopDetailViewE");
    out.list_table=unique_primary_vtable(r,x,"N6toybox3gui30AppraisalExchangeShopListLayerE");
    out.state_table=unique_primary_vtable(r,x,"N6toybox3gui28AppraisalExchangeShopUIStateE");
    uintptr_t dtor=0;
    if(!out.detail_table || !out.list_table || !out.state_table || !read_range_ptr(r,out.detail_table,&dtor)) return mass_shop_unresolved(__LINE__);
    const auto cleanup=mass_owner_block(r,x,injection_function_start(r,x,dtor),dtor_bind);
    if(!cleanup || mass_address(cleanup,0,1)!=out.detail_table || mass_field(cleanup,4)!=out.live.currency ||
       mass_field(cleanup,6)!=out.live.item || mass_field(cleanup,8)!=out.live.commodity ||
       mass_call(cleanup,9)!=out.destroy) return mass_shop_unresolved(__LINE__);
    std::vector<uintptr_t> show;
    for(auto candidate:find_item_contract(x,show_detail))
        if(mass_call(candidate,2)==out.binder && mass_field(candidate,0,3)==mass_field(candidate,3,3)) show.push_back(candidate);
    if(show.size()!=1 || inner!=mass_field(show[0],0,3)) return mass_shop_unresolved(__LINE__);
    out.detail=inner;out.live.valid=true;
    if(!resolve_mass_lifetime(r,x,out)) return mass_shop_unresolved(__LINE__);
    out.valid=true;
    return out;
}
