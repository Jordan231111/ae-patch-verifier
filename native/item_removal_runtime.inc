// Included after the injection resolver helpers, in the same translation unit.
#include "item_removal_contracts.h"
#include "item_pet_contracts.h"

enum class InstanceKind { Equipment, Pet, Buddy, Unknown, Count };
struct InstanceCountLayout {
    size_t root = 0, key = 0, left = 0, right = 0, vector = 0;
    uintptr_t bool_getter = 0, type_getter = 0;
    bool valid = false;
};
struct InstanceLayout {
    uintptr_t vtable = 0, amount = 0, change = 0;
    uintptr_t repository = 0, query = 0, dispose = 0, remove = 0;
    uintptr_t decoder = 0, id_getter = 0;
    uintptr_t create_one = 0, prepare = 0;
    size_t species_id = 0, species_secure = 0, instance_id = 0, instance_secure = 0, notify_slot = 0;
    InstanceCountLayout count;
    bool ready = false;
};
struct PetStorageLayout {
    struct Repository {
        size_t live=0, ids=0, root=0, key=0, left=0, right=0, payload=0;
        uintptr_t assign=0, destructor=0, unlink=0, free_node=0;
    } repository;
    uintptr_t manager = 0, accessor = 0, reload = 0, append = 0, assign = 0, destructor = 0;
    uintptr_t hash_find = 0, hash_unlink = 0, node_dispose = 0;
    size_t live = 0, deleted = 0, map = 0, dirty = 0, userdata_id = 0, node_payload = 0;
    size_t stock_userdata = 0, pets = 0, equipped[4]{};
    bool ready = false;
};
PetStorageLayout g_pet_storage;
InstanceLayout g_instance_layouts[static_cast<size_t>(InstanceKind::Count)];

uintptr_t unique_primary_vtable(const std::vector<MemoryRange> &readable,
                               const std::vector<MemoryRange> &exec, const char *name) {
    std::vector<uintptr_t> tables;
    for (uintptr_t text : find_pattern(readable, reinterpret_cast<const uint8_t *>(name), std::strlen(name) + 1)) {
        for (uintptr_t name_slot : find_pattern(readable, reinterpret_cast<const uint8_t *>(&text), sizeof(text))) {
            if (name_slot % alignof(uintptr_t) || name_slot < sizeof(uintptr_t)) continue;
            const uintptr_t type = name_slot - sizeof(uintptr_t);
            for (uintptr_t type_slot : find_pattern(readable, reinterpret_cast<const uint8_t *>(&type), sizeof(type))) {
                uintptr_t offset_to_top = 1, first = 0, second = 0;
                const uintptr_t table = type_slot + sizeof(uintptr_t);
                if (type_slot % alignof(uintptr_t) || type_slot < sizeof(uintptr_t) ||
                    !read_range_ptr(readable, type_slot - sizeof(uintptr_t), &offset_to_top) || offset_to_top != 0 ||
                    !read_range_ptr(readable, table, &first) || !range_contains(exec, first, 4) ||
                    !read_range_ptr(readable, table + sizeof(uintptr_t), &second) || !range_contains(exec, second, 4)) continue;
                push_unique(tables, table);
            }
        }
    }
    return tables.size() == 1 ? tables.front() : 0;
}

template<size_t N>
constexpr size_t removal_code_size(const item_removal_contracts::Word (&)[N]) { return N * sizeof(uint32_t); }

