#pragma once

// Executes imported resolver/queue code only. Never invokes a game function.
bool audit_item_injection(const std::vector<MemoryRange> &readable,
                          const std::vector<MemoryRange> &exec,
                          uintptr_t purchase, uintptr_t writer) {
    const bool resolved = resolve_item_injection(readable, exec, purchase, writer);
    bool passed = resolved;
    auto check = [&](const char *id, bool ok, const std::string &detail) {
        printf("CHECK %s %d %s\n", id, ok, detail.c_str());
        passed &= ok;
    };
    check("injection.resolver", resolved, "Production resolver and cross-call consistency");
    const auto &layout = g_injection_layout;
    auto target = [&](const char *id, uintptr_t address) {
        char detail[64];
        std::snprintf(detail, sizeof(detail), "RVA=0x%llx",
                      static_cast<unsigned long long>(address ? address - audit_base : 0));
        check(id, resolved && address && range_contains(exec, address, sizeof(uint32_t)), detail);
    };
    target("injection.token_repository", layout.token_repository);
    check("injection.token_repository_slot",injection_writable_image_slot(readable,layout.token_repository_slot),
          "Existing native owner slot; admission must not create a repository during loading");
    target("injection.token_assign", layout.token_assign);
    target("injection.token_kind", layout.token_kind);
    target("injection.base_change", layout.base_change);
    target("injection.ticket_writer", layout.ticket_writer);
    target("injection.dynamic_cast", layout.cast_function);
    target("injection.master_getter", layout.master_getter);
    target("injection.type_getter", layout.type_getter);
    target("injection.resource_gate", layout.resources_low);
    target("injection.other_resources", layout.other_resources);
    target("injection.sync_manager", layout.sync_manager);
    target("injection.sync", layout.sync);
    target("injection.achievement_callback",g_injection_cascade.callback);
    target("injection.achievement_eligible",g_injection_cascade.eligible);
    target("injection.achievement_refresh",g_injection_cascade.reset);
    target("injection.achievement_release",g_injection_cascade.destroy);
    check("injection.achievement_owner_slot",g_injection_cascade.ready &&
          injection_writable_image_slot(readable,g_injection_cascade.manager_slot),
          "Static capture, reward and lifecycle contracts; live ownership/resource behavior requires device validation");
    target("injection.initial_weapon", layout.initial_equipment_checks[0]);
    target("injection.initial_armor", layout.initial_equipment_checks[1]);
    target("injection.initial_equipment", layout.initial_equipment_checks[2]);
    target("injection.unknown_factory", layout.unknown_factory);
    check("injection.rtti", resolved && range_contains(readable, layout.item_rtti, sizeof(uintptr_t)) &&
          range_contains(readable, layout.ticket_rtti, sizeof(uintptr_t)), "RTTI operands recovered from the game's cast call");
    check("injection.change_slot", resolved, "slot=" + std::to_string(layout.change_slot));
    check("injection.token_pool", resolved, "begin=" + std::to_string(layout.pool_begin) +
          " low-water=" + std::to_string(layout.low_watermark));
    check("injection.other_pool", resolved, "count-field=" + std::to_string(layout.other_count_field) +
          " low-water=" + std::to_string(layout.other_low_watermark));
    check("injection.amount_max", resolved, "inventory-maximum=" + std::to_string(layout.amount_max));
    check("injection.equipment_master", resolved && layout.equipment_master,
          "field=" + std::to_string(layout.equipment_master));
    check("injection.embedded_id", resolved && layout.unknown_factory,
          "field=" + std::to_string(layout.borrowed_id_field));
    check("injection.queue", audit_injection_queue(), "Production parser and queue contracts, including 20000 IDs");
    check("injection.character_ready_slot",range_contains(readable,layout.character_ready_slot,sizeof(uintptr_t)),
          "Fresh character-repository readiness prevents implicit equipment rewards during initialization");
    check("injection.character_bss_bounds",injection_writable_image_slot(readable,layout.character_ready_slot),
          "Writable PT_LOAD memory extent includes the anonymous BSS tail");
    target("injection.currency_writer",layout.currency_core);
    check("injection.currency_exclusion",layout.forbidden_currency!=0,"Native forbidden currency ID="+std::to_string(layout.forbidden_currency));
    const auto &equipment=g_instance_layouts[static_cast<size_t>(InstanceKind::Equipment)];
    const auto &pet=g_instance_layouts[static_cast<size_t>(InstanceKind::Pet)];
    const auto &buddy=g_instance_layouts[static_cast<size_t>(InstanceKind::Buddy)];
    const auto &unknown=g_instance_layouts[static_cast<size_t>(InstanceKind::Unknown)];
    check("injection.equipment_instances",equipment.ready && equipment.count.valid && equipment.create_one,
          "Owned query, native creation/removal, identity decoder and native count index");
    check("injection.pet_instances",pet.ready && pet.count.valid && pet.create_one,
          "Owned query, native factory and exact instance identity");
    check("injection.buddy_instances",buddy.ready && buddy.count.valid && buddy.create_one && buddy.prepare,
          "Native preparation, factory, removal and count index");
    check("injection.unknown_instances",unknown.ready && unknown.count.valid && layout.unknown_factory,
          "Native unidentified-instance factory, selection and removal");
    check("injection.pet_storage",g_pet_storage.ready && g_pet_storage.append && g_pet_storage.hash_unlink && g_pet_storage.node_dispose,
          "Authoritative live/deleted/hash transaction and typed ownership helpers");
    check("injection.pet_survivors",g_pet_storage.ready && g_pet_storage.repository.assign &&
          g_pet_storage.repository.destructor && g_pet_storage.repository.unlink && g_pet_storage.repository.free_node,
          "Bindings for in-place compaction of three indexes; no runtime stock changes occur in this audit");
    check("injection.pet_creation_transaction",g_pet_creation.ready,
          "Native publication order, counter and three-index rollback bindings; allocation failure is a device check");
    check("injection.fish_pool",g_fish.ready && g_fish.pool_draw && g_fish.pool_ctor && g_fish.pool_destroy && g_fish.cache_destroy,
          "Native weighted pool, size farm, seed signature and ownership cleanup contracts");
    check("injection.fish_storage",g_fish.ready && g_fish.fbs_push && g_fish.map_erase && g_fish.repo_remove,
          "Independent inventory storage, capacity, identity sequence and failure rollback bindings");
    check("injection.lottery_semantics",g_special_inventory.scalar_ready!=g_special_inventory.records_ready,
          g_special_inventory.scalar_ready ? "Legacy scalar quantity writer" : "Server-issued expiry records; ordinary quantity writer is RET");
    check("injection.growth_semantics",g_special_inventory.growth_non_inventory,
          "Zero ordinary amount and RET writer; pending gift consumption changes a chosen character");
    return passed;
}
