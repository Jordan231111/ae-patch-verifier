// Generated verbatim from the production byte-mask scanner.
#pragma once
std::vector<uintptr_t> find_masked_pattern(const std::vector<MemoryRange> &ranges,
                                           const uint8_t *pattern,
                                           const uint8_t *mask,
                                           size_t len) {
    std::vector<uintptr_t> hits;
    if (pattern == nullptr || mask == nullptr || len == 0) return hits;
    for_each_snapshot_segment(ranges, [&](uintptr_t live_base, const uint8_t *data, size_t seg_len) {
        if (seg_len < len) return;
        size_t last = seg_len - len;  // last valid start offset within the segment
        for (size_t i = 0; i <= last; ++i) {
            if (mask[0] != 0 && data[i] != pattern[0]) continue;  // first-byte prefilter
            bool matched = true;
            for (size_t j = 1; j < len; ++j) {
                if (mask[j] != 0 && data[i + j] != pattern[j]) { matched = false; break; }
            }
            if (matched) hits.push_back(live_base + i);
        }
    });
    return hits;
}
