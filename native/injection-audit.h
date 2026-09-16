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
    return passed;
}
