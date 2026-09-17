#include "item_fish_contracts.h"

// Fish sampling and storage are independent of ordinary Item::changeAmount.
// Every binding is immutable within this loaded image and obtained from a
// unique semantic owner/call edge. No sampled fish or inventory owner is cached.
struct FishLayout {
    bool ready=false;
    uintptr_t pool_ctor=0,pool_destroy=0,pool_draw=0,cache_destroy=0,groups=0,add_row=0;
    uintptr_t integer=0,float_get=0,string_get=0,decode=0,seed_repo=0,seed_pop=0;
    uintptr_t species_repo=0,label_find=0,size_table=0;
    size_t pool_size=0,pool_rng=0,pool_sum=0,pool_id=0,pool_result_size=0,pool_signature=0;
    size_t cache_size=0,cache_root=0,group_id=0,group_label=0,weights[3]{},species_master=0,species_id=0;
    size_t species_map=0,species_payload=0,size_group=0,size_scale=0,seed_field=0;
    uintptr_t species_slot=0,hash_unlink=0,node_dispose=0,user_destroy=0;
    uintptr_t root_slot=0,repo_slot=0,box_repo_slot=0,root_get=0,repo_get=0,box_repo_get=0;
    uintptr_t user_fishing_get=0,last_id_set=0,user_factory=0,stock_id_set=0,fish_id_set=0;
    uintptr_t size_set=0,reserved_set=0,signature_set=0,domain_factory=0,repo_append=0,repo_remove=0;
    uintptr_t fbs_push=0,fbs_vector_get=0,user_append=0,map_find=0,map_erase=0,user_copy=0,user_move=0;
    uintptr_t owned_box=0,float_decode=0,bool_decode=0,signature_decode=0;
    size_t root_fbs=0,user_fishing_pair=0,last_id=0,stock_id=0,fish_id=0,size=0,reserved=0,signature=0;
    size_t live=0,deleted=0,map=0,dirty=0,node_userdata=0,domain_userdata=0,repo_vector=0,box_master=0,box_capacity=0,catch_count=0;
};
FishLayout g_fish;

template<size_t N>
uintptr_t fish_capture(uintptr_t at,const fish_contracts::Word (&p)[N],fish_contracts::Role role) {
    if(at) for(size_t i=0;i<N;++i) if(p[i].role==role) return at+4*i;
    return 0;
}
template<size_t N>
uintptr_t fish_call(uintptr_t at,const fish_contracts::Word (&p)[N],fish_contracts::Role role) {
    uintptr_t target=0;
    const auto pc=fish_capture(at,p,role);
    return pc && decode_branch_target(pc,true,&target) ? target : 0;
}
template<size_t N>
size_t fish_field(uintptr_t at,const fish_contracts::Word (&p)[N],fish_contracts::Role role,unsigned scale=0) {
    const auto pc=fish_capture(at,p,role);
    return pc ? item_catalog::unsigned_offset(read_u32(pc),scale) : SIZE_MAX;
}
template<size_t N>
size_t fish_mov(uintptr_t at,const fish_contracts::Word (&p)[N],fish_contracts::Role role) {
    const auto pc=fish_capture(at,p,role);
    return pc ? (read_u32(pc)>>5U)&0xffffU : SIZE_MAX;
}
struct FishResolver {
    const std::vector<MemoryRange> &r,&x;
    template<size_t N>
    std::vector<uintptr_t> sites(uintptr_t owner,const fish_contracts::Word (&p)[N]) const {
        item_catalog::Word words[N]{};
        for(size_t i=0;i<N;++i) words[i]={p[i].value,p[i].mask,item_catalog::Role::none};
        std::vector<MemoryRange> scope;
        if(!owner) scope=x;
        else for(const auto &range:x) {
            const auto lo=std::max(range.start,owner),hi=std::min(range.end,owner+65536);
            if(hi>lo) scope.push_back({lo,hi,true,false});
        }
        auto hits=find_item_contract(scope,words);
        hits.erase(std::remove_if(hits.begin(),hits.end(),[&](uintptr_t at) {
            const auto f=injection_function_start(r,x,at);
            return !f || (owner && f!=owner) || injection_function_start(r,x,at+4*(N-1))!=f;
        }),hits.end());
        return hits;
    }
    template<size_t N> uintptr_t one(uintptr_t owner,const fish_contracts::Word (&p)[N]) const {
        if(!owner) return 0;
        const auto hits=sites(owner,p);return hits.size()==1 ? hits.front() : 0;
    }
    template<size_t N> uintptr_t any(const fish_contracts::Word (&p)[N]) const {
        const auto hits=sites(0,p);return hits.size()==1 ? hits.front() : 0;
    }
    uintptr_t anchor(const char *s) const {return injection_anchored_function(r,x,s);}
    uintptr_t owner(uintptr_t at) const {return injection_function_start(r,x,at);}
    template<size_t N> uintptr_t slot(uintptr_t fn,const fish_contracts::Word (&p)[N]) const {
        using fish_contracts::Role;
        const auto at=one(fn,p);
        const auto result=at ? decode_adrp_ldr_global(fish_capture(at,p,Role::slot_page),fish_capture(at,p,Role::slot_load)) : 0;
        return injection_writable_image_slot(r,result) ? result : 0;
    }
};

