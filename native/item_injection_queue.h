#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace item_injection {
inline bool separator(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == ',' || c == ';' || c == '[' || c == ']';
}
inline bool whitespace(unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
enum class Outcome { Pending, Granted, Unavailable };
struct Entry {
    uint32_t id = 0;
    // Signed requested and verified inventory deltas.
    int64_t quantity = 0, goal = 0, granted = 0;
    int32_t initial = -1;
    bool equipment = false;
    Outcome outcome = Outcome::Pending;
    std::string error;
};
inline bool number(const std::string &s, size_t &at, uint64_t &value) {
    if (at < s.size() && s[at] == '+') ++at;
    size_t first = at; value = 0;
    while (at < s.size() && s[at] >= '0' && s[at] <= '9') {
        unsigned digit = static_cast<unsigned>(s[at++] - '0');
        if (value > (UINT64_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return at != first && value != 0;
}
inline uint64_t magnitude(int64_t value) {
    return value < 0 ? uint64_t{0} - static_cast<uint64_t>(value) : static_cast<uint64_t>(value);
}
inline bool signed_quantity(const std::string &s, size_t &at, int64_t &value) {
    bool negative = at < s.size() && s[at] == '-';
    if (negative) ++at;
    if (negative && at < s.size() && (s[at] == '+' || s[at] == '-')) return false;
    uint64_t absolute = 0;
    if (!number(s, at, absolute)) return false;
    const uint64_t limit = static_cast<uint64_t>(INT64_MAX) + (negative ? 1U : 0U);
    if (absolute > limit) return false;
    value = negative ? (absolute == limit ? INT64_MIN : -static_cast<int64_t>(absolute))
                     : static_cast<int64_t>(absolute);
    return true;
}
// id[:quantity], comma/whitespace separated. Duplicate IDs merge quantities in
// first-seen order. Signed deltas net out; a net-zero entry changes no inventory.
// Parsing validates the entire request before any game operation.
inline bool parse_items(const std::string &raw, int64_t default_quantity,
                        std::vector<Entry> &out, std::string &error) {
    out.clear(); error.clear();
    if (default_quantity == 0) { error = "Default quantity must not be zero."; return false; }
    std::unordered_map<uint32_t, size_t> positions;
    size_t at = 0;
    while (at < raw.size()) {
        if (separator(static_cast<unsigned char>(raw[at]))) { ++at; continue; }
        uint64_t id = 0;
        int64_t quantity = default_quantity;
        if (!number(raw, at, id) || id > INT32_MAX) { error = "Invalid item ID."; return false; }
        while (at < raw.size() && whitespace(static_cast<unsigned char>(raw[at]))) ++at;
        if (at < raw.size() && (raw[at] == ':' || raw[at] == 'x' || raw[at] == '*')) {
            ++at;
            while (at < raw.size() && whitespace(static_cast<unsigned char>(raw[at]))) ++at;
            if (!signed_quantity(raw, at, quantity)) { error = "Invalid item quantity."; return false; }
            if (at < raw.size() && !separator(static_cast<unsigned char>(raw[at]))) {
                error = "Separate entries with commas or newlines."; return false;
            }
        } else if (at < raw.size() && !separator(static_cast<unsigned char>(raw[at])) &&
                   !(raw[at] >= '0' && raw[at] <= '9')) {
            error = "Use itemID or itemID:quantity."; return false;
        }
        auto [it, added] = positions.emplace(static_cast<uint32_t>(id), out.size());
        if (added) { Entry entry; entry.id = static_cast<uint32_t>(id); entry.quantity = quantity; out.push_back(std::move(entry)); }
        else {
            auto &entry = out[it->second];
            if ((quantity > 0 && entry.quantity > INT64_MAX - quantity) ||
                (quantity < 0 && entry.quantity < INT64_MIN - quantity)) {
                error = "Combined quantity overflows the numeric range."; return false;
            }
            entry.quantity += quantity;
        }
    }
    if (out.empty()) { error = "Enter at least one item ID."; return false; }
    return true;
}
enum class Phase { Idle, Checking, Running, Complete, Partial, Stopped, Failed };
struct Batch {
    Phase phase = Phase::Idle;
    std::vector<Entry> entries;
    size_t index = 0, checked = 0, completed = 0, unavailable = 0;
    uint32_t generation = 0;
    bool waiting_resources = false;
    std::string message = "Ready. Positive quantities add; negative quantities remove.";
    bool active() const { return phase == Phase::Checking || phase == Phase::Running; }
    bool start(const std::string &raw, int64_t quantity) {
        if (active()) return false;
        std::vector<Entry> parsed;
        std::string error;
        if (!parse_items(raw, quantity, parsed, error)) {
            entries.clear(); index = checked = completed = unavailable = 0;
            phase = Phase::Failed; message = error; return false;
        }
        std::string ready = "Checking item IDs, inventory and requested changes.";
        for (auto &entry : parsed) entry.goal = entry.quantity;
        entries = std::move(parsed); index = checked = completed = unavailable = 0;
        generation = generation == INT32_MAX ? 1 : generation + 1;
        waiting_resources = false; phase = Phase::Checking;
        message = std::move(ready);
        return true;
    }
    void fail(std::string reason) { phase = Phase::Failed; message = std::move(reason); }
    void stop() {
        if (!active()) return;
        phase = Phase::Stopped; waiting_resources = false;
        message = "Stopped. Already verified inventory changes are retained.";
    }
    void reject(Entry &entry, std::string error) {
        entry.outcome = Outcome::Unavailable; entry.error = std::move(error); ++unavailable;
    }
    void advance() {
        while (index < entries.size() && entries[index].outcome != Outcome::Pending) ++index;
        if (index == entries.size()) {
            phase = unavailable == 0 ? Phase::Complete : Phase::Partial;
            waiting_resources = false;
            message = unavailable == 0 ? "Inventory changes complete." : "Run finished with unavailable items. Open Results for details.";
        }
    }
    bool acknowledge(int32_t delta, int32_t before, int32_t after) {
        if (phase != Phase::Running || index >= entries.size() || delta == 0) return false;
        const auto &entry = entries[index];
        if ((delta > 0) != (entry.quantity > 0) ||
            magnitude(delta) > magnitude(entry.goal - entry.granted)) return false;
        if (before < 0 || after < 0 || static_cast<int64_t>(after) - before != delta) {
            fail("Unexpected inventory result. Stopped; check Results before retrying."); return false;
        }
        entries[index].granted += delta;
        if (entries[index].granted == entries[index].goal) {
            entries[index].outcome = Outcome::Granted; ++completed; ++index; advance();
        }
        return true;
    }
};
} // namespace item_injection