template<size_t N>
std::vector<uintptr_t> removal_candidates(const std::vector<MemoryRange> &readable,
        const std::vector<MemoryRange> &exec, uintptr_t function, size_t limit,
        const item_removal_contracts::Word (&pattern)[N]) {
    item_catalog::Word words[N]{};
    for (size_t i = 0; i < N; ++i) words[i] = {pattern[i].value, pattern[i].mask, item_catalog::Role::none};
    std::vector<MemoryRange> scope;
    if (!function) scope = exec;
    else for (const auto &r : exec) {
        const uintptr_t lo = std::max(r.start, function), hi = std::min(r.end, function + limit);
        if (hi > lo) scope.push_back({lo, hi, true, false});
    }
    auto hits = find_item_contract(scope, words);
    if (function && limit == removal_code_size(pattern)) {
        // A leaf reached by a verified call edge need not own an unwind entry.
        // Here the entire contract starts at that exact entry and ends in B/RET.
        hits.erase(std::remove_if(hits.begin(), hits.end(), [&](uintptr_t at) { return at != function; }), hits.end());
        return hits;
    }
    // Contract records contain value, mask and role, not just code bytes.
    // An instruction occupies four bytes regardless of sizeof(Word).
    if (function) hits.erase(std::remove_if(hits.begin(), hits.end(), [&](uintptr_t at) {
        return injection_function_start(readable, exec, at) != function ||
            injection_function_start(readable, exec, at + removal_code_size(pattern) - 4) != function;
    }), hits.end());
    return hits;
}
template<size_t N>
uintptr_t removal_contract(const std::vector<MemoryRange> &readable, const std::vector<MemoryRange> &exec,
        uintptr_t function, size_t limit, const item_removal_contracts::Word (&pattern)[N]) {
    auto hits = removal_candidates(readable, exec, function, limit, pattern);
    return hits.size() == 1 ? hits.front() : 0;
}
template<size_t N>
uintptr_t removal_capture(uintptr_t at, const item_removal_contracts::Word (&pattern)[N], item_removal_contracts::Role role) {
    if (at) for (size_t i = 0; i < N; ++i) if (pattern[i].role == role) return at + i * 4;
    return 0;
}
template<size_t N>
uintptr_t removal_call(uintptr_t at, const item_removal_contracts::Word (&pattern)[N], item_removal_contracts::Role role) {
    const uintptr_t pc = removal_capture(at, pattern, role);
    uintptr_t target = 0;
    return pc && decode_branch_target(pc, (read_u32(pc) & 0x80000000U) != 0, &target) ? target : 0;
}
template<size_t N>
size_t removal_add(uintptr_t at, const item_removal_contracts::Word (&pattern)[N], item_removal_contracts::Role role) {
    const uintptr_t pc = removal_capture(at, pattern, role);
    return pc ? (read_u32(pc) >> 10U) & 0xfffU : SIZE_MAX;
}
template<size_t N>
size_t removal_slot(uintptr_t at, const item_removal_contracts::Word (&pattern)[N], item_removal_contracts::Role role) {
    const uintptr_t pc = removal_capture(at, pattern, role);
    return pc ? item_catalog::unsigned_offset(read_u32(pc), 3) : SIZE_MAX;
}

