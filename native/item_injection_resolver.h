// Generated verbatim from the production resolver.
// Included in template_native.cpp after the shared read-only resolver helpers.
// This feature owns its queue, command serials, diagnostics and token use.
struct InjectionLayout {
    uintptr_t character_ready_slot = 0;
    uintptr_t currency_core = 0;
    uint32_t forbidden_currency = 0;
    uintptr_t token_repository = 0, token_assign = 0, token_kind = 0;
    uintptr_t base_change = 0, ticket_writer = 0;
    uintptr_t base_set = 0, amount_floor = 0;
    size_t setter_slot = 0, core_slot = 0, state_slot = 0;
    uintptr_t resources_low = 0, cast_function = 0, item_rtti = 0, ticket_rtti = 0;
    uintptr_t master_getter = 0, type_getter = 0;
    uintptr_t sync = 0, sync_manager = 0;
    uintptr_t other_resources = 0;
    uintptr_t unknown_factory = 0;
    size_t borrowed_id_field = 0;
    uintptr_t initial_equipment_checks[3]{};
    size_t change_slot = 0, pool_begin = 0, low_watermark = 0, equipment_master = 0;
    int32_t amount_max = 0;
    size_t other_count_field = 0, other_low_watermark = 0;
};
InjectionLayout g_injection_layout;
std::atomic<bool> g_injection_ready{false};
// Function boundaries come from the ELF loader's unwind metadata, so locating an
// assertion's owner does not depend on a particular compiler prologue or RVA.
// These are standard ELF/DWARF format records, never game-object field layouts.
uintptr_t injection_function_start(const std::vector<MemoryRange> &readable,
                                   const std::vector<MemoryRange> &exec, uintptr_t pc) {
    if(!range_contains(exec,pc,4)) return 0;
    const auto read_metadata=[&](uintptr_t address,void *value,size_t size) {
        if(!range_contains(readable,address,size)) return false;
        if(t_active_scan_snapshot && t_active_scan_snapshot->read(address,value,size)) return true;
        return vm_read_partial(address,value,size)==static_cast<ssize_t>(size);
    };
    const uintptr_t base = find_module_base(kTargetLib);
    Elf64_Ehdr elf{};
    if (!base || !range_contains(readable, base, sizeof(elf)) ||
        !read_metadata(base, &elf, sizeof(elf)) ||
        std::memcmp(elf.e_ident, ELFMAG, SELFMAG) != 0 || elf.e_machine != EM_AARCH64 ||
        elf.e_phentsize != sizeof(Elf64_Phdr) || elf.e_phnum == 0) return 0;
    for (size_t i = 0; i < elf.e_phnum; ++i) {
        Elf64_Phdr ph{};
        uintptr_t at = base + elf.e_phoff + i * sizeof(ph);
        if (!read_metadata(at, &ph, sizeof(ph))) return 0;
        if (ph.p_type != PT_GNU_EH_FRAME) continue;
        struct Header { uint8_t version, pointer_encoding, count_encoding, table_encoding;
                        int32_t frame_pointer; uint32_t count; } header{};
        struct Entry { int32_t function, fde; };
        const uintptr_t address = base + ph.p_vaddr;
        if (!range_contains(readable, address, sizeof(header)) ||
            !read_metadata(address, &header, sizeof(header)) ||
            header.version != 1 || header.pointer_encoding != 0x1b ||
            header.count_encoding != 0x03 || header.table_encoding != 0x3b ||
            header.count == 0 || ph.p_memsz < sizeof(header) ||
            header.count > (ph.p_memsz - sizeof(header)) / sizeof(Entry)) return 0;
        const uintptr_t table = address + sizeof(header);
        if (!range_contains(readable, table, header.count * sizeof(Entry))) return 0;
        size_t lo = 0, hi = header.count;
        uintptr_t found = 0, fde=0;
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            Entry entry{};
            if (!read_metadata(table + mid * sizeof(Entry), &entry, sizeof(entry))) return 0;
            const uintptr_t start = address + static_cast<intptr_t>(entry.function);
            if (start <= pc) { found = start; fde=address+static_cast<intptr_t>(entry.fde);lo = mid + 1; } else hi = mid;
        }
        uintptr_t end=0;
        return found && elf_unwind::fde_contains(read_metadata,fde,found,pc,&end) &&
               range_contains(exec,found,end-found) ? found : 0;
    }
    return 0;
}

