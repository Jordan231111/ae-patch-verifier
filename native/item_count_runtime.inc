#include "item_count_contracts.h"

template<size_t N>
int64_t count_pair_offset(uintptr_t at, const item_removal_contracts::Word (&pattern)[N], item_removal_contracts::Role role) {
    const auto pc = removal_capture(at, pattern, role);
    return pc ? item_catalog::signed_bits((read_u32(pc) >> 15U) & 127U, 7) * 8 : -1;
}
InstanceCountLayout resolve_instance_count(const std::vector<MemoryRange> &r, const std::vector<MemoryRange> &x,
                                           InstanceKind kind, const InstanceLayout &item) {
    using namespace item_removal_contracts;
    InstanceCountLayout out;
    if (!item.query || !item.decoder || !item.species_secure) return out;
    auto contract = [&](uintptr_t f, size_t n, const auto &p) { return f ? removal_contract(r, x, f, n, p) : 0; };
    auto field = [](uintptr_t at, const auto &p, Role role, unsigned scale) {
        return item_catalog::unsigned_offset(read_u32(removal_capture(at, p, role)), scale);
    };
    if (kind == InstanceKind::Unknown) {
        const auto live = contract(item.query, 1024, count_unknown_live);
        const auto predicate = contract(item.query, 1024, unknown_predicate);
        if (!live || !predicate || removal_call(predicate, unknown_predicate, Role::decoder) != item.decoder) return out;
        const auto offset = count_pair_offset(live, count_unknown_live, Role::source_field);
        if (offset < 0) return out;
        out.vector = static_cast<size_t>(offset);
        out.bool_getter = removal_call(predicate, unknown_predicate, Role::bool_getter);
        out.type_getter = removal_call(predicate, unknown_predicate, Role::type_getter);
        out.valid = range_contains(x, out.bool_getter, 4) && range_contains(x, out.type_getter, 4);
        return out;
    }
    if (kind == InstanceKind::Pet) {
        const auto map = contract(item.query, 512, count_pet_map);
        const auto op = removal_call(map, count_pet_map, Role::inner);
        const auto tree = contract(op, 1024, count_pet_tree);
        const auto node = contract(op, 1024, count_pet_node);
        if (!map || !tree || !node) return out;
        const auto root = item_catalog::signed_bits((read_u32(removal_capture(tree, count_pet_tree, Role::root)) >> 12U) & 511U, 9);
        const auto vector = count_pair_offset(map, count_pet_map, Role::payload);
        if (root < 0 || vector < 0) return out;
        out.root = removal_add(map, count_pet_map, Role::map_field) + static_cast<size_t>(root);
        out.vector = static_cast<size_t>(vector);
        out.key = field(tree, count_pet_tree, Role::key, 2);
        out.left = field(tree, count_pet_tree, Role::left, 3);
        out.right = field(tree, count_pet_tree, Role::right, 3);
        if (out.right != removal_add(tree, count_pet_tree, Role::source_field) ||
            out.key != field(node, count_pet_node, Role::key, 2) ||
            out.vector != field(node, count_pet_node, Role::payload, 3) ||
            count_pair_offset(node, count_pet_node, Role::end_field) != static_cast<int64_t>(out.vector + sizeof(uintptr_t))) return out;
    } else {
        const auto tree = contract(item.query, 512, count_tree);
        const auto at = contract(item.query, 512, count_query_at);
        const auto copy = contract(item.query, 512, count_copy);
        if (!tree || !at || !copy || removal_call(at, count_query_at, Role::decoder) != item.decoder) return out;
        const auto getter = removal_call(at, count_query_at, Role::inner);
        const auto search = contract(getter, 512, count_at_tree);
        const auto value = contract(getter, 512, count_at_value);
        const auto vector_begin = count_pair_offset(copy, count_copy, Role::source_field);
        if (!search || !value || vector_begin < 0) return out;
        out.root = field(tree, count_tree, Role::root, 3);
        out.key = field(tree, count_tree, Role::key, 2);
        out.left = field(tree, count_tree, Role::left, 3);
        out.right = removal_add(tree, count_tree, Role::right) + out.left;
        out.vector = removal_add(value, count_at_value, Role::payload) + static_cast<size_t>(vector_begin);
        if (out.root != removal_add(at, count_query_at, Role::map_field) + field(search, count_at_tree, Role::root, 3) ||
            out.key != field(search, count_at_tree, Role::key, 2) ||
            out.left != field(search, count_at_tree, Role::left, 3) || out.right != field(search, count_at_tree, Role::right, 3)) return out;
    }
    out.valid = std::max({out.root, out.key, out.left, out.right, out.vector}) < 4096 && out.left != out.right;
    return out;
}

template<class T>
T instance_member(uintptr_t address) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void *>(address), sizeof(value));
    return value;
}