FishLayout resolve_fish(const std::vector<MemoryRange> &r,const std::vector<MemoryRange> &x) {
    using namespace fish_contracts;
    FishLayout out;const FishResolver c{r,x};
    const auto unresolved=[&](int line) {
#if defined(AE_TEST_SYMBOL_RESOLVER) || defined(AE_DEVICE_LAB)
        std::fprintf(stderr,"FISH_UNRESOLVED %d\n",line);
#endif
        return FishLayout{};
    };
    const auto place=c.anchor("bool toybox::DomainFishingPlace::updateRespawnState()");
    out.pool_draw=c.anchor("bool toybox::FishPoolLottery::lottery(FishSizeLotteryFarmCache &)");
    const auto size_draw=c.anchor("float toybox::FishSizeLotteryFarm::getFishSize(float, int)");
    const auto init=c.anchor("void toybox::DomainFishingRepository::init()");
    out.seed_pop=c.anchor("std::shared_ptr<DomainRandomSeed> toybox::DomainRandomSeedRepository::pop()");
    if(!place || !out.pool_draw || !size_draw || !init || !out.seed_pop) return unresolved(__LINE__);
    const auto ctor_site=c.one(place,pool_pool_ctor_from_place);
    out.pool_ctor=fish_call(ctor_site,pool_pool_ctor_from_place,Role::ctor);
    out.pool_size=fish_field(ctor_site,pool_pool_ctor_from_place,Role::pool_size);
    if(!out.pool_ctor || out.pool_ctor!=fish_call(ctor_site,pool_pool_ctor_from_place,Role::ctor2) ||
       out.pool_ctor!=fish_call(ctor_site,pool_pool_ctor_from_place,Role::ctor3) || out.pool_size>4096) return unresolved(__LINE__);
    const auto ctor=c.one(out.pool_ctor,pool_pool_constructor),group=c.one(init,pool_group_table_from_init);
    const auto weights=c.one(place,pool_pool_weights_and_add),label=c.one(out.pool_draw,pool_pool_group_label);
    const auto species=c.one(out.pool_draw,pool_pool_species_lookup),size=c.one(out.pool_draw,pool_pool_size_metadata);
    const auto result=c.one(out.pool_draw,pool_pool_result_id),result2=c.one(out.pool_draw,pool_pool_result_size_signature);
    const auto size_rng=c.one(size_draw,pool_size_rng_constructor),size_rows=c.one(size_draw,pool_size_table_rows);
    const auto cleanup=c.one(place,pool_normal_cleanup),cache_init=c.one(place,pool_cache_init);
    if(!ctor || !group || !weights || !label || !species || !size || !result || !result2 || !size_rng ||
       !size_rows || !cleanup || !cache_init) return unresolved(__LINE__);
    out.integer=fish_call(group,pool_group_table_from_init,Role::int_getter);
    out.float_get=fish_call(size,pool_pool_size_metadata,Role::float_getter);
    out.decode=fish_call(result,pool_pool_result_id,Role::id_decoder);
    if(!out.integer || !out.float_get || !out.decode ||
       fish_call(weights,pool_pool_weights_and_add,Role::int_getter1)!=out.integer ||
       fish_call(weights,pool_pool_weights_and_add,Role::int_getter2)!=out.integer ||
       fish_call(weights,pool_pool_weights_and_add,Role::int_getter3)!=out.integer ||
       fish_call(size,pool_pool_size_metadata,Role::int_getter)!=out.integer ||
       fish_call(size,pool_pool_size_metadata,Role::size_draw)!=size_draw ||
       fish_call(result2,pool_pool_result_size_signature,Role::int_getter)!=out.integer ||
       fish_call(size_rows,pool_size_table_rows,Role::int_getter)!=out.integer ||
       fish_call(size_rows,pool_size_table_rows,Role::int_getter2)!=out.integer ||
       fish_call(size_rows,pool_size_table_rows,Role::float_getter)!=out.float_get) return unresolved(__LINE__);
    const auto rng_ctor=fish_call(ctor,pool_pool_constructor,Role::rng_ctor);
    const auto size_rng_ctor=fish_call(size_rng,pool_size_rng_constructor,Role::rng_ctor);
    const auto pop=c.one(rng_ctor,pool_rng_pop),size_pop=c.one(size_rng_ctor,pool_rng_pop);
    out.seed_repo=fish_call(pop,pool_rng_pop,Role::repository);out.seed_field=fish_mov(pop,pool_rng_pop,Role::seed_field);
    if(!pop || !size_pop || !out.seed_repo || fish_call(pop,pool_rng_pop,Role::pop)!=out.seed_pop ||
       fish_call(size_pop,pool_rng_pop,Role::pop)!=out.seed_pop || fish_call(size_pop,pool_rng_pop,Role::repository)!=out.seed_repo ||
       fish_mov(size_pop,pool_rng_pop,Role::seed_field)!=out.seed_field ||
       fish_mov(ctor,pool_pool_constructor,Role::rng_allocation_size)!=fish_mov(size_rng,pool_size_rng_constructor,Role::rng_allocation_size) ||
       fish_mov(ctor,pool_pool_constructor,Role::rng_kind)!=fish_mov(size_rng,pool_size_rng_constructor,Role::rng_kind)) return unresolved(__LINE__);
    out.pool_destroy=fish_call(cleanup,pool_normal_cleanup,Role::pool_destroy);
    out.cache_destroy=fish_call(cleanup,pool_normal_cleanup,Role::cache_destroy);
    if(!out.pool_destroy || !out.cache_destroy || out.pool_destroy!=fish_call(cleanup,pool_normal_cleanup,Role::pool_destroy2) ||
       out.pool_destroy!=fish_call(cleanup,pool_normal_cleanup,Role::pool_destroy3) ||
       out.pool_size!=fish_mov(cleanup,pool_normal_cleanup,Role::pool_size1) ||
       out.pool_size!=fish_mov(cleanup,pool_normal_cleanup,Role::pool_size2)) return unresolved(__LINE__);
    const auto cache_dtor=c.one(out.cache_destroy,pool_cache_destroy_body),pool_dtor=c.one(out.pool_destroy,pool_pool_destroy_body);
    const auto rng_destroy=fish_call(pool_dtor,pool_pool_destroy_body,Role::rng_destroy);
    const auto rng_dtor=c.one(rng_destroy,pool_rng_destroy_seed);
    out.pool_rng=fish_field(ctor,pool_pool_constructor,Role::rng_field,3);
    if(!cache_dtor || !pool_dtor || !rng_dtor || fish_call(cache_dtor,pool_cache_destroy_body,Role::self1)!=out.cache_destroy ||
       fish_call(cache_dtor,pool_cache_destroy_body,Role::self2)!=out.cache_destroy ||
       fish_call(rng_dtor,pool_rng_destroy_seed,Role::repository)!=out.seed_repo ||
       fish_field(rng_dtor,pool_rng_destroy_seed,Role::seed_field,3)!=out.seed_field ||
       fish_field(rng_dtor,pool_rng_destroy_seed,Role::seed_object,3)!=out.seed_field ||
       fish_field(rng_dtor,pool_rng_destroy_seed,Role::seed_control,3)!=out.seed_field+8 ||
       fish_field(ctor,pool_pool_constructor,Role::publish_rng,3)!=out.pool_rng ||
       fish_field(pool_dtor,pool_pool_destroy_body,Role::rng_field,3)!=out.pool_rng ||
       fish_field(result2,pool_pool_result_size_signature,Role::rng_field,3)!=out.pool_rng ||
       fish_field(result2,pool_pool_result_size_signature,Role::rng_seed_field,3)!=out.seed_field) return unresolved(__LINE__);
    const auto cache_stack=fish_field(cache_init,pool_cache_init,Role::cache_stack);
    const auto cache_root_stack=fish_field(cleanup,pool_normal_cleanup,Role::cache_root_stack,3);
    if(cache_root_stack<cache_stack || cache_stack!=fish_field(cleanup,pool_normal_cleanup,Role::cache_stack) ||
       cache_stack!=fish_field(cache_init,pool_cache_init,Role::begin_stack,3)) return unresolved(__LINE__);
    out.cache_root=cache_root_stack-cache_stack;
    // The native tree header consists of begin, root and size; these positions
    // are also confirmed by the contiguous STP initialization contract.
    out.cache_size=out.cache_root+2*sizeof(uintptr_t);
    if(out.cache_root!=fish_field(cache_init,pool_cache_init,Role::root_member) || out.cache_root!=sizeof(uintptr_t) ||
       fish_field(cache_dtor,pool_cache_destroy_body,Role::value)!=fish_field(size,pool_pool_size_metadata,Role::cache_value)) return unresolved(__LINE__);
    out.pool_sum=fish_field(ctor,pool_pool_constructor,Role::rate_sum_field,2);
    out.pool_id=fish_field(result,pool_pool_result_id,Role::result_secure_id);
    out.pool_result_size=fish_field(result2,pool_pool_result_size_signature,Role::result_size,2);
    out.pool_signature=fish_field(result2,pool_pool_result_size_signature,Role::result_signature,2);
    out.groups=fish_call(group,pool_group_table_from_init,Role::table_accessor);
    out.add_row=fish_call(weights,pool_pool_weights_and_add,Role::add_row);
    out.string_get=fish_call(label,pool_pool_group_label,Role::string_decoder);
    out.group_id=fish_mov(group,pool_group_table_from_init,Role::group_id_tag);
    out.group_label=fish_field(label,pool_pool_group_label,Role::label_tag,1);
    out.weights[0]=fish_mov(weights,pool_pool_weights_and_add,Role::weight_tag1);
    out.weights[1]=fish_mov(weights,pool_pool_weights_and_add,Role::weight_tag2);
    out.weights[2]=fish_mov(weights,pool_pool_weights_and_add,Role::weight_tag3);
    out.species_repo=fish_call(species,pool_pool_species_lookup,Role::species_repository);
    const auto species_owner=c.one(out.pool_draw,pool_species_owner);
    const auto species_pair=species_owner ? item_catalog::signed_bits((read_u32(species_owner)>>15U)&127U,7)*8 : -1;
    if(species_pair<0) return unresolved(__LINE__);
    out.species_payload=static_cast<size_t>(species_pair);
    out.species_slot=c.slot(out.species_repo,local_lazy_repo_slot);
    if(!out.species_slot) return unresolved(__LINE__);
    out.label_find=fish_call(species,pool_pool_species_lookup,Role::label_map_find);
    out.species_map=fish_field(species,pool_pool_species_lookup,Role::species_label_map);
    out.species_master=fish_field(size,pool_pool_size_metadata,Role::species_master,3);
    out.species_id=fish_field(result,pool_pool_result_id,Role::species_secure_id);
    out.size_group=fish_mov(size,pool_pool_size_metadata,Role::size_table_tag);
    out.size_scale=fish_mov(size,pool_pool_size_metadata,Role::size_scale_tag);
    out.size_table=fish_call(size_rng,pool_size_rng_constructor,Role::size_table_accessor);
    if(out.pool_sum!=fish_field(ctor,pool_pool_constructor,Role::rate_sum_field2,2) ||
       out.pool_signature!=fish_field(ctor,pool_pool_constructor,Role::signature_field,2) ||
       out.pool_result_size!=fish_field(ctor,pool_pool_constructor,Role::size_and_type_field,3) ||
       fish_field(ctor,pool_pool_constructor,Role::typed_id_field)!=fish_field(result,pool_pool_result_id,Role::typed_id_field) ||
       out.species_master!=fish_field(size,pool_pool_size_metadata,Role::species_master2,3) ||
       std::max({out.pool_sum,out.pool_id,out.pool_result_size,out.pool_signature})+4>out.pool_size) return unresolved(__LINE__);

    const auto regist=c.anchor("DomainFishStockRepository: not found masterdata {}");
    const auto push=c.anchor("_fishStockMap.find(value->id()) == _fishStockMap.end()");
    out.domain_factory=c.anchor("static std::shared_ptr<DomainFishStock> toybox::DomainFishStock::createShared(const std::shared_ptr<userdata::UserFishStock>)");
    if(!regist || !push || !out.domain_factory) return unresolved(__LINE__);
    const auto storage=c.one(regist,storage_storage_create),finish=c.one(regist,storage_domain_finish);
    const auto repo_capacity=c.any(storage_repo_capacity);
    const auto capacity=c.one(c.owner(repo_capacity),storage_capacity),append=c.one(push,storage_fbs_append);
    if(!storage || !finish || !repo_capacity || !capacity || !append ||
       fish_call(finish,storage_domain_finish,Role::domain_factory)!=out.domain_factory ||
       fish_call(storage,storage_storage_create,Role::master_integer_read)!=out.integer ||
       fish_call(capacity,storage_capacity,Role::master_integer_read)!=out.integer ||
       fish_call(append,storage_fbs_append,Role::secure_int_get)!=out.decode ||
       fish_call(storage,storage_storage_create,Role::secure_int_get)!=out.decode) return unresolved(__LINE__);
    out.repo_get=fish_call(repo_capacity,storage_repo_capacity,Role::fish_repo_get);
    out.box_repo_get=fish_call(capacity,storage_capacity,Role::fish_item_repo_get);
    const auto repo_pair=read_u32(fish_capture(repo_capacity,storage_repo_capacity,Role::repo_vector_field));
    const auto repo_field=item_catalog::signed_bits((repo_pair>>15U)&127U,7)*8;
    if(repo_field<0) return unresolved(__LINE__);
    out.repo_vector=static_cast<size_t>(repo_field);
    out.owned_box=fish_call(capacity,storage_capacity,Role::owned_box_get);
    out.box_master=fish_field(capacity,storage_capacity,Role::box_master_field,3);
    out.box_capacity=fish_mov(capacity,storage_capacity,Role::capacity_schema_field);
    out.user_fishing_get=fish_call(storage,storage_storage_create,Role::user_fishing_get);
    out.last_id_set=fish_call(storage,storage_storage_create,Role::last_id_set);
    out.user_factory=fish_call(storage,storage_storage_create,Role::user_stock_factory);
    out.stock_id_set=fish_call(storage,storage_storage_create,Role::stock_id_set);
    out.fish_id_set=fish_call(storage,storage_storage_create,Role::fish_id_set);
    out.size_set=fish_call(storage,storage_storage_create,Role::size_set);
    out.reserved_set=fish_call(storage,storage_storage_create,Role::reserved_set);
    out.signature_set=fish_call(storage,storage_storage_create,Role::signature_set);
    out.fbs_push=fish_call(storage,storage_storage_create,Role::fbs_push_wrapper);
    out.repo_append=fish_call(finish,storage_domain_finish,Role::repo_append);
    out.user_append=fish_call(append,storage_fbs_append,Role::user_vector_push);
    out.map_find=fish_call(append,storage_fbs_append,Role::id_map_find);
    out.live=fish_mov(append,storage_fbs_append,Role::live_vector_field);
    out.map=fish_mov(append,storage_fbs_append,Role::id_map_field);
    const auto pair=c.one(out.user_fishing_get,local_user_fishing_get_pair);
    out.root_get=fish_call(pair,local_user_fishing_get_pair,Role::root_get);
    out.user_fishing_pair=fish_field(pair,local_user_fishing_get_pair,Role::userdata_pair,3);
    uintptr_t root_real=0;
    if(!pair || !decode_branch_target(out.root_get,false,&root_real) ||
       fish_field(pair,local_user_fishing_get_pair,Role::userdata_control,3)!=out.user_fishing_pair+8) return unresolved(__LINE__);
    out.root_slot=c.slot(root_real,local_lazy_root_slot);out.repo_slot=c.slot(out.repo_get,local_lazy_repo_slot);
    out.box_repo_slot=c.slot(out.box_repo_get,local_lazy_repo_slot);
    const auto wrapper=removal_contract(r,x,out.fbs_push,removal_code_size(item_removal_contracts::userdata_erase_wrapper),item_removal_contracts::userdata_erase_wrapper);
    if(!out.root_slot || !out.repo_slot || !out.box_repo_slot || !wrapper ||
       removal_call(wrapper,item_removal_contracts::userdata_erase_wrapper,item_removal_contracts::Role::manager)!=out.root_get ||
       removal_call(wrapper,item_removal_contracts::userdata_erase_wrapper,item_removal_contracts::Role::remove)!=push) return unresolved(__LINE__);
    const auto getter_fields=[&](uintptr_t fn,const auto &pattern,Role field) {
        const auto at=c.one(fn,pattern);return fish_field(at,pattern,field);
    };
    out.last_id=getter_fields(out.last_id_set,local_integer_setter_fields,Role::secure_field);
    out.stock_id=getter_fields(out.stock_id_set,local_integer_setter_fields,Role::secure_field);
    out.fish_id=getter_fields(out.fish_id_set,local_integer_setter_fields,Role::secure_field);
    out.signature=getter_fields(out.signature_set,local_integer_setter_fields,Role::secure_field);
    out.signature_decode=fish_call(c.one(out.signature_set,local_integer_setter_fields),local_integer_setter_fields,Role::secure_get);
    const auto size_set=c.one(out.size_set,local_float_setter_fields),reserved=c.one(out.reserved_set,local_bool_setter_fields);
    out.size=fish_field(size_set,local_float_setter_fields,Role::secure_field);
    out.reserved=fish_field(reserved,local_bool_setter_fields,Role::secure_field);
    out.float_decode=fish_call(size_set,local_float_setter_fields,Role::secure_float_get);
    out.bool_decode=fish_call(reserved,local_bool_setter_fields,Role::secure_bool_get);
    const auto dirty=c.one(push,local_fbs_dirty_field),node=c.one(push,local_fbs_node_userdata);
    out.dirty=fish_mov(dirty,local_fbs_dirty_field,Role::dirty_field);
    out.node_userdata=fish_field(node,local_fbs_node_userdata,Role::node_userdata_field);
    out.user_copy=fish_call(node,local_fbs_node_userdata,Role::pair_copy);
    if(!size_set || !reserved || !dirty || !node || out.last_id!=fish_field(storage,storage_storage_create,Role::last_id_field) ||
       out.stock_id!=fish_field(append,storage_fbs_append,Role::stock_id_field) ||
       std::max({out.last_id,out.stock_id,out.fish_id,out.signature,out.size,out.reserved,out.node_userdata})>4096) return unresolved(__LINE__);
    std::vector<uintptr_t> detaches;
    for(auto at:c.sites(0,storage_detach_lookup)) {
        const auto getter=fish_call(at,storage_detach_lookup,Role::fbs_vector_get);
        const auto local=c.one(getter,local_fbs_vector_get);
        if(local && fish_mov(local,local_fbs_vector_get,Role::live_vector_field)==out.live &&
           fish_call(local,local_fbs_vector_get,Role::root_get)==out.root_get) detaches.push_back(at);
    }
    if(detaches.size()!=1) return unresolved(__LINE__);
    const auto detach_site=detaches.front(),detach=c.owner(detach_site);
    const auto user_field=item_catalog::signed_bits((read_u32(detach_site)>>12U)&511U,9);
    if(user_field<0) return unresolved(__LINE__);
    out.domain_userdata=static_cast<size_t>(user_field);
    out.fbs_vector_get=fish_call(detach_site,storage_detach_lookup,Role::fbs_vector_get);
    const auto vector=c.one(out.fbs_vector_get,local_fbs_vector_get);
    out.root_fbs=fish_field(vector,local_fbs_vector_get,Role::root_fbs_field,3);
    const auto erase=c.one(detach,storage_detach_erase);
    out.user_move=fish_call(erase,storage_detach_erase,Role::user_pair_move);
    const auto del_wrapper=fish_call(erase,storage_detach_erase,Role::fbs_delete_wrapper);
    const auto del_site=removal_contract(r,x,del_wrapper,removal_code_size(item_removal_contracts::userdata_erase_wrapper),item_removal_contracts::userdata_erase_wrapper);
    const auto del=removal_call(del_site,item_removal_contracts::userdata_erase_wrapper,item_removal_contracts::Role::remove);
    const auto deletion=c.one(del,storage_fbs_delete),del_map=c.one(del,local_fbs_delete_map);
    out.map_erase=fish_call(deletion,storage_fbs_delete,Role::id_map_erase);
    out.deleted=fish_mov(deletion,storage_fbs_delete,Role::tombstone_vector_field);
    if(!erase || !del_site || !deletion || !del_map ||
       removal_call(del_site,item_removal_contracts::userdata_erase_wrapper,item_removal_contracts::Role::manager)!=out.root_get ||
       fish_mov(del_map,local_fbs_delete_map,Role::id_map_field)!=out.map ||
       fish_call(del_map,local_fbs_delete_map,Role::map_erase)!=out.map_erase ||
       fish_call(deletion,storage_fbs_delete,Role::user_vector_push)!=out.user_append ||
       fish_call(deletion,storage_fbs_delete,Role::secure_int_get)!=out.decode ||
       fish_field(deletion,storage_fbs_delete,Role::live_vector_end_field,3)!=out.live+sizeof(uintptr_t)) return unresolved(__LINE__);
    std::vector<uintptr_t> removers;
    for(auto at:c.sites(0,storage_repo_remove))
        if(fish_call(at,storage_repo_remove,Role::domain_detach)==detach) push_unique(removers,c.owner(at));
    if(removers.size()!=1) return unresolved(__LINE__);
    out.repo_remove=removers.front();
    const auto catches=c.one(regist,storage_catch_count);
    out.catch_count=fish_field(catches,storage_catch_count,Role::secure_field);
    const auto catch_setter=fish_call(catches,storage_catch_count,Role::secure_set);
    if(!catches || out.catch_count!=getter_fields(catch_setter,local_integer_setter_fields,Role::secure_field) ||
       fish_call(catches,storage_catch_count,Role::secure_int_get)!=out.decode || out.catch_count==out.last_id ||
       out.seed_repo!=g_injection_layout.other_resources) return unresolved(__LINE__);
    const auto map_erase=c.one(out.map_erase,storage_map_erase_body);
    out.hash_unlink=fish_call(map_erase,storage_map_erase_body,Role::unlink);
    out.node_dispose=fish_call(map_erase,storage_map_erase_body,Role::dispose);
    const auto holder=removal_contract(r,x,out.node_dispose,256,item_removal_contracts::pet_holder_dtor);
    out.user_destroy=removal_call(holder,item_removal_contracts::pet_holder_dtor,item_removal_contracts::Role::destructor);
    if(!map_erase || !holder || fish_call(map_erase,storage_map_erase_body,Role::id_map_find)!=out.map_find ||
       removal_add(holder,item_removal_contracts::pet_holder_dtor,item_removal_contracts::Role::payload)!=out.node_userdata ||
       !removal_contract(r,x,out.hash_unlink,removal_code_size(item_removal_contracts::hash_unlink_body),item_removal_contracts::hash_unlink_body)) return unresolved(__LINE__);
    out.ready=true;return out;
}