// Android may map the zero-filled tail of PT_LOAD as anonymous memory. A
// pathname-filtered /proc/maps snapshot therefore cannot validate BSS slots.
bool injection_writable_image_slot(const std::vector<MemoryRange> &readable, uintptr_t slot) {
    const uintptr_t base=find_module_base(kTargetLib);
    Elf64_Ehdr elf{};
    if(!base || !range_contains(readable,base,sizeof(elf)) ||
       vm_read_partial(base,&elf,sizeof(elf))!=sizeof(elf) ||
       std::memcmp(elf.e_ident,ELFMAG,SELFMAG) || elf.e_ident[EI_CLASS]!=ELFCLASS64 ||
       elf.e_ident[EI_DATA]!=ELFDATA2LSB || elf.e_machine!=EM_AARCH64 ||
       elf.e_phentsize!=sizeof(Elf64_Phdr) || !elf.e_phnum || slot%alignof(uintptr_t)) return false;
    if(elf.e_phoff>UINTPTR_MAX-base) return false;
    const uintptr_t table=base+elf.e_phoff;
    if(!range_contains(readable,table,size_t(elf.e_phnum)*sizeof(Elf64_Phdr))) return false;
    for(size_t i=0;i<elf.e_phnum;++i) {
        Elf64_Phdr ph{};
        if(vm_read_partial(table+i*sizeof(ph),&ph,sizeof(ph))!=sizeof(ph)) return false;
        if(ph.p_type!=PT_LOAD || !(ph.p_flags&PF_W) || ph.p_filesz>ph.p_memsz ||
           ph.p_vaddr>UINTPTR_MAX-base) continue;
        const uintptr_t start=base+ph.p_vaddr;
        if(ph.p_memsz>UINTPTR_MAX-start || slot<start || ph.p_memsz<sizeof(uintptr_t) ||
           slot-start>ph.p_memsz-sizeof(uintptr_t)) continue;
        uintptr_t value=0;
        return vm_read_partial(slot,&value,sizeof(value))==sizeof(value);
    }
    return false;
}

uintptr_t injection_anchored_function(const std::vector<MemoryRange> &readable,
                                      const std::vector<MemoryRange> &exec, const char *text) {
    std::vector<uintptr_t> functions;
    for (uintptr_t string : find_pattern(readable, reinterpret_cast<const uint8_t *>(text), std::strlen(text) + 1)) {
        for (uintptr_t ref : find_pc_relative_refs(exec, string)) {
            uintptr_t function = injection_function_start(readable, exec, ref);
            if (function && ref >= function && ref - function < 65536) push_unique(functions, function);
        }
    }
    return functions.size() == 1 ? functions.front() : 0;
}

template<size_t N>
std::vector<uintptr_t> injection_contract_candidates(const std::vector<MemoryRange> &exec,
                             uintptr_t start, size_t length, const item_injection_contracts::Word (&pattern)[N]) {
    item_catalog::Word converted[N]{};
    for (size_t i = 0; i < N; ++i) converted[i] = {pattern[i].value, pattern[i].mask, item_catalog::Role::none};
    std::vector<MemoryRange> scope;
    if (start == 0) scope = exec;
    else for (const auto &r : exec) {
        const uintptr_t lo = std::max(r.start, start), hi = std::min(r.end, start + length);
        if (hi > lo) scope.push_back({lo, hi, true, false});
    }
    return find_item_contract(scope, converted);
}
template<size_t N>
uintptr_t injection_capture(uintptr_t site, const item_injection_contracts::Word (&pattern)[N],
                            item_injection_contracts::Role role) {
    if (!site) return 0;
    for (size_t i = 0; i < N; ++i) if (pattern[i].role == role) return site + i * sizeof(uint32_t);
    return 0;
}
template<size_t N>
uintptr_t injection_contract(const std::vector<MemoryRange> &exec, uintptr_t start, size_t length,
                             const item_injection_contracts::Word (&pattern)[N]) {
    auto hits = injection_contract_candidates(exec, start, length, pattern);
    return hits.size() == 1 ? hits.front() : 0;
}