InstanceLayout resolve_instance_layout(const std::vector<MemoryRange> &r, const std::vector<MemoryRange> &x,
                                      InstanceKind kind, size_t amount_slot, size_t change_slot) {
    using namespace item_removal_contracts;
    const char *names[] = {"N6toybox21DomainEquipmentSpecieE", "N6toybox24DomainPetEquipmentSpecieE",
                          "N6toybox26DomainBuddyEquipmentSpecieE", "N6toybox22DomainUnknownEquipmentE"};
    InstanceLayout out;
    out.vtable = unique_primary_vtable(r, x, names[static_cast<size_t>(kind)]);
    if (!out.vtable || !read_range_ptr(r, out.vtable + amount_slot, &out.amount) ||
        !read_range_ptr(r, out.vtable + change_slot, &out.change) || !range_contains(x, out.amount, 4) ||
        !range_contains(x, out.change, 4)) return out;
    auto contract = [&](uintptr_t f, size_t n, const auto &p) { return removal_contract(r, x, f, n, p); };
    size_t source = SIZE_MAX, member = SIZE_MAX;
    uintptr_t copier = 0;
    if (kind == InstanceKind::Equipment || kind == InstanceKind::Pet) {
        const auto amount = contract(out.amount, 512, stock_amount);
        const auto dispose = contract(out.amount, 512, stock_dispose);
        if (!amount || !dispose) return out;
        out.repository = removal_call(amount, stock_amount, Role::repository);
        out.query = removal_call(amount, stock_amount, Role::query);
        copier = removal_call(amount, stock_amount, Role::secure_copy);
        source = removal_add(amount, stock_amount, Role::source_field);
        member = removal_add(amount, stock_amount, Role::input_field);
        out.dispose = removal_call(dispose, stock_dispose, Role::dispose);
        if (kind == InstanceKind::Pet) {
            const auto decoder = contract(out.query, 256, pet_query_decoder);
            if (!decoder || removal_add(decoder, pet_query_decoder, Role::input_field) != member) return out;
            out.decoder = removal_call(decoder, pet_query_decoder, Role::decoder);
        } else {
            const auto decoder = contract(out.query, 256, query_decoder);
            if (!decoder || removal_add(decoder, query_decoder, Role::input_field) != member) return out;
            out.decoder = removal_call(decoder, query_decoder, Role::decoder);
        }
        {
            const auto remove = contract(out.change, 4096, stock_remove);
            const auto notify = contract(out.change, 4096, stock_notify);
            if (!remove || !notify || removal_call(remove, stock_remove, Role::repository) != out.repository ||
                removal_call(remove, stock_remove, Role::secure_copy) != copier ||
                removal_call(remove, stock_remove, Role::copy2) != copier ||
                removal_add(remove, stock_remove, Role::input_field) != member ||
                removal_add(remove, stock_remove, Role::secure_field) != member) return out;
            out.remove = removal_call(remove, stock_remove, Role::remove);
            if (kind == InstanceKind::Equipment &&
                out.remove != injection_anchored_function(r, x, "can not remove initial weapon or armor! [equipmentStockId] [{}]")) return out;
            out.instance_secure = removal_add(remove, stock_remove, Role::source_field);
            out.notify_slot = removal_slot(notify, stock_notify, Role::notify_slot);
        }
    } else if (kind == InstanceKind::Buddy) {
        const auto amount = contract(out.amount, 512, buddy_amount);
        const auto dispose = contract(out.amount, 512, buddy_dispose);
        if (!amount || !dispose) return out;
        const auto wrapper = removal_call(amount, buddy_amount, Role::wrapper);
        const auto query = contract(wrapper, 512, buddy_query);
        const auto inner_site = contract(out.change, 1024, buddy_inner);
        const auto inner = removal_call(inner_site, buddy_inner, Role::inner);
        const auto remove = inner ? contract(inner, 4096, buddy_remove) : 0;
        const auto notify = contract(out.change, 1024, buddy_notify);
        if (!query || !remove || !notify) return out;
        copier = removal_call(amount, buddy_amount, Role::secure_copy);
        out.repository = removal_call(query, buddy_query, Role::repository);
        out.query = removal_call(query, buddy_query, Role::query);
        source = removal_add(amount, buddy_amount, Role::source_field);
        member = removal_add(query, buddy_query, Role::input_field);
        if (removal_add(amount, buddy_amount, Role::input_field) != member ||
            removal_add(remove, buddy_remove, Role::input_field) != member ||
            removal_call(query, buddy_query, Role::secure_copy) != copier ||
            removal_call(remove, buddy_remove, Role::secure_copy) != copier ||
            removal_call(remove, buddy_remove, Role::copy2) != copier ||
            removal_call(remove, buddy_remove, Role::repository) != out.repository) return out;
        out.remove = removal_call(remove, buddy_remove, Role::remove);
        out.dispose = removal_call(dispose, buddy_dispose, Role::dispose);
        out.instance_secure = removal_add(remove, buddy_remove, Role::source_field);
        out.notify_slot = removal_slot(notify, buddy_notify, Role::notify_slot);
        const auto decoder = contract(out.query, 256, query_decoder);
        if (!decoder || removal_add(decoder, query_decoder, Role::input_field) != member) return out;
        out.decoder = removal_call(decoder, query_decoder, Role::decoder);
    } else {
        const auto amount = contract(out.amount, 512, unknown_amount);
        if (!amount) return out;
        const auto wrapper = removal_call(amount, unknown_amount, Role::wrapper);
        const auto query = contract(wrapper, 512, unknown_query);
        const auto dispose = contract(wrapper, 512, unknown_dispose);
        if (!query || !dispose) return out;
        copier = removal_call(amount, unknown_amount, Role::secure_copy);
        if (removal_call(query, unknown_query, Role::secure_copy) != copier) return out;
        out.repository = removal_call(query, unknown_query, Role::repository);
        out.query = removal_call(query, unknown_query, Role::query);
        out.dispose = removal_call(dispose, unknown_dispose, Role::dispose);
        source = removal_add(amount, unknown_amount, Role::source_field);
        member = removal_add(query, unknown_query, Role::input_field);
        const auto predicate = contract(out.query, 1024, unknown_predicate);
        if (!predicate || removal_add(predicate, unknown_predicate, Role::input_field) != member ||
            removal_add(amount, unknown_amount, Role::input_field) != member) return out;
        out.decoder = removal_call(predicate, unknown_predicate, Role::decoder);
        const auto type_getter = removal_call(predicate, unknown_predicate, Role::type_getter);
        const auto type = contract(type_getter, removal_code_size(unknown_getter), unknown_getter);
        if (!type || removal_call(type, unknown_getter, Role::decoder) != out.decoder) return out;
        auto candidates = removal_candidates(r, x, 0, 0, unknown_remove);
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](uintptr_t at) {
            return removal_call(at, unknown_remove, Role::repository) != out.repository;
        }), candidates.end());
        if (candidates.size() != 1) return out;
        out.id_getter = removal_call(candidates.front(), unknown_remove, Role::id_getter);
        out.remove = removal_call(candidates.front(), unknown_remove, Role::remove);
        const auto id = contract(out.id_getter, removal_code_size(unknown_getter), unknown_getter);
        if (!id || removal_call(id, unknown_getter, Role::decoder) != out.decoder ||
            removal_slot(id, unknown_getter, Role::userdata_field) != removal_slot(type, unknown_getter, Role::userdata_field) ||
            removal_add(id, unknown_getter, Role::secure_field) == removal_add(type, unknown_getter, Role::secure_field)) return out;
    }
    if (member == SIZE_MAX || source < member || source > 65536 || !member || !copier ||
        !out.repository || !out.query || !out.dispose || !out.decoder) return out;
    out.species_id = source - member;
    out.species_secure = source;
    if (kind == InstanceKind::Equipment || kind == InstanceKind::Pet || kind == InstanceKind::Buddy) {
        if (out.instance_secure < member || out.instance_secure > 65536 || !out.notify_slot || out.notify_slot > 65536) return out;
        out.instance_id = out.instance_secure - member;
        uintptr_t notify = 0;
        if (!read_range_ptr(r, out.vtable + out.notify_slot, &notify) || !range_contains(x, notify, 4)) return out;
    }
    for (uintptr_t address : {out.repository, out.query, out.dispose, out.decoder})
        if (!range_contains(x, address, 4)) return out;
    out.ready = kind != InstanceKind::Pet && out.remove && range_contains(x, out.remove, 4);
    return out;
}

