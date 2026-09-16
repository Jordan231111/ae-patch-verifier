#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

struct MemoryRange { uintptr_t start, end; bool executable, writable; };
bool memory_matches_mask(uintptr_t address, const uint8_t *pattern, const uint8_t *mask, size_t length) {
    if (!address || !pattern || !mask || !length) return false;
    auto bytes = reinterpret_cast<const uint8_t *>(address);
    for (size_t i = 0; i < length; ++i) if (mask[i] && bytes[i] != pattern[i]) return false;
    return true;
}
template<class Visit> void for_each_snapshot_segment(const std::vector<MemoryRange> &ranges, Visit visit) {
    for (const auto &range : ranges) visit(range.start, reinterpret_cast<const uint8_t *>(range.start), range.end - range.start);
}
#include "byte-scan.h"

int main() {
    uint32_t seed = 0x5a831fc1;
    auto random = [&]() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
    for (unsigned trial = 0; trial < 4000; ++trial) {
        const size_t size = random() % 128, length = 1 + random() % 24;
        std::vector<uint8_t> bytes(size), pattern(length), mask(length);
        for (auto &b : bytes) b = random() % 4;
        for (auto &b : pattern) b = random() % 4;
        for (auto &b : mask) b = (random() % 3) ? static_cast<uint8_t>(1 + random() % 255) : 0;
        const auto base = reinterpret_cast<uintptr_t>(bytes.data());
        std::vector<MemoryRange> ranges{{base, base + bytes.size(), true, false}};
        std::vector<uintptr_t> expected;
        if (size >= length) for (size_t at = 0; at <= size - length; ++at) {
            bool match = true;
            for (size_t j = 0; j < length; ++j) {
                if (mask[j] && bytes[at + j] != pattern[j]) match = false;
            }
            if (match) expected.push_back(base + at);
        }
        assert(find_masked_pattern(ranges, pattern.data(), mask.data(), length) == expected);
    }
    uint8_t repeated[]{7, 7, 7, 7}, pattern[]{7, 7}, mask[]{0, 255};
    auto base = reinterpret_cast<uintptr_t>(repeated);
    std::vector<MemoryRange> ranges{{base, base + sizeof(repeated), true, false}};
    assert(find_masked_pattern(ranges, pattern, mask, 2) == std::vector<uintptr_t>({base, base + 1, base + 2}));
    assert(find_masked_pattern(ranges, pattern, mask, 0).empty());
    assert(find_masked_pattern(ranges, nullptr, mask, 2).empty());
    assert(find_masked_pattern(ranges, pattern, nullptr, 2).empty());
}
