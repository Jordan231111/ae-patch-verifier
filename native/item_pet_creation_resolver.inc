#include "item_pet_creation_contracts.h"
struct PetCreationLayout {
    uintptr_t inner=0;
    size_t basic=0,last_id=0,species_tree=0;
    bool ready=false;
};
PetCreationLayout g_pet_creation;
PetCreationLayout resolve_pet_creation(const std::vector<MemoryRange> &r,const std::vector<MemoryRange> &x) {
    using namespace item_removal_contracts;
    PetCreationLayout out;
    const auto &s=g_pet_storage;
    const auto &pet=g_instance_layouts[static_cast<size_t>(InstanceKind::Pet)];
    if(!s.ready || !pet.ready || !pet.create_one) return out;
    const auto one=[&](uintptr_t f,const auto &p) {return f ? removal_contract(r,x,f,4096,p) : 0;};
    const auto outer=one(pet.create_one,pet_create_inner);
    out.inner=removal_call(outer,pet_create_inner,Role::inner);
    if(!out.inner) return out;
    const auto counter=one(out.inner,pet_create_counter),increment=one(out.inner,pet_create_increment);
    const auto getter=removal_call(counter,pet_create_counter,Role::accessor);
    const auto basic=getter ? one(getter,pet_create_basic_pair) : 0;
    const auto main=one(out.inner,pet_create_main),ids=one(out.inner,pet_create_id_index);
    const auto species=one(out.inner,pet_create_species_index),fbs=one(out.inner,pet_create_fbs);
    if(!counter || !increment || !basic || !main || !ids || !species || !fbs ||
       removal_call(increment,pet_create_increment,Role::accessor)!=getter ||
       removal_call(basic,pet_create_basic_pair,Role::manager)!=s.manager ||
       removal_call(counter,pet_create_counter,Role::decoder)!=pet.decoder ||
       removal_call(main,pet_create_main,Role::append)!=removal_call(species,pet_create_species_index,Role::append) ||
       removal_call(ids,pet_create_id_index,Role::assign)!=s.repository.assign ||
       removal_add(ids,pet_create_id_index,Role::map_field)!=s.repository.ids ||
       removal_add(ids,pet_create_id_index,Role::payload)!=s.repository.payload ||
       removal_add(species,pet_create_species_index,Role::payload)!=pet.count.vector ||
       s.repository.live!=0 || !(counter<increment && increment<main && main<ids && ids<species && species<fbs)) return out;
    const auto pair=read_u32(removal_capture(basic,pet_create_basic_pair,Role::userdata_field));
    const auto pair_offset=item_catalog::signed_bits((pair>>15U)&127U,7)*8;
    if(pair_offset<0 || removal_slot(basic,pet_create_basic_pair,Role::source_field)!=0) return out;
    out.basic=static_cast<size_t>(pair_offset);
    out.last_id=removal_add(counter,pet_create_counter,Role::secure_field);
    const auto counter_setter=removal_call(increment,pet_create_increment,Role::setter);
    const auto setter=one(counter_setter,pet_create_counter_setter);
    if(!setter || removal_add(setter,pet_create_counter_setter,Role::secure_field)!=out.last_id ||
       removal_call(setter,pet_create_counter_setter,Role::decoder)!=pet.decoder) return out;
    out.species_tree=removal_add(species,pet_create_species_index,Role::map_field);
    if(out.last_id>4096 || s.repository.root<s.repository.ids ||
       out.species_tree+s.repository.root-s.repository.ids!=pet.count.root) return out;
    const auto writer=removal_call(fbs,pet_create_fbs,Role::wrapper);
    const auto wrapper=writer ? removal_contract(r,x,writer,removal_code_size(userdata_erase_wrapper),userdata_erase_wrapper) : 0;
    const auto push=injection_anchored_function(r,x,"void toybox::userdata::UserDataFBS::pushBackPetEquipmentStock(const std::shared_ptr<UserPetEquipmentStock> &)");
    if(!wrapper || !push || removal_call(wrapper,userdata_erase_wrapper,Role::manager)!=s.manager ||
       removal_call(wrapper,userdata_erase_wrapper,Role::remove)!=push) return out;
    out.ready=true;return out;
}