#include "item_count_runtime.inc"
#include "item_pet_runtime.inc"
#include "item_creation_runtime.inc"
#include "item_pet_creation_resolver.inc"

void resolve_item_removals(const std::vector<MemoryRange> &readable, const std::vector<MemoryRange> &exec,
                           size_t amount_slot, size_t change_slot) {
    for (size_t i = 0; i < static_cast<size_t>(InstanceKind::Count); ++i) {
        g_instance_layouts[i] = resolve_instance_layout(readable, exec, static_cast<InstanceKind>(i), amount_slot, change_slot);
        ALOGI("AE_INJECTION removal kind=%zu ready=%d", i, g_instance_layouts[i].ready);
    }
    auto &pet = g_instance_layouts[static_cast<size_t>(InstanceKind::Pet)];
    for (size_t i = 0; i < static_cast<size_t>(InstanceKind::Count); ++i)
        g_instance_layouts[i].count = resolve_instance_count(readable, exec, static_cast<InstanceKind>(i), g_instance_layouts[i]);
    g_pet_storage = resolve_pet_storage(readable, exec, pet, g_instance_layouts[static_cast<size_t>(InstanceKind::Buddy)]);
    pet.ready = g_pet_storage.ready;
    resolve_instance_creation(readable, exec);
    g_pet_creation=resolve_pet_creation(readable,exec);
}

// The verified native query ABI returns an owning vector through X8. Only the
// host's disposal adapter may release its buffer/control blocks. A snapshot is
// confined to one game-thread slice; addresses are never retained as identities.
