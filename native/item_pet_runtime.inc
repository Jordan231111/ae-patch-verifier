// Pet's shipped removal function destroys its lookup index before querying it.
// Resolve the complete authoritative userdata transaction and host typed helpers.
template<size_t N>
size_t removal_mov(uintptr_t at, const item_removal_contracts::Word (&pattern)[N], item_removal_contracts::Role role) {
    const uintptr_t pc = removal_capture(at, pattern, role);
    return pc ? (read_u32(pc) >> 5U) & 0xffffU : SIZE_MAX;
}
std::vector<uintptr_t> removal_direct_calls(const std::vector<MemoryRange> &r, const std::vector<MemoryRange> &x,
                                         uintptr_t function, size_t limit) {
    std::vector<uintptr_t> calls;
    for (uintptr_t pc = function; function && pc < function + limit && range_contains(x, pc, 4); pc += 4) {
        if (injection_function_start(r, x, pc) != function) break;
        uintptr_t target = 0;
        if (decode_branch_target(pc, true, &target)) push_unique(calls, target);
    }
    return calls;
}

PetStorageLayout resolve_pet_storage(const std::vector<MemoryRange> &r, const std::vector<MemoryRange> &x,
                                    const InstanceLayout &pet, const InstanceLayout &buddy) {
    using namespace item_removal_contracts;
    PetStorageLayout out;
    if (!pet.remove || !pet.decoder || !pet.instance_secure || !buddy.ready) return out;
    auto contract = [&](uintptr_t f, size_t n, const auto &p) { return removal_contract(r, x, f, n, p); };
    const auto ctor_site = contract(pet.repository, 256, pet_repo_ctor);
    const auto ctor = removal_call(ctor_site, pet_repo_ctor, Role::constructor);
    const auto reload_site = ctor ? contract(ctor, 512, pet_ctor_reload) : 0;
    out.reload = removal_call(reload_site, pet_ctor_reload, Role::reload);
    const auto accessor_site = out.reload ? contract(out.reload, 2048, pet_reload_accessor) : 0;
    const auto ids = out.reload ? contract(out.reload, 2048, pet_reload_ids) : 0;
    if (!accessor_site || !ids || removal_call(ids, pet_reload_ids, Role::decoder) != pet.decoder ||
        removal_call(ids, pet_reload_ids, Role::copy2) != pet.decoder ||
        removal_add(ids, pet_reload_ids, Role::secure_field) != removal_add(ids, pet_reload_ids, Role::source_field)) return out;
    out.userdata_id = removal_add(ids, pet_reload_ids, Role::secure_field);
    out.accessor = removal_call(accessor_site, pet_reload_accessor, Role::accessor);
    const auto accessor = contract(out.accessor, removal_code_size(pet_accessor), pet_accessor);
    if (!accessor) return out;
    out.manager = removal_call(accessor, pet_accessor, Role::manager);
    out.live = removal_mov(accessor, pet_accessor, Role::live_field);
    const auto push = injection_anchored_function(r, x,
        "void toybox::userdata::UserDataFBS::pushBackPetEquipmentStock(const std::shared_ptr<UserPetEquipmentStock> &)");
    const auto fields = push ? contract(push, 1024, pet_push_fields) : 0;
    const auto assign = push ? contract(push, 1024, pet_push_assign) : 0;
    const auto dirty = push ? contract(push, 1024, pet_push_dirty) : 0;
    if (!fields || !assign || !dirty || removal_mov(fields, pet_push_fields, Role::live_field) != out.live ||
        removal_call(fields, pet_push_fields, Role::decoder) != pet.decoder ||
        removal_add(fields, pet_push_fields, Role::secure_field) != out.userdata_id) return out;
    out.map = removal_mov(fields, pet_push_fields, Role::map_field);
    out.dirty = removal_mov(dirty, pet_push_dirty, Role::dirty_field);
    out.append = removal_call(fields, pet_push_fields, Role::append);
    out.hash_find = removal_call(fields, pet_push_fields, Role::find);
    out.assign = removal_call(assign, pet_push_assign, Role::assign);
    out.node_payload = removal_add(assign, pet_push_assign, Role::payload);
    const auto emplace = removal_call(assign, pet_push_assign, Role::emplace);
    auto deleted_sites = removal_candidates(r, x, 0, 0, pet_deleted_vector);
    deleted_sites.erase(std::remove_if(deleted_sites.begin(), deleted_sites.end(), [&](uintptr_t at) {
        return removal_mov(at, pet_deleted_vector, Role::live_field) != out.live ||
            removal_mov(at, pet_deleted_vector, Role::map_field) != out.map ||
            removal_call(at, pet_deleted_vector, Role::dispose) != removal_call(at, pet_deleted_vector, Role::copy2);
    }), deleted_sites.end());
    if (deleted_sites.size() != 1) return out;
    out.deleted = removal_mov(deleted_sites.front(), pet_deleted_vector, Role::deleted_field);
    const auto append_dispose = contract(out.append, 1024, pet_append_dispose);
    const auto split = removal_call(append_dispose, pet_append_dispose, Role::dispose);
    const auto split_dtor = split ? contract(split, 1024, pet_split_dtor) : 0;
    out.destructor = removal_call(split_dtor, pet_split_dtor, Role::destructor);
    const auto node_dispose = emplace ? contract(emplace, 2048, pet_emplace_dispose) : 0;
    out.node_dispose = removal_call(node_dispose, pet_emplace_dispose, Role::dispose);
    const auto node_dtor = out.node_dispose ? contract(out.node_dispose, 256, pet_holder_dtor) : 0;
    if (!out.destructor || !node_dtor || removal_call(node_dtor, pet_holder_dtor, Role::destructor) != out.destructor ||
        removal_add(node_dtor, pet_holder_dtor, Role::payload) != out.node_payload) return out;

    std::vector<uintptr_t> fbs_erasers;
    for (uintptr_t callee : removal_direct_calls(r, x, buddy.remove, 8192)) {
        const auto wrapper = contract(callee, removal_code_size(userdata_erase_wrapper), userdata_erase_wrapper);
        if (wrapper && removal_call(wrapper, userdata_erase_wrapper, Role::manager) == out.manager)
            push_unique(fbs_erasers, removal_call(wrapper, userdata_erase_wrapper, Role::remove));
    }
    if (fbs_erasers.size() != 1) return out;
    const auto erase_site = contract(fbs_erasers.front(), 2048, buddy_fbs_erase);
    const auto erase = removal_call(erase_site, buddy_fbs_erase, Role::remove);
    const auto unlink_site = erase ? contract(erase, 512, hash_erase_unlink) : 0;
    out.hash_unlink = removal_call(unlink_site, hash_erase_unlink, Role::unlink);
    if (!out.hash_unlink || !contract(out.hash_unlink, removal_code_size(hash_unlink_body), hash_unlink_body)) return out;

    const auto stock_table = unique_primary_vtable(r, x, "N6toybox23DomainPetEquipmentStockE");
    uintptr_t stock_dtor = 0;
    if (!stock_table || !read_range_ptr(r, stock_table, &stock_dtor)) return out;
    const auto userdata = contract(stock_dtor, 512, pet_stock_userdata);
    uintptr_t table_address = 0, page = 0; uint32_t reg = 0;
    if (!userdata || !decode_adrp_page(read_u32(removal_capture(userdata, pet_stock_userdata, Role::page)),
            removal_capture(userdata, pet_stock_userdata, Role::page), &page, &reg) ||
        !decode_add_immediate(read_u32(removal_capture(userdata, pet_stock_userdata, Role::page_add)), reg, page, &table_address) ||
        table_address != stock_table || removal_call(userdata, pet_stock_userdata, Role::destructor) != out.destructor) return out;
    const int64_t userdata_field = item_catalog::signed_bits(
        (read_u32(removal_capture(userdata, pet_stock_userdata, Role::userdata_field)) >> 12U) & 511U, 9);
    if (userdata_field <= 0) return out;
    out.stock_userdata = static_cast<size_t>(userdata_field);
    // Keep existing DomainPetEquipmentStock wrappers alive. Reload reconstructs
    // them and breaks GUI holders which compare the actual stock object.
    if(!pet.count.valid) return out;
    auto &repo=out.repository;
    const auto stock_vector=contract(out.reload,4096,pet_repo_stock_vector);
    const auto id_insert=contract(out.reload,4096,pet_repo_id_insert);
    const auto id_get=injection_anchored_function(r,x,"not found petEquipmentStock [equipmentStockId] [{}]");
    const auto id_tree=id_get ? contract(id_get,1024,pet_repo_id_tree) : 0;
    const auto id_value=id_get ? contract(id_get,1024,pet_repo_id_value) : 0;
    const auto id_erase=contract(pet.remove,2048,pet_repo_id_erase);
    if(!stock_vector || !id_insert || !id_tree || !id_value || !id_erase) return out;
    const auto live_field=count_pair_offset(stock_vector,pet_repo_stock_vector,Role::live_field);
    const auto id_root=item_catalog::signed_bits((read_u32(removal_capture(id_tree,pet_repo_id_tree,Role::root))>>12U)&511U,9);
    const auto reload_root=item_catalog::signed_bits((read_u32(removal_capture(stock_vector,pet_repo_stock_vector,Role::root))>>12U)&511U,9);
    const auto payload=count_pair_offset(id_value,pet_repo_id_value,Role::payload);
    if(live_field<0 || id_root<0 || payload<0) return out;
    repo.live=static_cast<size_t>(live_field);repo.root=static_cast<size_t>(id_root);repo.payload=static_cast<size_t>(payload);
    repo.ids=removal_add(id_insert,pet_repo_id_insert,Role::map_field);
    repo.key=item_catalog::unsigned_offset(read_u32(removal_capture(id_tree,pet_repo_id_tree,Role::key)),2);
    repo.left=removal_slot(id_tree,pet_repo_id_tree,Role::left);
    repo.right=removal_add(id_tree,pet_repo_id_tree,Role::right);
    repo.assign=removal_call(id_insert,pet_repo_id_insert,Role::assign);
    repo.destructor=removal_call(stock_vector,pet_repo_stock_vector,Role::destructor);
    repo.unlink=removal_call(id_erase,pet_repo_id_erase,Role::unlink);
    repo.free_node=removal_call(id_erase,pet_repo_id_erase,Role::dispose);
    uintptr_t deleting=0;
    if(!read_range_ptr(r,stock_table+sizeof(uintptr_t),&deleting) ||
       repo.live+sizeof(uintptr_t)!=removal_slot(stock_vector,pet_repo_stock_vector,Role::end_field) ||
       repo.ids!=removal_add(stock_vector,pet_repo_stock_vector,Role::map_field) || id_root!=reload_root || repo.root<repo.ids ||
       repo.root-repo.ids!=removal_add(id_erase,pet_repo_id_erase,Role::source_field) ||
       repo.payload!=removal_add(id_insert,pet_repo_id_insert,Role::payload) ||
       repo.payload!=removal_add(id_erase,pet_repo_id_erase,Role::payload) ||
       repo.destructor!=removal_call(id_erase,pet_repo_id_erase,Role::destructor) ||
       !contract(repo.unlink,removal_code_size(pet_repo_unlink),pet_repo_unlink) ||
       !contract(repo.assign,removal_code_size(pet_repo_assign),pet_repo_assign) ||
       !contract(repo.destructor,removal_code_size(pet_repo_destruct),pet_repo_destruct) ||
       !contract(deleting,removal_code_size(pet_stock_deleting),pet_stock_deleting) ||
       removal_call(deleting,pet_stock_deleting,Role::destructor)!=stock_dtor ||
       removal_call(deleting,pet_stock_deleting,Role::dispose)!=repo.free_node ||
       removal_call(repo.assign,pet_repo_assign,Role::release_shared)!=removal_call(repo.destructor,pet_repo_destruct,Role::release_shared) ||
       removal_call(repo.assign,pet_repo_assign,Role::release_weak)!=removal_call(repo.destructor,pet_repo_destruct,Role::release_weak) ||
       !range_contains(x,repo.free_node,4) || repo.left==repo.right ||
       std::max({repo.live,repo.ids,repo.root,repo.key,repo.left,repo.right,repo.payload})>=4096) return out;
    const auto create = injection_anchored_function(r, x,
        "static std::shared_ptr<DomainPetEquipmentStock> toybox::DomainPetEquipmentStock::createShared(const std::shared_ptr<userdata::UserPetEquipmentStock>)");
    const auto create_site = create ? contract(create, 1024, pet_create_userdata) : 0;
    const auto setter = removal_call(create_site, pet_create_userdata, Role::setter);
    const auto setter_site = setter ? contract(setter, 512, pet_assign_userdata) : 0;
    if (!setter_site || removal_add(setter_site, pet_assign_userdata, Role::userdata_field) != out.stock_userdata ||
        removal_call(setter_site, pet_assign_userdata, Role::assign) != out.assign) return out;

    const auto bad = contract(pet.remove, 2048, pet_bad_unlink);
    const auto unsafe = removal_call(bad, pet_bad_unlink, Role::inner);
    const auto unsafe_site = unsafe ? contract(unsafe, 512, pet_unsafe_unlink) : 0;
    if (!unsafe_site) return out;
    auto guards = removal_candidates(r, x, 0, 0, pet_equipped_predicate);
    guards.erase(std::remove_if(guards.begin(), guards.end(), [&](uintptr_t at) {
        for (Role role : {Role::repository, Role::active_pet, Role::type_getter})
            if (removal_call(at, pet_equipped_predicate, role) != removal_call(unsafe_site, pet_unsafe_unlink, role)) return true;
        return false;
    }), guards.end());
    if (guards.size() != 1) return out;
    const auto slot_getter = removal_call(guards.front(), pet_equipped_predicate, Role::slot_getter);
    const auto slots = contract(slot_getter, 1024, pet_slot_fields);
    const auto slot_userdata = contract(slot_getter, 1024, pet_slot_userdata);
    if (!slots || !slot_userdata || removal_call(slots, pet_slot_fields, Role::decoder) != pet.decoder) return out;
    size_t index = 0;
    for (Role role : {Role::slot1, Role::slot3, Role::slot4, Role::slot5}) out.equipped[index++] = removal_add(slots, pet_slot_fields, role);
    const auto push_pet = injection_anchored_function(r, x,
        "void toybox::userdata::UserDataFBS::pushBackPet(const std::shared_ptr<UserPet> &)");
    const auto pet_fields = push_pet ? contract(push_pet, 1024, pet_push_fields) : 0;
    if (!pet_fields) return out;
    out.pets = removal_mov(pet_fields, pet_push_fields, Role::live_field);
    const auto get_user = removal_call(slot_userdata, pet_slot_userdata, Role::accessor);
    size_t matching_wrappers = 0;
    for (uintptr_t callee : removal_direct_calls(r, x, get_user, 4096)) {
        const auto wrapper = contract(callee, removal_code_size(userdata_erase_wrapper), userdata_erase_wrapper);
        if (wrapper && removal_call(wrapper, userdata_erase_wrapper, Role::manager) == out.manager &&
            removal_call(wrapper, userdata_erase_wrapper, Role::remove) == push_pet) ++matching_wrappers;
    }
    if (matching_wrappers != 1) return out;
    std::vector<size_t> fields_used{out.live, out.deleted, out.map, out.pets};
    std::sort(fields_used.begin(), fields_used.end());
    for (size_t i = 0; i < fields_used.size(); ++i)
        if (!fields_used[i] || fields_used[i] > 65536 || (i && fields_used[i] < fields_used[i-1] + 24)) return out;
    if (!out.dirty || out.dirty > 65536 || !out.userdata_id || out.userdata_id > 4096) return out;
    std::vector<size_t> equip(std::begin(out.equipped), std::end(out.equipped));
    std::sort(equip.begin(), equip.end());
    if (equip.front() == 0 || equip.back() > 4096 || std::adjacent_find(equip.begin(), equip.end()) != equip.end()) return out;
    for (uintptr_t function : {out.manager, out.accessor, out.reload, out.append, out.assign, out.destructor,
                               out.hash_find, out.hash_unlink, out.node_dispose}) if (!range_contains(x, function, 4)) return out;
    out.ready = true;
    return out;
}