#include "item_removal_runtime.inc"
#include "item_fish_resolver.inc"

#include "item_semantics_resolver.inc"

bool resolve_item_injection(const std::vector<MemoryRange> &readable,
                            const std::vector<MemoryRange> &exec,
                            uintptr_t purchase, uintptr_t ticket_writer) {
    using namespace item_injection_contracts;
    if (g_injection_ready.load(std::memory_order_acquire)) return true;
    if (!g_object_layouts.amount_valid || !g_item_layout_ready.load(std::memory_order_acquire)) return false;
    uintptr_t wrapper = 0;
    if (!resolve_lua_registered_function(readable, exec, "changeItemAmount", true, &wrapper) || !wrapper) return false;
    // The wrapper contains an early return before the dispatch tail, so these
    // bounded instruction scans intentionally do not stop at the first RET.
    const uintptr_t repo = injection_contract(exec, wrapper, 1024, lua_token_repository);
    const uintptr_t assign = injection_contract(exec, wrapper, 1024, lua_token_assign);
    const uintptr_t dispatch = injection_contract(exec, wrapper, 1024, lua_change_dispatch);
    const uintptr_t kind = purchase ? injection_contract(exec, purchase, 1024, shop_token_kind) : 0;
    InjectionLayout layout{};
    uintptr_t shop_repo = 0;
    if (!repo || !assign || !dispatch || !kind || !ticket_writer ||
        !decode_branch_target(repo, true, &layout.token_repository) ||
        !decode_branch_target(injection_capture(assign, lua_token_assign, Role::assign), true, &layout.token_assign) ||
        !decode_branch_target(kind, true, &shop_repo) || shop_repo != layout.token_repository ||
        !decode_branch_target(injection_capture(kind, shop_token_kind, Role::kind), true, &layout.token_kind) ||
        !decode_branch_target(ticket_writer + sizeof(sig_item_writer_pattern) - 4, true, &layout.base_change)) return false;
    bool shared_assign = false;
    for (uintptr_t pc = kind; pc < kind + 256 && range_contains(exec, pc, 4); pc += 4) {
        uintptr_t callee = 0;
        if (decode_branch_target(pc, true, &callee) && callee == layout.token_assign) shared_assign = true;
    }
    const uintptr_t pool = injection_contract(exec, layout.token_assign, 128, token_pool);
    if (!shared_assign || !pool) return false;
    const int64_t pool_offset = item_catalog::signed_bits((read_u32(pool) >> 15U) & 127U, 7) * 8;
    layout.change_slot = item_catalog::unsigned_offset(read_u32(injection_capture(dispatch, lua_change_dispatch, Role::change_slot)), 3);
    if (pool_offset < 0 || layout.change_slot % sizeof(uintptr_t) != 0 ||
        layout.change_slot == g_object_layouts.amount_slot) return false;
    layout.pool_begin = static_cast<size_t>(pool_offset);
    layout.ticket_writer = ticket_writer;
    uintptr_t initial_factory = injection_anchored_function(readable, exec,
            "can not add initial weapon or armor! [equipmentSpecieId] [{}]");
    const auto initial_sites = initial_factory ? injection_contract_candidates(exec, initial_factory, 1024, initial_equipment)
                                               : std::vector<uintptr_t>{};
    bool initial_valid = !initial_sites.empty();
    for (uintptr_t initial : initial_sites) {
        const size_t field = item_catalog::unsigned_offset(read_u32(initial), 3);
        if (field != item_catalog::unsigned_offset(read_u32(injection_capture(initial, initial_equipment, Role::master2)), 3) ||
            field != item_catalog::unsigned_offset(read_u32(injection_capture(initial, initial_equipment, Role::master3)), 3) ||
            (layout.equipment_master && layout.equipment_master != field)) initial_valid = false;
        layout.equipment_master = field;
        const Role check_roles[] = {Role::check1, Role::check2, Role::check3};
        for (size_t i = 0; i < 3; ++i) {
            uintptr_t callee = 0;
            if (!decode_branch_target(injection_capture(initial, initial_equipment, check_roles[i]), true, &callee) ||
                (layout.initial_equipment_checks[i] && layout.initial_equipment_checks[i] != callee)) initial_valid = false;
            layout.initial_equipment_checks[i] = callee;
        }
    }
    if (!initial_valid) { layout.equipment_master = 0; std::fill(std::begin(layout.initial_equipment_checks), std::end(layout.initial_equipment_checks), 0); }

    const uintptr_t cast = injection_contract(exec, wrapper, 1024, ticket_cast);
    const uintptr_t ticket = injection_contract(exec, wrapper, 1024, ticket_dispatch);
    uintptr_t ticket_call = 0, gate_repository = 0, pool_predicate = 0;
    layout.resources_low = injection_contract(exec, 0, 0, resource_gate);
    if (!cast || !ticket || ticket <= cast || ticket - cast > 128 ||
        !decode_branch_target(injection_capture(ticket, ticket_dispatch, Role::writer), true, &ticket_call) || ticket_call != ticket_writer ||
        !decode_branch_target(injection_capture(cast, ticket_cast, Role::cast_call), true, &layout.cast_function) ||
        !read_range_ptr(readable, decode_adrp_ldr_global(injection_capture(cast, ticket_cast, Role::item_page), injection_capture(cast, ticket_cast, Role::item_load)), &layout.item_rtti) ||
        !read_range_ptr(readable, decode_adrp_ldr_global(injection_capture(cast, ticket_cast, Role::ticket_page), injection_capture(cast, ticket_cast, Role::ticket_load)), &layout.ticket_rtti) ||
        !layout.resources_low || !decode_branch_target(injection_capture(layout.resources_low, resource_gate, Role::repository), true, &gate_repository) ||
        gate_repository != layout.token_repository ||
        !decode_branch_target(injection_capture(layout.resources_low, resource_gate, Role::pool_predicate), true, &pool_predicate) ||
        injection_contract(exec, pool_predicate, sizeof(token_low_watermark), token_low_watermark) != pool_predicate) return false;
    const size_t byte_watermark = (read_u32(injection_capture(pool_predicate, token_low_watermark, Role::watermark)) >> 10U) & 0xfffU;
    if (byte_watermark == 0 || byte_watermark % (2 * sizeof(uintptr_t))) return false;
    const int64_t predicate_pool = item_catalog::signed_bits((read_u32(pool_predicate) >> 15U) & 127U, 7) * sizeof(uintptr_t);
    if (predicate_pool != pool_offset) return false;
    layout.low_watermark = byte_watermark / (2 * sizeof(uintptr_t));
    uintptr_t other_low = 0;
    if (!decode_branch_target(injection_capture(layout.resources_low, resource_gate, Role::other_repository), true, &layout.other_resources) ||
        !decode_branch_target(injection_capture(layout.resources_low, resource_gate, Role::other_predicate), false, &other_low) ||
        injection_contract(exec, other_low, sizeof(other_resource_low_watermark), other_resource_low_watermark) != other_low) return false;
    layout.other_count_field = item_catalog::unsigned_offset(read_u32(other_low), 3);
    layout.other_low_watermark = (read_u32(injection_capture(other_low, other_resource_low_watermark, Role::watermark)) >> 10U) & 0xfffU;
    if (!layout.other_low_watermark) return false;
    const uintptr_t ceiling = injection_contract(exec, layout.base_change, 256, amount_ceiling);
    if (!ceiling) return false;
    const uint32_t maximum = ((read_u32(ceiling) >> 5U) & 0xffffU) |
                             (((read_u32(injection_capture(ceiling, amount_ceiling, Role::high_half)) >> 5U) & 0xffffU) << 16U);
    if (maximum == 0 || maximum > INT32_MAX) return false;
    layout.amount_max = static_cast<int32_t>(maximum);
    layout.setter_slot = item_catalog::unsigned_offset(read_u32(injection_capture(ceiling, amount_ceiling, Role::setter_slot)), 3);
    const auto floor = injection_contract(exec, layout.base_change, 256, amount_floor);
    uintptr_t floor2 = 0;
    if (!floor || !decode_branch_target(injection_capture(floor, amount_floor, Role::floor1), true, &layout.amount_floor) ||
        !decode_branch_target(injection_capture(floor, amount_floor, Role::floor2), true, &floor2) || floor2 != layout.amount_floor) return false;
    layout.base_set = injection_anchored_function(readable, exec, "virtual void toybox::DomainItem::setAmount(int, uint32_t)");
    const auto core = layout.base_set ? injection_contract(exec, layout.base_set, 1024, base_set_core) : 0;
    const auto state = layout.base_set ? injection_contract(exec, layout.base_set, 1024, base_set_state) : 0;
    if (!core || !state || item_catalog::unsigned_offset(read_u32(injection_capture(core, base_set_core, Role::amount_slot)), 3)
            != g_object_layouts.amount_slot ||
        ((read_u32(injection_capture(core, base_set_core, Role::requested1)) >> 12U) & 511U) !=
        ((read_u32(injection_capture(core, base_set_core, Role::requested2)) >> 12U) & 511U)) return false;
    layout.core_slot = item_catalog::unsigned_offset(read_u32(injection_capture(core, base_set_core, Role::core_slot)), 3);
    layout.state_slot = item_catalog::unsigned_offset(read_u32(injection_capture(state, base_set_state, Role::state_slot)), 3);
    if (!layout.setter_slot || !layout.core_slot || !layout.state_slot || !range_contains(exec, layout.amount_floor, 4)) return false;
    const uintptr_t type = injection_contract(exec, layout.token_kind, 128, master_type);
    if (!type || !decode_branch_target(injection_capture(type, master_type, Role::master_getter), true, &layout.master_getter) ||
        !decode_branch_target(injection_capture(type, master_type, Role::type_getter), true, &layout.type_getter)) return false;
    layout.sync = injection_anchored_function(readable, exec, "user_data_sync");
    if (!layout.sync) return false;
    for (uintptr_t site : injection_contract_candidates(exec, 0, 0, sync_bridge)) {
        uintptr_t getter = 0, target = 0;
        if (!decode_branch_target(injection_capture(site, sync_bridge, Role::sync), true, &target) || target != layout.sync) continue;
        if (!decode_branch_target(site, true, &getter) || (layout.sync_manager && layout.sync_manager != getter)) return false;
        layout.sync_manager = getter;
    }
    if (!layout.sync_manager) return false;
    for (uintptr_t function : {layout.token_repository, layout.token_assign, layout.token_kind,
                               layout.base_change, layout.ticket_writer, layout.cast_function, layout.resources_low,
                               layout.master_getter, layout.type_getter, layout.sync, layout.sync_manager, layout.other_resources}) {
        if (!range_contains(exec, function, 4)) return false;
    }
    uintptr_t id_getter = 0;
    if (decode_branch_target(injection_capture(repo, lua_token_repository, Role::id_getter), true, &id_getter)) {
        const uintptr_t copy = injection_contract(exec, id_getter, 1024, id_copy);
        uintptr_t secure_copy = 0;
        if (copy && decode_branch_target(injection_capture(copy, id_copy, Role::secure_copy), true, &secure_copy)) {
            const size_t source_field = (read_u32(injection_capture(copy, id_copy, Role::source_field)) >> 10U) & 0xfffU;
            const int64_t secure_field = item_catalog::signed_bits(
                    (read_u32(injection_capture(copy, id_copy, Role::secure_field)) >> 12U) & 511U, 9);
            std::vector<uintptr_t> factories;
            for (uintptr_t site : injection_contract_candidates(exec, purchase, 2048, unknown_shop)) {
                uintptr_t getter = 0, factory = 0, copier = 0;
                if (!decode_branch_target(injection_capture(site, unknown_shop, Role::id_getter), true, &getter) || getter != id_getter ||
                    !decode_branch_target(injection_capture(site, unknown_shop, Role::factory), true, &factory)) continue;
                const uintptr_t adapter = injection_contract(exec, factory, 128, unknown_adapter);
                if (!adapter || !decode_branch_target(injection_capture(adapter, unknown_adapter, Role::secure_copy), true, &copier) ||
                    copier != secure_copy || secure_field <= 0 || source_field < static_cast<size_t>(secure_field) ||
                    ((read_u32(injection_capture(adapter, unknown_adapter, Role::input_field)) >> 10U) & 0xfffU) != static_cast<size_t>(secure_field)) continue;
                push_unique(factories, factory);
            }
            if (factories.size() == 1) {
                layout.unknown_factory = factories.front();
                layout.borrowed_id_field = source_field - secure_field;
            }
        }
    }
    const auto character=injection_anchored_function(readable,exec,"static DomainPCRepository *toybox::DomainPCRepository::getInstance()");
    if(character) {
        const auto unique_owned=[&](const auto &pattern) {
            auto sites=injection_contract_candidates(exec,character,1024,pattern);
            sites.erase(std::remove_if(sites.begin(),sites.end(),[&](uintptr_t at) {
                return injection_function_start(readable,exec,at)!=character;
            }),sites.end());
            return sites.size()==1 ? sites.front() : uintptr_t(0);
        };
        const auto load=unique_owned(pc_singleton_load),store=unique_owned(pc_singleton_store);
        uintptr_t page=0;uint32_t reg=0;
        if(load && store && decode_adrp_page(read_u32(load),load,&page,&reg)) {
            const auto field=item_catalog::unsigned_offset(read_u32(load+12),3);
            const auto target=load+16+static_cast<intptr_t>(item_catalog::signed_bits((read_u32(load+16)>>5)&0x7ffff,19)*4);
            if(field==item_catalog::unsigned_offset(read_u32(store+12),3) && target==store+16 &&
               field<=UINTPTR_MAX-page && injection_writable_image_slot(readable,page+field))
                layout.character_ready_slot=page+field;
        }
    }
    const auto currency_table = unique_primary_vtable(readable,exec,"N6toybox14DomainCurrencyE");
    uintptr_t currency_core = 0;
    if (currency_table && read_range_ptr(readable,currency_table+layout.core_slot,&currency_core)) {
        auto sites = injection_contract_candidates(exec,currency_core,2048,currency_forbidden);
        sites.erase(std::remove_if(sites.begin(),sites.end(),[&](uintptr_t at) {
            return injection_function_start(readable,exec,at)!=currency_core;
        }),sites.end());
        if (sites.size()==1) {
            bool anchored = false;
            static constexpr char message[] = "cannot modify gem amount";
            for (auto text : find_pattern(readable,reinterpret_cast<const uint8_t *>(message),sizeof(message)))
                for (auto ref : find_pc_relative_refs(exec,text))
                    anchored |= injection_function_start(readable,exec,ref)==currency_core;
            if (anchored) {
                layout.currency_core=currency_core;
                layout.forbidden_currency=((read_u32(sites[0])>>5U)&0xffffU) |
                    (((read_u32(sites[0]+20)>>5U)&0xffffU)<<16U);
            }
        }
    }
    resolve_item_removals(readable, exec, g_object_layouts.amount_slot, layout.change_slot);
    g_injection_layout = layout;
    g_fish=resolve_fish(readable,exec);
    g_special_inventory=resolve_special_inventory(readable,exec,g_object_layouts.amount_slot,layout.core_slot);
    g_injection_ready.store(true, std::memory_order_release);
    ALOGI("AE_INJECTION resolved changeSlot=%zu poolBegin=%zu equipment=%d lowWatermark=%zu amountMax=%d",
          layout.change_slot, layout.pool_begin, layout.equipment_master != 0, layout.low_watermark, layout.amount_max);
    return true;
}

