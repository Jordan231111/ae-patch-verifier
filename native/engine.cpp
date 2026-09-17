// Generated from the production module by scripts/build-native-engine.py.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <memory>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <vector>
#include <string>
#include <unordered_map>
#include <sys/types.h>
#include "elf-abi.h"
#include "elf_unwind_bounds.h"
#include "item_injection_contracts.h"
#include "item_injection_queue_test.h"
#include "item_catalog_signatures.h"
#define ALOGI(...) do { printf(__VA_ARGS__); puts(""); } while(0)
#define ALOGW(...) ALOGI(__VA_ARGS__)
struct MemoryRange {uintptr_t start,end;bool executable,writable;};
uintptr_t audit_base=0;size_t writes=0;std::vector<MemoryRange> audit_ranges,audit_readable;
constexpr const char *kTargetLib="libapp.so";
uintptr_t find_module_base(const char *){return audit_base;}
uintptr_t decode_adrp_ldr_global(uintptr_t,uintptr_t);
std::unordered_map<std::string,uintptr_t> audit_symbols;
void *resolve_app_symbol(const char *n){return (void*)audit_symbols[n];}
std::vector<MemoryRange> app_exec_ranges(){return audit_ranges;}
std::vector<MemoryRange> app_readable_ranges(){return audit_readable;}
struct ScanSnapshot {bool read(uintptr_t,void*,size_t) const{return false;}};
thread_local ScanSnapshot *t_active_scan_snapshot=nullptr;
std::mutex g_item_layout_mutex;std::atomic<bool> g_item_layout_ready{false};item_catalog::Layout g_item_layout{};
std::atomic<bool> g_object_layouts_ready{false};item_catalog::ObjectLayouts g_object_layouts{};
struct ItemNameRet {uint64_t w0,w1,w2;};
void append_item_dump_log(const char *kind,int,int,int,const char *d){printf("LAYOUT %s %s\n",kind,d);}
ssize_t vm_read_partial(uintptr_t a,void *p,size_t n){memcpy(p,(void*)a,n);return n;}
template<typename T> void for_each_snapshot_segment(const std::vector<MemoryRange>& rs,T visit){for(auto r:rs)visit(r.start,(const uint8_t*)r.start,r.end-r.start);}

bool checked_address_end(uintptr_t address, size_t len, uintptr_t *end) {
    if (end == nullptr || address == 0 || len == 0 || len > UINTPTR_MAX - address) return false;
    *end = address + len;
    return true;
}

bool range_contains(const std::vector<MemoryRange> &ranges, uintptr_t address, size_t len) {
    uintptr_t end = 0;
    if (!checked_address_end(address, len, &end)) return false;
    for (const MemoryRange &range: ranges) {
        if (address >= range.start && end <= range.end) return true;
    }
    return false;
}

void push_unique(std::vector<uintptr_t> &items, uintptr_t value) {
    for (uintptr_t item: items) {
        if (item == value) return;
    }
    items.push_back(value);
}

uint32_t read_u32(uintptr_t address) {
    uint32_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void *>(address), sizeof(value));
    return value;
}

bool decode_branch_target(uintptr_t source, bool link_expected, uintptr_t *target) {
    uint32_t instruction = read_u32(source);
    uint32_t opcode = instruction & 0xFC000000U;
    uint32_t expected = link_expected ? 0x94000000U : 0x14000000U;
    if (opcode != expected) return false;

    int32_t imm26 = static_cast<int32_t>(instruction & 0x03FFFFFFU);
    if (imm26 >= 0x02000000) imm26 -= 0x04000000;
    *target = source + static_cast<int64_t>(imm26) * 4;
    return true;
}

bool memory_matches_mask(uintptr_t address, const uint8_t *pattern, const uint8_t *mask, size_t len) {
    if (address == 0 || pattern == nullptr || mask == nullptr || len == 0) return false;
    const uint8_t *data = reinterpret_cast<const uint8_t *>(address);
    for (size_t i = 0; i < len; ++i) {
        if (mask[i] != 0 && data[i] != pattern[i]) return false;
    }
    return true;
}

bool memory_equals(uintptr_t address, const uint8_t *bytes, size_t len) {
    if (address == 0 || bytes == nullptr || len == 0) return false;
    return std::memcmp(reinterpret_cast<const void *>(address), bytes, len) == 0;
}

std::vector<uintptr_t> find_pattern(const std::vector<MemoryRange>& rs,const uint8_t*p,size_t n){std::vector<uintptr_t> h;if(!p||!n)return h;for(auto r:rs){auto b=(const uint8_t*)r.start,e=(const uint8_t*)r.end;while(b+n<=e){auto q=std::search(b,e,p,p+n);if(q==e)break;h.push_back((uintptr_t)q);b=q+1;}}return h;}

#include "byte-scan.h"

std::vector<uint8_t> code_pattern_mask(const uint8_t *pattern, size_t len, const uint8_t *mask) {
    std::vector<uint8_t> result(len, 0xFF);
    if (mask != nullptr) std::copy_n(mask, len, result.begin());
    for (size_t i = 0; i + 4 <= len; i += 4) {
        uint32_t w = 0, keep = UINT32_MAX;
        std::memcpy(&w, pattern + i, sizeof(w));
        const unsigned base = (w >> 5) & 31U;
        if (base != 31 && base != 29) {
            if ((w & 0x3B000000U) == 0x39000000U) keep &= ~0x003FFC00U; // LDR/STR unsigned
            if ((w & 0x3B200000U) == 0x38000000U) keep &= ~0x001FF000U; // LDUR/STUR, pre/post
            if ((w & 0x3A000000U) == 0x28000000U) keep &= ~0x003F8000U; // LDP/STP
            if ((w & 0x7F000000U) == 0x11000000U) keep &= ~0x003FFC00U; // member ADD
        }
        if ((w & 0x7C000000U) == 0x14000000U) keep &= 0xFC000000U; // B/BL
        if ((w & 0xFF000010U) == 0x54000000U || (w & 0x7E000000U) == 0x34000000U) keep &= ~0x00FFFFE0U;
        if ((w & 0x7E000000U) == 0x36000000U) keep &= ~0x0007FFE0U;
        if ((w & 0x9F000000U) == 0x90000000U || (w & 0x9F000000U) == 0x10000000U) keep &= 0x9F00001FU;
        for (size_t j = 0; j < 4; ++j) result[i + j] &= static_cast<uint8_t>(keep >> (j * 8));
    }
    return result;
}

std::vector<uintptr_t> find_code_pattern(const std::vector<MemoryRange> &ranges,
                                        const uint8_t *pattern, size_t len, const uint8_t *mask = nullptr) {
    std::vector<uintptr_t> hits;
    if (pattern == nullptr || len == 0 || len % 4 != 0) return hits;
    const auto effective = code_pattern_mask(pattern, len, mask);
    size_t anchor = 0;
    for (size_t i = 0; i < len; ++i) if (effective[i] == 0xFF) { anchor = i; break; }
    for_each_snapshot_segment(ranges, [&](uintptr_t live, const uint8_t *data, size_t size) {
        if (size < len) return;
        for (size_t i = (4 - (live & 3U)) & 3U; i <= size - len; i += 4) {
            if ((data[i + anchor] & effective[anchor]) != (pattern[anchor] & effective[anchor])) continue;
            bool match = true;
            for (size_t j = 0; j < len; ++j)
                if ((data[i + j] & effective[j]) != (pattern[j] & effective[j])) { match = false; break; }
            if (match) hits.push_back(live + i);
        }
    });
    return hits;
}

static constexpr uint8_t sig_item_writer_pattern[] = {
        0xFD, 0x7B, 0xBE, 0xA9, 0xF4, 0x4F, 0x01, 0xA9,
        0xFD, 0x03, 0x00, 0x91, 0xF3, 0x03, 0x03, 0x2A,
        0xF4, 0x03, 0x00, 0xAA, 0x00, 0x00, 0x00, 0x00
};
static constexpr uint8_t sig_item_writer_pattern_mask[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
static constexpr uint8_t sig_reward_writer[] = {
        0xFF, 0x43, 0x07, 0xD1, 0xFD, 0x7B, 0x18, 0xA9,
        0xFC, 0x67, 0x19, 0xA9, 0xF8, 0x5F, 0x1A, 0xA9,
        0xF6, 0x57, 0x1B, 0xA9, 0xF4, 0x4F, 0x1C, 0xA9,
        0xFD, 0x03, 0x06, 0x91, 0x58, 0xD0, 0x3B, 0xD5,
        0xF4, 0x03, 0x03, 0x2A
};
static constexpr uint8_t sig_quest_reward_pattern[] = {
        0xFF, 0x03, 0x04, 0xD1, 0xFD, 0x7B, 0x0C, 0xA9,
        0xF7, 0x6B, 0x00, 0xF9, 0xF6, 0x57, 0x0E, 0xA9,
        0xF4, 0x4F, 0x0F, 0xA9, 0xFD, 0x03, 0x03, 0x91,
        0x57, 0xD0, 0x3B, 0xD5, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xE8, 0x16, 0x40, 0xF9,
        0x01, 0x01, 0x80, 0x52, 0xE2, 0x03, 0x1F, 0x2A,
        0xA8, 0x83, 0x1F, 0xF8, 0x00, 0x18, 0x40, 0xF9,
        0x00, 0x00, 0x00, 0x00, 0x1F, 0x0C, 0x00, 0x71
};
static constexpr uint8_t sig_quest_reward_pattern_mask[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF
};
static constexpr uint8_t sig_domain_item_set_amount[] = {
        0xFF, 0x43, 0x02, 0xD1, 0xFD, 0x7B, 0x05, 0xA9,
        0xF7, 0x33, 0x00, 0xF9, 0xF6, 0x57, 0x07, 0xA9,
        0xF4, 0x4F, 0x08, 0xA9, 0xFD, 0x43, 0x01, 0x91,
        0x56, 0xD0, 0x3B, 0xD5, 0x17, 0x40, 0x99, 0x52
};
static constexpr uint8_t sig_add_pc_exp[] = {
        0xFD, 0x7B, 0xBD, 0xA9, 0xF6, 0x57, 0x01, 0xA9,
        0xF4, 0x4F, 0x02, 0xA9, 0xFD, 0x03, 0x00, 0x91,
        0x15, 0x1C, 0x40, 0xF9, 0xF4, 0x03, 0x00, 0xAA
};
static constexpr uint8_t sig_cat_scratch_stamp_total_pattern[] = {
        0xFF, 0x83, 0x01, 0xD1, 0xFD, 0x7B, 0x03, 0xA9,
        0xF6, 0x57, 0x04, 0xA9, 0xF4, 0x4F, 0x05, 0xA9,
        0xFD, 0xC3, 0x00, 0x91, 0x55, 0xD0, 0x3B, 0xD5,
        0xF3, 0x03, 0x00, 0xAA, 0xE0, 0x03, 0x00, 0x91,
        0xA8, 0x16, 0x40, 0xF9, 0xF4, 0x03, 0x01, 0x2A,
        0xF6, 0x03, 0x00, 0x91, 0xA8, 0x83, 0x1F, 0xF8,
        0xFF, 0x03, 0x00, 0xF9, 0xFF, 0x7F, 0x01, 0xA9,
        0xFF, 0x13, 0x00, 0xF9, 0x00, 0x00, 0x00, 0x00,
        0xE0, 0x03, 0x00, 0x91, 0xE1, 0x03, 0x14, 0x2A,
        0x00, 0x00, 0x00, 0x00, 0x74, 0x02, 0x02, 0x91,
        0x9F, 0x02, 0x16, 0xEB, 0xC0, 0x00, 0x00, 0x54,
        0xE0, 0x03, 0x00, 0x91, 0x00, 0x00, 0x00, 0x00,
        0xE1, 0x03, 0x00, 0x2A, 0xE0, 0x03, 0x14, 0xAA,
        0x00, 0x00, 0x00, 0x00, 0xE0, 0x03, 0x40, 0xF9,
        0x28, 0x00, 0x80, 0x52, 0x68, 0xC2, 0x04, 0x39
};
static constexpr uint8_t sig_cat_scratch_stamp_total_pattern_mask[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
static constexpr uint8_t sig_domain_token_shop_set_total[] = {
        0xff, 0x83, 0x01, 0xd1, 0xfd, 0x7b, 0x04, 0xa9, 0xf4, 0x4f, 0x05, 0xa9,
        0xfd, 0x03, 0x01, 0x91, 0x54, 0xd0, 0x3b, 0xd5, 0x88, 0x16, 0x40, 0xf9,
        0xa8, 0x83, 0x1f, 0xf8, 0x82, 0x04, 0x00, 0x34, 0xf3, 0x03, 0x01, 0x2a
};
static constexpr uint8_t sig_domain_token_shop_reset_total[] = {
        0xff, 0x03, 0x02, 0xd1, 0xfd, 0x7b, 0x06, 0xa9, 0xf4, 0x4f, 0x07, 0xa9,
        0xfd, 0x83, 0x01, 0x91, 0x54, 0xd0, 0x3b, 0xd5, 0xf3, 0x03, 0x00, 0xaa,
        0x41, 0x02, 0x80, 0x52, 0x88, 0x16, 0x40, 0xf9
};
static constexpr uint8_t sig_token_shop_purchase[] = {
        0xff, 0x03, 0x04, 0xd1, 0xfd, 0x7b, 0x0b, 0xa9, 0xfa, 0x67, 0x0c, 0xa9,
        0xf8, 0x5f, 0x0d, 0xa9, 0xf6, 0x57, 0x0e, 0xa9, 0xf4, 0x4f, 0x0f, 0xa9,
        0xfd, 0xc3, 0x02, 0x91, 0x00, 0x00, 0x00, 0x00, 0xf4, 0x03, 0x00, 0xaa,
        0x08, 0x17, 0x40, 0xf9, 0xa8, 0x83, 0x1f, 0xf8, 0xa8, 0x63, 0x00, 0xd1,
        0x13, 0x04, 0x40, 0xf9, 0x60, 0x16, 0x42, 0xf9, 0x00, 0x00, 0x00, 0x00,
        0x60, 0x16, 0x42, 0xf9
};
static constexpr uint8_t mask_token_shop_purchase[] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x00, 0x00, 0xff
};
static constexpr uint8_t sig_appraisal_exchange_shop_purchase[] = {
        0xff, 0x03, 0x04, 0xd1, 0xfd, 0x7b, 0x0b, 0xa9, 0xfa, 0x67, 0x0c, 0xa9,
        0xf8, 0x5f, 0x0d, 0xa9, 0xf6, 0x57, 0x0e, 0xa9, 0xf4, 0x4f, 0x0f, 0xa9,
        0xfd, 0xc3, 0x02, 0x91, 0x00, 0x00, 0x00, 0x00, 0xf3, 0x03, 0x00, 0xaa,
        0xe8, 0x16, 0x40, 0xf9, 0xa8, 0x83, 0x1f, 0xf8, 0xa8, 0x63, 0x00, 0xd1,
        0x18, 0x04, 0x40, 0xf9, 0x00, 0x1b, 0x42, 0xf9, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x1b, 0x42, 0xf9
};
static constexpr uint8_t mask_appraisal_exchange_shop_purchase[] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00,
        0xff, 0x00, 0x00, 0xff
};
static constexpr uint8_t sig_token_shop_trade_complete[] = {
        0xff, 0x83, 0x04, 0xd1, 0xfd, 0x7b, 0x0c, 0xa9, 0xfc, 0x6f, 0x0d, 0xa9,
        0xfa, 0x67, 0x0e, 0xa9, 0xf8, 0x5f, 0x0f, 0xa9, 0xf6, 0x57, 0x10, 0xa9,
        0xf4, 0x4f, 0x11, 0xa9, 0xfd, 0x03, 0x03, 0x91, 0x00, 0x00, 0x00, 0x00,
        0xf3, 0x03, 0x00, 0xaa, 0xc8, 0x16, 0x40, 0xf9, 0xa8, 0x83, 0x1f, 0xf8,
        0x00, 0x14, 0x42, 0xf9, 0x00, 0x00, 0x00, 0x00, 0x17, 0x7c, 0x40, 0x92,
        0xff, 0x0a, 0x00, 0xf1
};
static constexpr uint8_t mask_token_shop_trade_complete[] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff
};
static constexpr uint8_t sig_field_talk_start_in_animation[] = {
        0xfd, 0x7b, 0x07, 0xa9, 0xf7, 0x43, 0x00, 0xf9, 0xf6, 0x57, 0x09, 0xa9,
        0xf4, 0x4f, 0x0a, 0xa9, 0xfd, 0xc3, 0x01, 0x91, 0x56, 0xd0, 0x3b, 0xd5,
        0xf3, 0x03, 0x00, 0xaa, 0xc8, 0x16, 0x40, 0xf9, 0xa8, 0x83, 0x1e, 0xf8,
        0x00, 0xe4, 0x41, 0xf9, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x40, 0xf9,
        0x08, 0x55, 0x40, 0xf9, 0x00, 0x01, 0x3f, 0xd6, 0x00, 0x00, 0x40, 0xfd,
        0xe0, 0x03, 0x80, 0x3d
};
static constexpr uint8_t mask_field_talk_start_in_animation[] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff
};
static constexpr uint8_t sig_field_talk_start_out_animation[] = {
        0xfd, 0x7b, 0x05, 0xa9, 0xf7, 0x33, 0x00, 0xf9, 0xf6, 0x57, 0x07, 0xa9,
        0xf4, 0x4f, 0x08, 0xa9, 0xfd, 0x43, 0x01, 0x91, 0x56, 0xd0, 0x3b, 0xd5,
        0xf3, 0x03, 0x00, 0xaa, 0xc8, 0x16, 0x40, 0xf9, 0xa8, 0x83, 0x1f, 0xf8,
        0x00, 0xe4, 0x41, 0xf9, 0x00, 0x00, 0x00, 0x00, 0x60, 0x06, 0x42, 0xfd,
        0x00, 0x00, 0x00, 0x00, 0xe1, 0x23, 0x00, 0x91, 0xe0, 0x07, 0x00, 0xfd,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf4, 0x03, 0x00, 0xaa
};
static constexpr uint8_t mask_field_talk_start_out_animation[] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff
};
static constexpr uint8_t sig_field_talk_on_end_in_animation[] = {
        0xfd, 0x7b, 0x07, 0xa9, 0xf7, 0x43, 0x00, 0xf9, 0xf6, 0x57, 0x09, 0xa9,
        0xf4, 0x4f, 0x0a, 0xa9, 0xfd, 0xc3, 0x01, 0x91, 0x56, 0xd0, 0x3b, 0xd5,
        0xf3, 0x03, 0x00, 0xaa, 0xc8, 0x16, 0x40, 0xf9, 0xa8, 0x83, 0x1f, 0xf8,
        0x08, 0x00, 0x42, 0xf9, 0x00, 0x00, 0x00, 0x00, 0x14, 0x85, 0x42, 0xf9,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x63, 0x00, 0x91,
        0x00, 0x00, 0x00, 0x00, 0xe1, 0x63, 0x00, 0x91, 0xe0, 0x03, 0x14, 0xaa,
        0x00, 0x00, 0x00, 0x00, 0xe8, 0x63, 0x40, 0x39
};
static constexpr uint8_t mask_field_talk_on_end_in_animation[] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff
};
constexpr const char *kAchievementFireEventTypeName = "N6toybox17DomainAchievement16FireTriggerEventE";
constexpr const char *kAchievementGameEventPrefix = "TB-GameEvent-";
constexpr const char *kAchievementRepositoryNotFoundText = "not found achievement [achievementLabel] [{}]";
constexpr const char *kAchievementRepositoryEndAssertText = "achievementIter != _achievementLabelToAchievementMap.end()";
struct DialogueNativeResolution {
    uintptr_t page_jump_delay = 0;
    uintptr_t talk_layer_run_auto_touch = 0;
    uintptr_t still_talk_layer_run_auto_touch = 0;
    uintptr_t get_auto_feed_mode = 0;
    uintptr_t get_auto_feed_wait_time_letter = 0;
    uintptr_t get_auto_feed_wait_time_minimum = 0;
    uintptr_t action_timer_value = 0;
    uintptr_t talk_ui_process_wait = 0;
    uintptr_t talk_layer_touch_handler = 0;
    uintptr_t rendering_checker = 0;
    uintptr_t page_jump_unlocked_offset = 0;
    uintptr_t talk_ui_state_talk_layer_offset = 0;
    uintptr_t talk_layer_main_window_offset = 0;
    uintptr_t talk_main_window_rendering_flag_offset = 0;
    uintptr_t talk_main_window_ready_offset = 0;
    uintptr_t talk_main_window_started_offset = 0;
    uintptr_t talk_main_window_aux_offset = 0;
};
struct DialoguePageJumpCandidate {
    uintptr_t address;
    uintptr_t unlocked_offset;
};
template<size_t N> bool read_item_contract(uintptr_t, const item_catalog::Word (&)[N], uint32_t (&)[N]);
template<size_t N> std::vector<uintptr_t> find_item_contract(const std::vector<MemoryRange> &, const item_catalog::Word (&)[N]);
template<size_t N> uint32_t item_role_word(const uint32_t (&)[N], const item_catalog::Word (&)[N], item_catalog::Role);
template<size_t N> uintptr_t scoped_layout_contract(const std::vector<MemoryRange> &, uintptr_t, const item_catalog::Word (&)[N], uint32_t (&)[N]);
uintptr_t find_function_start_before_ref(const std::vector<MemoryRange> &, uintptr_t);
void *resolve_app_symbol(const char *);


template<size_t N> uintptr_t layout_call_target(uintptr_t,const item_catalog::Word (&)[N],item_catalog::Role);

uintptr_t resolve_pattern_hook_address(const std::vector<MemoryRange> &ranges,
                                       const char *name,
                                       const uint8_t *signature,
                                       size_t signature_len) {
    std::vector<uintptr_t> hits = find_code_pattern(ranges, signature, signature_len);
    if (hits.size() != 1) {
        ALOGW("AE_TRACE func hook %s skipped signatureHits=%zu", name, hits.size());
        return 0;
    }
    return hits[0];
}

uintptr_t resolve_masked_pattern_hook_address(const std::vector<MemoryRange> &ranges,
                                              const char *name,
                                              const uint8_t *signature,
                                              const uint8_t *mask,
                                              size_t signature_len) {
    std::vector<uintptr_t> hits = find_code_pattern(ranges, signature, signature_len, mask);
    if (hits.size() != 1) {
        ALOGW("AE_TRACE func hook %s skipped maskedSignatureHits=%zu", name, hits.size());
        return 0;
    }
    return hits[0];
}

bool read_range_ptr(const std::vector<MemoryRange> &ranges, uintptr_t address, uintptr_t *out) {
    if (out == nullptr || !range_contains(ranges, address, sizeof(uintptr_t))) return false;
    // Fault-safe read: this runs in resolve_lua_registered_function on the same startup
    // crash path as the scanner, dereferencing libapp.so pointer-table slots while the
    // linker may still be reprotecting pages. process_vm_readv returns EFAULT instead of
    // faulting, so a transient unreadable slot is a clean miss, never a SIGSEGV.
    uintptr_t value = 0;
    if (vm_read_partial(address, &value, sizeof(value)) != static_cast<ssize_t>(sizeof(value))) return false;
    *out = value;
    return true;
}

int64_t sign_extend_u64(uint64_t value, unsigned bits) {
    if (bits == 0 || bits >= 64) return static_cast<int64_t>(value);
    uint64_t sign = 1ULL << (bits - 1U);
    return static_cast<int64_t>((value ^ sign) - sign);
}

bool decode_adr_target(uint32_t instruction, uintptr_t pc, uintptr_t *target) {
    if (target == nullptr || (instruction & 0x9F000000U) != 0x10000000U) return false;
    uint64_t immlo = (instruction >> 29U) & 0x3U;
    uint64_t immhi = (instruction >> 5U) & 0x7FFFFU;
    int64_t imm = sign_extend_u64((immhi << 2U) | immlo, 21);
    *target = static_cast<uintptr_t>(static_cast<int64_t>(pc) + imm);
    return true;
}

bool decode_adrp_page(uint32_t instruction, uintptr_t pc, uintptr_t *page, uint32_t *reg) {
    if (page == nullptr || reg == nullptr || (instruction & 0x9F000000U) != 0x90000000U) return false;
    uint64_t immlo = (instruction >> 29U) & 0x3U;
    uint64_t immhi = (instruction >> 5U) & 0x7FFFFU;
    int64_t imm = sign_extend_u64((immhi << 2U) | immlo, 21) << 12;
    uintptr_t pc_page = pc & ~static_cast<uintptr_t>(0xFFF);
    *page = static_cast<uintptr_t>(static_cast<int64_t>(pc_page) + imm);
    *reg = instruction & 0x1FU;
    return true;
}

bool decode_add_immediate(uint32_t instruction, uint32_t base_reg, uintptr_t base, uintptr_t *target) {
    if (target == nullptr || (instruction & 0x7F000000U) != 0x11000000U) return false;
    uint32_t rn = (instruction >> 5U) & 0x1FU;
    if (rn != base_reg) return false;
    uint64_t imm = (instruction >> 10U) & 0xFFFU;
    if ((instruction & (1U << 22U)) != 0) imm <<= 12U;
    *target = base + static_cast<uintptr_t>(imm);
    return true;
}

bool pc_relative_sequence_references(const std::vector<MemoryRange> &ranges,
                                     uintptr_t pc,
                                     uintptr_t target) {
    if (!range_contains(ranges, pc, 4)) return false;
    uint32_t instruction = read_u32(pc);

    uintptr_t adr_target = 0;
    if (decode_adr_target(instruction, pc, &adr_target) && adr_target == target) {
        return true;
    }

    uintptr_t adrp_page = 0;
    uint32_t reg = 0;
    if (!decode_adrp_page(instruction, pc, &adrp_page, &reg)) return false;

    for (uintptr_t next = pc + 4; next <= pc + 32; next += 4) {
        if (!range_contains(ranges, next, 4)) break;
        uintptr_t add_target = 0;
        if (decode_add_immediate(read_u32(next), reg, adrp_page, &add_target) && add_target == target) {
            return true;
        }
    }
    return false;
}

bool function_references_address(const std::vector<MemoryRange> &ranges,
                                 uintptr_t function_start,
                                 size_t max_bytes,
                                 uintptr_t target) {
    if (function_start == 0 || target == 0) return false;
    uintptr_t end = function_start + max_bytes;
    if (end < function_start) return false;
    for (uintptr_t pc = function_start; pc + 4 <= end; pc += 4) {
        if (!range_contains(ranges, pc, 4)) break;
        if (pc_relative_sequence_references(ranges, pc, target)) return true;
    }
    return false;
}

std::vector<uintptr_t> find_pc_relative_refs(const std::vector<MemoryRange> &ranges, uintptr_t target) {
    std::vector<uintptr_t> refs;
    if (target == 0) return refs;
    for (const MemoryRange &range: ranges) {
        uintptr_t pc = (range.start + 3U) & ~static_cast<uintptr_t>(3U);
        while (pc + 4 <= range.end) {
            if (pc_relative_sequence_references(ranges, pc, target)) {
                push_unique(refs, pc);
            }
            pc += 4;
        }
    }
    return refs;
}

bool is_sub_sp_start(uint32_t instruction) {
    return (instruction & 0xFFC003FFU) == 0xD10003FFU; // sub sp, sp, #imm
}

bool is_stp_fp_lr_start(uint32_t instruction) {
    if ((instruction & 0xFFC07FFFU) == 0xA9007BFD) return true; // stp x29, x30, [sp, #imm]
    if ((instruction & 0xFFC07FFFU) == 0xA9807BFD) return true; // stp x29, x30, [sp, #-imm]!
    return false;
}

uintptr_t find_function_start_before_ref(const std::vector<MemoryRange> &ranges, uintptr_t ref) {
    if (ref < 4) return 0;
    uintptr_t aligned = ref & ~static_cast<uintptr_t>(3U);
    uintptr_t min = aligned > 0x240 ? aligned - 0x240 : 0;
    uintptr_t stp_fallback = 0;
    for (uintptr_t pc = aligned; pc >= min; pc -= 4) {
        if (!range_contains(ranges, pc, 4)) break;
        uint32_t instruction = read_u32(pc);
        if (is_sub_sp_start(instruction)) return pc;
        if (stp_fallback == 0 && is_stp_fp_lr_start(instruction)) {
            uintptr_t previous = pc >= 4 ? pc - 4 : 0;
            if (previous != 0 && range_contains(ranges, previous, 4) &&
                is_sub_sp_start(read_u32(previous))) {
                return previous;
            }
            // A pre-indexed FP/LR pair allocates this function's frame. Do not
            // continue into an earlier function's SUB SP prologue.
            if ((instruction & 0xFFC07FFFU) == 0xA9807BFDU) return pc;
            stp_fallback = pc;
        }
        if (pc == min) break;
    }
    return stp_fallback;
}

bool is_adrp_reg(uint32_t instruction, uint32_t reg) {
    return (instruction & 0x9F00001FU) == (0x90000000U | (reg & 0x1FU));
}

bool is_ret_instruction(uint32_t instruction) {
    return instruction == 0xD65F03C0U;
}

bool is_unconditional_branch(uint32_t instruction) {
    return (instruction & 0xFC000000U) == 0x14000000U;
}

bool is_conditional_branch(uint32_t instruction) {
    return (instruction & 0xFF000010U) == 0x54000000U;
}

bool is_cbz_x_reg(uint32_t instruction, uint32_t reg) {
    return (instruction & 0xFF00001FU) == (0xB4000000U | (reg & 0x1FU));
}

bool decode_unsigned_offset(uint32_t instruction,
                            uint32_t opcode,
                            unsigned scale_shift,
                            int rn,
                            int rt,
                            uintptr_t *offset) {
    if ((instruction & 0xFFC00000U) != opcode) return false;
    if (rn >= 0 && static_cast<uint32_t>(rn) != ((instruction >> 5U) & 0x1FU)) return false;
    if (rt >= 0 && static_cast<uint32_t>(rt) != (instruction & 0x1FU)) return false;
    if (offset != nullptr) {
        *offset = static_cast<uintptr_t>(((instruction >> 10U) & 0xFFFU) << scale_shift);
    }
    return true;
}

bool decode_ldr_x_unsigned(uint32_t instruction, int rn, int rt, uintptr_t *offset) {
    return decode_unsigned_offset(instruction, 0xF9400000U, 3, rn, rt, offset);
}

bool decode_ldr_w_unsigned(uint32_t instruction, int rn, int rt, uintptr_t *offset) {
    return decode_unsigned_offset(instruction, 0xB9400000U, 2, rn, rt, offset);
}

bool decode_ldr_b_unsigned(uint32_t instruction, int rn, int rt, uintptr_t *offset) {
    return decode_unsigned_offset(instruction, 0x39400000U, 0, rn, rt, offset);
}

bool decode_str_x_unsigned(uint32_t instruction, int rn, int rt, uintptr_t *offset) {
    return decode_unsigned_offset(instruction, 0xF9000000U, 3, rn, rt, offset);
}

bool decode_str_w_unsigned(uint32_t instruction, int rn, int rt, uintptr_t *offset) {
    return decode_unsigned_offset(instruction, 0xB9000000U, 2, rn, rt, offset);
}

bool decode_str_b_unsigned(uint32_t instruction, int rn, int rt, uintptr_t *offset) {
    return decode_unsigned_offset(instruction, 0x39000000U, 0, rn, rt, offset);
}

bool decode_ldr_s_unsigned(uint32_t instruction, int rn, int rt, uintptr_t *offset) {
    return decode_unsigned_offset(instruction, 0xBD400000U, 2, rn, rt, offset);
}

bool decode_add_immediate_offset(uint32_t instruction, int rd, int rn, uintptr_t *offset) {
    if ((instruction & 0x7F000000U) != 0x11000000U) return false;
    if (rd >= 0 && static_cast<uint32_t>(rd) != (instruction & 0x1FU)) return false;
    if (rn >= 0 && static_cast<uint32_t>(rn) != ((instruction >> 5U) & 0x1FU)) return false;
    uintptr_t imm = static_cast<uintptr_t>((instruction >> 10U) & 0xFFFU);
    if ((instruction & (1U << 22U)) != 0) imm <<= 12U;
    if (offset != nullptr) *offset = imm;
    return true;
}

bool is_cmp_w_immediate(uint32_t instruction, uint32_t rn, uint32_t expected_imm) {
    if ((instruction & 0xFFC0001FU) != 0x7100001FU) return false;
    if (((instruction >> 5U) & 0x1FU) != (rn & 0x1FU)) return false;
    uint32_t imm = (instruction >> 10U) & 0xFFFU;
    if ((instruction & (1U << 22U)) != 0) imm <<= 12U;
    return imm == expected_imm;
}

bool dialogue_standard_prologue_matches(const std::vector<MemoryRange> &ranges, uintptr_t address) {
    static constexpr uint32_t prologue[] = {
            0xD10203FFU, // sub sp, sp, #0x80
            0xA9047BFDU, // stp x29, x30, [sp, #0x40]
            0xF9002BF7U, // str x23, [sp, #0x50]
            0xA90657F6U, // stp x22, x21, [sp, #0x60]
            0xA9074FF4U, // stp x20, x19, [sp, #0x70]
            0x910103FDU, // add x29, sp, #0x40
            0xD53BD056U, // mrs x22, TPIDR_EL0
            0xAA0003F3U, // mov x19, x0
            0xF94016C8U, // ldr x8, [x22, #0x28]
            0xF81F83A8U, // stur x8, [x29, #-0x8]
    };
    if (!range_contains(ranges, address, sizeof(prologue))) return false;
    for (size_t i = 0; i < sizeof(prologue) / sizeof(prologue[0]); ++i) {
        if (read_u32(address + i * 4U) != prologue[i]) return false;
    }
    return true;
}

bool dialogue_function_has_strb_wzr_x0(const std::vector<MemoryRange> &ranges,
                                       uintptr_t address,
                                       uintptr_t *offset) {
    if (!range_contains(ranges, address, 30U * 4U)) return false;
    for (size_t i = 10; i < 30; ++i) {
        uintptr_t decoded = 0;
        if (decode_str_b_unsigned(read_u32(address + i * 4U), 0, 31, &decoded)) {
            if (offset != nullptr) *offset = decoded;
            return true;
        }
    }
    return false;
}

bool dialogue_function_has_ldr_s0_x8(const std::vector<MemoryRange> &ranges, uintptr_t address) {
    if (!range_contains(ranges, address, 30U * 4U)) return false;
    for (size_t i = 10; i < 30; ++i) {
        uintptr_t ignored = 0;
        if (decode_ldr_s_unsigned(read_u32(address + i * 4U), 8, 0, &ignored)) return true;
    }
    return false;
}

bool dialogue_function_has_virtual_x19_call(const std::vector<MemoryRange> &ranges,
                                            uintptr_t address,
                                            int required_instruction_index) {
    if (address == 0) return false;
    int begin = required_instruction_index >= 0 ? required_instruction_index : 0;
    int end = required_instruction_index >= 0 ? required_instruction_index + 1 : 80;
    for (int i = begin; i < end; ++i) {
        uintptr_t pc = address + static_cast<uintptr_t>(i) * 4U;
        if (!range_contains(ranges, pc, 5U * 4U)) break;
        uintptr_t vtable_offset = 0;
        uintptr_t zero_offset = 0;
        if (!decode_ldr_x_unsigned(read_u32(pc), 19, 8, &zero_offset) ||
            zero_offset != 0) {
            continue;
        }
        if (read_u32(pc + 4) != 0xAA0003E1U || // mov x1, x0
            read_u32(pc + 8) != 0xAA1303E0U || // mov x0, x19
            !decode_ldr_x_unsigned(read_u32(pc + 12), 8, 8, &vtable_offset) ||
            read_u32(pc + 16) != 0xD63F0100U) { // blr x8
            continue;
        }
        return true;
    }
    return false;
}

bool resolve_dialogue_auto_feed_getters(const std::vector<MemoryRange> &ranges,
                                        DialogueNativeResolution *out) {
    if (out == nullptr) return false;
    std::vector<uintptr_t> hits;
    for (const MemoryRange &range: ranges) {
        uintptr_t pc = (range.start + 3U) & ~static_cast<uintptr_t>(3U);
        while (pc + 60 <= range.end) {
            uintptr_t ignored = 0;
            if (is_adrp_reg(read_u32(pc), 8) &&
                decode_ldr_b_unsigned(read_u32(pc + 4), 8, 0, &ignored) &&
                is_ret_instruction(read_u32(pc + 8)) &&
                is_adrp_reg(read_u32(pc + 12), 8) &&
                decode_ldr_s_unsigned(read_u32(pc + 16), 8, 0, &ignored) &&
                is_ret_instruction(read_u32(pc + 20)) &&
                is_adrp_reg(read_u32(pc + 24), 8) &&
                decode_ldr_s_unsigned(read_u32(pc + 28), 8, 0, &ignored) &&
                is_ret_instruction(read_u32(pc + 32)) &&
                is_adrp_reg(read_u32(pc + 36), 8) &&
                read_u32(pc + 40) == 0x2F00E401U && // movi d1, #0
                decode_ldr_b_unsigned(read_u32(pc + 44), 8, 8, &ignored) &&
                is_cmp_w_immediate(read_u32(pc + 48), 8, 0) &&
                read_u32(pc + 52) == 0x1E210C00U && // fcsel s0, s0, s1, eq
                is_ret_instruction(read_u32(pc + 56))) {
                hits.push_back(pc);
            }
            pc += 4;
        }
    }

    if (hits.size() != 1) {
        ALOGW("AE_TRACE dialogue native auto-feed getters skipped hits=%zu", hits.size());
        return false;
    }
    out->get_auto_feed_mode = hits[0];
    out->get_auto_feed_wait_time_letter = hits[0] + 12;
    out->get_auto_feed_wait_time_minimum = hits[0] + 24;
    return true;
}

uintptr_t resolve_dialogue_action_timer_value(const std::vector<MemoryRange> &ranges) {
    std::vector<uintptr_t> hits;
    for (const MemoryRange &range: ranges) {
        uintptr_t pc = (range.start + 3U) & ~static_cast<uintptr_t>(3U);
        while (pc + 48 <= range.end) {
            uintptr_t ignored = 0;
            uintptr_t branch_low = 0;
            uintptr_t branch_high = 0;
            if (decode_ldr_w_unsigned(read_u32(pc), 0, 8, &ignored) &&
                is_cmp_w_immediate(read_u32(pc + 4), 8, 3) &&
                is_conditional_branch(read_u32(pc + 8)) &&
                read_u32(pc + 12) == 0x2F00E400U && // movi d0, #0
                is_cmp_w_immediate(read_u32(pc + 16), 8, 2) &&
                is_conditional_branch(read_u32(pc + 20)) &&
                decode_add_immediate_offset(read_u32(pc + 24), 8, 0, &branch_low) &&
                is_unconditional_branch(read_u32(pc + 28)) &&
                decode_add_immediate_offset(read_u32(pc + 32), 8, 0, &branch_high) &&
                decode_ldr_x_unsigned(read_u32(pc + 36), 8, 8, &ignored) &&
                ignored == 0 &&
                decode_ldr_s_unsigned(read_u32(pc + 40), 8, 0, &ignored) &&
                is_ret_instruction(read_u32(pc + 44)) &&
                branch_low != branch_high) {
                hits.push_back(pc);
            }
            pc += 4;
        }
    }

    if (hits.size() != 1) {
        ALOGW("AE_TRACE dialogue action timer skipped hits=%zu", hits.size());
        return 0;
    }
    return hits[0];
}

bool resolve_dialogue_render_checker_offsets(const std::vector<MemoryRange> &ranges,
                                             uintptr_t checker,
                                             uintptr_t *main_window_offset,
                                             uintptr_t *rendering_flag_offset) {
    if (main_window_offset == nullptr || rendering_flag_offset == nullptr) return false;
    if (!range_contains(ranges, checker, 16)) return false;
    uintptr_t main_offset = 0;
    uintptr_t flag_offset = 0;
    if (!decode_ldr_x_unsigned(read_u32(checker), 0, 8, &main_offset)) return false;
    if (!is_cbz_x_reg(read_u32(checker + 4), 8)) return false;
    if (!decode_ldr_b_unsigned(read_u32(checker + 8), 8, 8, &flag_offset)) return false;
    if (!is_unconditional_branch(read_u32(checker + 12))) return false;
    *main_window_offset = main_offset;
    *rendering_flag_offset = flag_offset;
    return main_offset != 0 && flag_offset != 0;
}

bool resolve_dialogue_talk_ui_process_wait(const std::vector<MemoryRange> &ranges,
                                           uintptr_t action_timer_addr,
                                           uintptr_t auto_feed_mode_addr,
                                           DialogueNativeResolution *out) {
    if (out == nullptr || action_timer_addr == 0 || auto_feed_mode_addr == 0) return false;
    std::vector<DialogueNativeResolution> candidates;
    for (const MemoryRange &range: ranges) {
        uintptr_t pc = (range.start + 3U) & ~static_cast<uintptr_t>(3U);
        while (pc + 4 <= range.end) {
            uintptr_t target = 0;
            if (!decode_branch_target(pc, true, &target) || target != action_timer_addr) {
                pc += 4;
                continue;
            }

            uintptr_t start = find_function_start_before_ref(ranges, pc);
            if (start == 0) {
                pc += 4;
                continue;
            }

            bool has_auto_feed_mode = false;
            uintptr_t talk_layer_offset = 0;
            uintptr_t rendering_checker = 0;
            uintptr_t main_window_offset = 0;
            uintptr_t rendering_flag_offset = 0;
            for (uintptr_t scan = start; scan < start + 0x200; scan += 4) {
                if (!range_contains(ranges, scan, 4)) break;
                uintptr_t scan_target = 0;
                if (!decode_branch_target(scan, true, &scan_target)) continue;
                if (scan_target == auto_feed_mode_addr) {
                    has_auto_feed_mode = true;
                }
                if (scan < start + 4 || !range_contains(ranges, scan - 4, 4)) continue;
                uintptr_t candidate_talk_layer_offset = 0;
                if (!decode_ldr_x_unsigned(read_u32(scan - 4), 19, 0,
                                           &candidate_talk_layer_offset)) {
                    continue;
                }
                uintptr_t candidate_main_window_offset = 0;
                uintptr_t candidate_rendering_flag_offset = 0;
                if (!resolve_dialogue_render_checker_offsets(ranges,
                                                             scan_target,
                                                             &candidate_main_window_offset,
                                                             &candidate_rendering_flag_offset)) {
                    continue;
                }
                talk_layer_offset = candidate_talk_layer_offset;
                rendering_checker = scan_target;
                main_window_offset = candidate_main_window_offset;
                rendering_flag_offset = candidate_rendering_flag_offset;
            }

            if (has_auto_feed_mode && rendering_checker != 0 && talk_layer_offset != 0) {
                bool duplicate = false;
                for (const DialogueNativeResolution &candidate: candidates) {
                    if (candidate.talk_ui_process_wait == start) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) {
                    DialogueNativeResolution candidate{};
                    candidate.talk_ui_process_wait = start;
                    candidate.rendering_checker = rendering_checker;
                    candidate.talk_ui_state_talk_layer_offset = talk_layer_offset;
                    candidate.talk_layer_main_window_offset = main_window_offset;
                    candidate.talk_main_window_rendering_flag_offset = rendering_flag_offset;
                    candidates.push_back(candidate);
                }
            }
            pc += 4;
        }
    }

    if (candidates.size() != 1) {
        ALOGW("AE_TRACE dialogue TalkUI.processWait skipped candidates=%zu", candidates.size());
        return false;
    }

    out->talk_ui_process_wait = candidates[0].talk_ui_process_wait;
    out->rendering_checker = candidates[0].rendering_checker;
    out->talk_ui_state_talk_layer_offset = candidates[0].talk_ui_state_talk_layer_offset;
    out->talk_layer_main_window_offset = candidates[0].talk_layer_main_window_offset;
    out->talk_main_window_rendering_flag_offset =
            candidates[0].talk_main_window_rendering_flag_offset;
    return true;
}

std::vector<DialoguePageJumpCandidate> find_dialogue_page_jump_candidates(
        const std::vector<MemoryRange> &ranges) {
    std::vector<DialoguePageJumpCandidate> candidates;
    for (const MemoryRange &range: ranges) {
        uintptr_t pc = (range.start + 3U) & ~static_cast<uintptr_t>(3U);
        while (pc + 0x180 <= range.end) {
            if (!dialogue_standard_prologue_matches(ranges, pc)) {
                pc += 4;
                continue;
            }
            uintptr_t unlocked_offset = 0;
            if (dialogue_function_has_ldr_s0_x8(ranges, pc) &&
                dialogue_function_has_strb_wzr_x0(ranges, pc, &unlocked_offset) &&
                dialogue_function_has_virtual_x19_call(ranges, pc, -1)) {
                candidates.push_back({pc, unlocked_offset});
            }
            pc += 4;
        }
    }
    return candidates;
}

bool resolve_dialogue_page_jump_delay(const std::vector<MemoryRange> &ranges,
                                      uintptr_t talk_ui_process_wait_addr,
                                      DialogueNativeResolution *out) {
    if (out == nullptr) return false;
    (void) talk_ui_process_wait_addr;
    const auto readable = app_readable_ranges();
    // The std::function callable's RTTI names its enclosing C++ method. Follow
    // name -> type_info -> vtable -> constructor reference; never choose by RVA
    // proximity to an unrelated function. These are Itanium C++ ABI relations.
    constexpr char owner[] = "NSt6__ndk110__function6__funcIZN6toybox3gui21TalkMessageWindowNode13pageJumpDelayEvE3$_0";
    std::vector<uintptr_t> vtables;
    for (uintptr_t text: find_pattern(readable, reinterpret_cast<const uint8_t *>(owner), sizeof(owner) - 1)) {
        uint8_t needle[sizeof(uintptr_t)];
        std::memcpy(needle, &text, sizeof(text));
        for (uintptr_t name_slot: find_pattern(readable, needle, sizeof(needle))) {
            if (name_slot < sizeof(uintptr_t)) continue;
            uintptr_t type_info = name_slot - sizeof(uintptr_t);
            std::memcpy(needle, &type_info, sizeof(type_info));
            for (uintptr_t type_slot: find_pattern(readable, needle, sizeof(needle))) {
                uintptr_t first_function = 0;
                const uintptr_t vtable = type_slot + sizeof(uintptr_t);
                if (read_range_ptr(readable, vtable, &first_function) &&
                    range_contains(ranges, first_function, sizeof(uint32_t))) push_unique(vtables, vtable);
            }
        }
    }
    std::vector<DialoguePageJumpCandidate> matches;
    for (const auto &candidate: find_dialogue_page_jump_candidates(ranges)) {
        bool references_owner = false;
        for (uintptr_t vtable: vtables) {
            references_owner |= function_references_address(ranges, candidate.address, 0x180, vtable);
        }
        if (references_owner) matches.push_back(candidate);
    }
    if (matches.size() != 1) {
        ALOGW("AE_TRACE pageJumpDelay semantic candidates=%zu callableVtables=%zu", matches.size(), vtables.size());
        return false;
    }
    out->page_jump_delay = matches[0].address;
    out->page_jump_unlocked_offset = matches[0].unlocked_offset;
    return true;
}

bool dialogue_run_auto_touch_candidate(const std::vector<MemoryRange> &ranges, uintptr_t address) {
    uintptr_t ignored = 0;
    return dialogue_standard_prologue_matches(ranges, address) &&
           !dialogue_function_has_strb_wzr_x0(ranges, address, &ignored) &&
           dialogue_function_has_virtual_x19_call(ranges, address, 35);
}

bool resolve_dialogue_run_auto_touch_actions(const std::vector<MemoryRange> &ranges,
                                             uintptr_t rendering_checker_addr,
                                             DialogueNativeResolution *out) {
    if (out == nullptr || rendering_checker_addr == 0) return false;
    uintptr_t talk_addr = find_function_start_before_ref(ranges, rendering_checker_addr);
    if (!dialogue_run_auto_touch_candidate(ranges, talk_addr)) {
        ALOGW("AE_TRACE dialogue TalkLayer.runAutoTouch skipped checker=0x%" PRIxPTR
              " candidate=0x%" PRIxPTR,
              rendering_checker_addr,
              talk_addr);
        talk_addr = 0;
    }

    std::vector<uintptr_t> run_auto_candidates;
    for (const MemoryRange &range: ranges) {
        uintptr_t pc = (range.start + 3U) & ~static_cast<uintptr_t>(3U);
        while (pc + 0x180 <= range.end) {
            if (dialogue_run_auto_touch_candidate(ranges, pc)) {
                push_unique(run_auto_candidates, pc);
            }
            pc += 4;
        }
    }

    std::vector<uintptr_t> still_candidates;
    for (uintptr_t candidate: run_auto_candidates) {
        if (candidate != talk_addr) still_candidates.push_back(candidate);
    }
    if (talk_addr != 0) out->talk_layer_run_auto_touch = talk_addr;
    if (still_candidates.size() == 1) {
        out->still_talk_layer_run_auto_touch = still_candidates[0];
    } else {
        ALOGW("AE_TRACE dialogue StillTalkLayer.runAutoTouch skipped candidates=%zu totalRunAuto=%zu",
              still_candidates.size(),
              run_auto_candidates.size());
    }
    ALOGI("AE_TRACE dialogue runAutoTouch resolved talk=0x%" PRIxPTR
          " still=0x%" PRIxPTR " totalCandidates=%zu",
          out->talk_layer_run_auto_touch,
          out->still_talk_layer_run_auto_touch,
          run_auto_candidates.size());
    return out->talk_layer_run_auto_touch != 0 || out->still_talk_layer_run_auto_touch != 0;
}

uintptr_t resolve_dialogue_touch_handler(const std::vector<MemoryRange> &ranges) {
    std::vector<uintptr_t> hits;
    for (const MemoryRange &range: ranges) {
        uintptr_t pc = (range.start + 3U) & ~static_cast<uintptr_t>(3U);
        while (pc + 0x80 <= range.end) {
            uintptr_t state_offset = 0;
            uintptr_t store_w_offset = 0;
            uintptr_t store_x_offset = 0;
            uintptr_t ignored = 0;
            if (read_u32(pc) == 0xA9BF7BFDU &&
                read_u32(pc + 4) == 0x910003FDU &&
                decode_ldr_w_unsigned(read_u32(pc + 8), 0, 8, &state_offset) &&
                decode_str_w_unsigned(read_u32(pc + 12), 0, 1, &store_w_offset) &&
                store_w_offset == state_offset &&
                decode_str_x_unsigned(read_u32(pc + 16), 0, 2, &store_x_offset) &&
                store_x_offset == state_offset + sizeof(uintptr_t) &&
                is_cmp_w_immediate(read_u32(pc + 20), 8, 0) &&
                is_conditional_branch(read_u32(pc + 24)) &&
                is_ret_instruction(read_u32(pc + 32)) &&
                decode_ldr_x_unsigned(read_u32(pc + 36), 0, 8, &ignored) &&
                ignored == 0 &&
                decode_ldr_x_unsigned(read_u32(pc + 40), 8, 8, &ignored) &&
                read_u32(pc + 44) == 0xD63F0100U) {
                hits.push_back(pc);
            }
            pc += 4;
        }
    }

    if (hits.size() != 1) {
        ALOGW("AE_TRACE dialogue touch handler skipped candidates=%zu", hits.size());
        return 0;
    }
    return hits[0];
}

void resolve_dialogue_window_flags(const std::vector<MemoryRange> &ranges, DialogueNativeResolution *resolved) {
    using namespace item_catalog;
    if (resolved == nullptr || resolved->talk_main_window_rendering_flag_offset == 0 ||
        resolved->talk_layer_main_window_offset == 0) return;
    std::vector<uintptr_t> open_hits;
    for (uintptr_t hit: find_item_contract(ranges, window_open)) {
        uint32_t words[std::size(window_open)]{};
        if (read_item_contract(hit, window_open, words) &&
            unsigned_offset(item_role_word(words, window_open, Role::window_rendering), 0) ==
                resolved->talk_main_window_rendering_flag_offset) open_hits.push_back(hit);
    }
    if (open_hits.size() != 1) return;
    uint32_t open_words[std::size(window_open)]{};
    if (!read_item_contract(open_hits[0], window_open, open_words)) return;
    size_t ready = unsigned_offset(item_role_word(open_words, window_open, Role::window_ready), 0);
    size_t aux = unsigned_offset(item_role_word(open_words, window_open, Role::window_aux), 0);
    size_t getters = 0;
    for (uintptr_t hit: find_item_contract(ranges, window_ready_getter)) {
        uint32_t words[std::size(window_ready_getter)]{};
        if (read_item_contract(hit, window_ready_getter, words) &&
            unsigned_offset(item_role_word(words, window_ready_getter, Role::window_member), 3) ==
                resolved->talk_layer_main_window_offset &&
            unsigned_offset(item_role_word(words, window_ready_getter, Role::window_ready), 0) == ready) ++getters;
    }
    std::vector<uintptr_t> update_hits;
    for (uintptr_t hit: find_item_contract(ranges, window_update)) {
        uint32_t words[std::size(window_update)]{};
        if (read_item_contract(hit, window_update, words) &&
            unsigned_offset(item_role_word(words, window_update, Role::window_ready), 1) == ready &&
            unsigned_offset(item_role_word(words, window_update, Role::window_aux), 0) == aux) update_hits.push_back(hit);
    }
    if (getters != 1 || update_hits.size() != 1) return;
    uint32_t words[std::size(window_update)]{};
    if (!read_item_contract(update_hits[0], window_update, words)) return;
    size_t started = unsigned_offset(item_role_word(words, window_update, Role::window_started), 0);
    if (ready == started || ready == aux || started == aux) return;
    resolved->talk_main_window_ready_offset = ready;
    resolved->talk_main_window_started_offset = started;
    resolved->talk_main_window_aux_offset = aux;
    ALOGI("AE_TRACE dialogue window fields ready=%zu started=%zu aux=%zu (open/update/getter agree)", ready, started, aux);
}

DialogueNativeResolution resolve_dialogue_native_hooks(const std::vector<MemoryRange> &ranges) {
    DialogueNativeResolution resolved{};
    resolve_dialogue_auto_feed_getters(ranges, &resolved);
    resolved.action_timer_value = resolve_dialogue_action_timer_value(ranges);
    if (resolved.action_timer_value != 0 && resolved.get_auto_feed_mode != 0) {
        resolve_dialogue_talk_ui_process_wait(ranges,
                                              resolved.action_timer_value,
                                              resolved.get_auto_feed_mode,
                                              &resolved);
    }
    resolve_dialogue_page_jump_delay(ranges, resolved.talk_ui_process_wait, &resolved);
    resolve_dialogue_run_auto_touch_actions(ranges, resolved.rendering_checker, &resolved);
    resolved.talk_layer_touch_handler = resolve_dialogue_touch_handler(ranges);
    resolve_dialogue_window_flags(ranges, &resolved);

    ALOGI("AE_TRACE dialogue native resolver page=0x%" PRIxPTR
          " talkRun=0x%" PRIxPTR " stillRun=0x%" PRIxPTR
          " autoMode=0x%" PRIxPTR " waitLetter=0x%" PRIxPTR
          " waitMinimum=0x%" PRIxPTR " actionTimer=0x%" PRIxPTR
          " processWait=0x%" PRIxPTR " touch=0x%" PRIxPTR
          " offsets page=0x%" PRIxPTR " uiTalk=0x%" PRIxPTR
          " mainWindow=0x%" PRIxPTR " renderingFlag=0x%" PRIxPTR,
          resolved.page_jump_delay,
          resolved.talk_layer_run_auto_touch,
          resolved.still_talk_layer_run_auto_touch,
          resolved.get_auto_feed_mode,
          resolved.get_auto_feed_wait_time_letter,
          resolved.get_auto_feed_wait_time_minimum,
          resolved.action_timer_value,
          resolved.talk_ui_process_wait,
          resolved.talk_layer_touch_handler,
          resolved.page_jump_unlocked_offset,
          resolved.talk_ui_state_talk_layer_offset,
          resolved.talk_layer_main_window_offset,
          resolved.talk_main_window_rendering_flag_offset);
    return resolved;
}

uintptr_t resolve_achievement_fire_event_dispatch(const std::vector<MemoryRange> &readable_ranges,
                                                  const std::vector<MemoryRange> &exec_ranges) {
    std::vector<uintptr_t> type_hits = find_pattern(
            readable_ranges,
            reinterpret_cast<const uint8_t *>(kAchievementFireEventTypeName),
            std::strlen(kAchievementFireEventTypeName) + 1);
    std::vector<uintptr_t> prefix_hits = find_pattern(
            readable_ranges,
            reinterpret_cast<const uint8_t *>(kAchievementGameEventPrefix),
            std::strlen(kAchievementGameEventPrefix) + 1);

    std::vector<uintptr_t> candidates;
    size_t type_refs = 0;
    for (uintptr_t type_hit: type_hits) {
        std::vector<uintptr_t> refs = find_pc_relative_refs(exec_ranges, type_hit);
        type_refs += refs.size();
        for (uintptr_t ref: refs) {
            uintptr_t function_start = find_function_start_before_ref(exec_ranges, ref);
            if (function_start == 0) continue;
            bool has_prefix = false;
            for (uintptr_t prefix_hit: prefix_hits) {
                if (function_references_address(exec_ranges, function_start, 0x180, prefix_hit)) {
                    has_prefix = true;
                    break;
                }
            }
            if (has_prefix) push_unique(candidates, function_start);
        }
    }

    if (candidates.size() != 1) {
        ALOGW("AE_TRACE achievement dispatcher skipped typeStringHits=%zu prefixStringHits=%zu typeRefs=%zu candidates=%zu",
              type_hits.size(),
              prefix_hits.size(),
              type_refs,
              candidates.size());
        return 0;
    }

    ALOGI("AE_TRACE achievement dispatcher resolved addr=0x%" PRIxPTR
          " typeStringHits=%zu prefixStringHits=%zu typeRefs=%zu",
          candidates[0],
          type_hits.size(),
          prefix_hits.size(),
          type_refs);
    return candidates[0];
}

uintptr_t resolve_domain_achievement_repository_get(const std::vector<MemoryRange> &readable_ranges,
                                                    const std::vector<MemoryRange> &exec_ranges) {
    std::vector<uintptr_t> not_found_hits = find_pattern(
            readable_ranges,
            reinterpret_cast<const uint8_t *>(kAchievementRepositoryNotFoundText),
            std::strlen(kAchievementRepositoryNotFoundText));
    std::vector<uintptr_t> end_assert_hits = find_pattern(
            readable_ranges,
            reinterpret_cast<const uint8_t *>(kAchievementRepositoryEndAssertText),
            std::strlen(kAchievementRepositoryEndAssertText));

    std::vector<uintptr_t> candidates;
    size_t refs = 0;
    for (uintptr_t hit: not_found_hits) {
        std::vector<uintptr_t> hit_refs = find_pc_relative_refs(exec_ranges, hit);
        refs += hit_refs.size();
        for (uintptr_t ref: hit_refs) {
            uintptr_t function_start = find_function_start_before_ref(exec_ranges, ref);
            if (function_start == 0) continue;
            bool has_end_assert = end_assert_hits.empty();
            for (uintptr_t assert_hit: end_assert_hits) {
                if (function_references_address(exec_ranges, function_start, 0x240, assert_hit)) {
                    has_end_assert = true;
                    break;
                }
            }
            if (has_end_assert) push_unique(candidates, function_start);
        }
    }

    if (candidates.size() != 1) {
        ALOGW("AE_TRACE achievement repository get skipped notFoundHits=%zu endAssertHits=%zu refs=%zu candidates=%zu",
              not_found_hits.size(),
              end_assert_hits.size(),
              refs,
              candidates.size());
        return 0;
    }

    ALOGI("AE_TRACE achievement repository get resolved addr=0x%" PRIxPTR
          " notFoundHits=%zu endAssertHits=%zu refs=%zu",
          candidates[0],
          not_found_hits.size(),
          end_assert_hits.size(),
          refs);
    return candidates[0];
}

uintptr_t previous_bl_target_before(const std::vector<MemoryRange> &exec_ranges,
                                    uintptr_t call_pc,
                                    size_t max_back_bytes) {
    if (call_pc < 4) return 0;
    size_t steps = max_back_bytes / 4;
    for (size_t i = 1; i <= steps; ++i) {
        uintptr_t pc = call_pc - i * 4U;
        if (!range_contains(exec_ranges, pc, 4)) break;
        uintptr_t target = 0;
        if (decode_branch_target(pc, true, &target) &&
            range_contains(exec_ranges, target, 4)) {
            return target;
        }
        if (pc < 4) break;
    }
    return 0;
}

uintptr_t resolve_achievement_repository_accessor_from_wrapper(const std::vector<MemoryRange> &exec_ranges,
                                                               uintptr_t wrapper_addr,
                                                               uintptr_t repository_get_addr,
                                                               const char *wrapper_name) {
    if (wrapper_addr == 0 || repository_get_addr == 0) return 0;
    for (uintptr_t pc = wrapper_addr; pc < wrapper_addr + 0x180; pc += 4) {
        if (!range_contains(exec_ranges, pc, 4)) break;
        uintptr_t target = 0;
        if (!decode_branch_target(pc, true, &target) || target != repository_get_addr) continue;
        uintptr_t accessor = previous_bl_target_before(exec_ranges, pc, 0x40);
        if (accessor != 0) {
            ALOGI("AE_TRACE achievement repository accessor candidate wrapper=%s wrapperAddr=0x%" PRIxPTR
                  " getCall=0x%" PRIxPTR " accessor=0x%" PRIxPTR,
                  wrapper_name == nullptr ? "<unknown>" : wrapper_name,
                  wrapper_addr,
                  pc,
                  accessor);
        }
        return accessor;
    }
    ALOGW("AE_TRACE achievement repository accessor missing wrapper=%s wrapperAddr=0x%" PRIxPTR
          " get=0x%" PRIxPTR,
          wrapper_name == nullptr ? "<unknown>" : wrapper_name,
          wrapper_addr,
          repository_get_addr);
    return 0;
}

uintptr_t resolve_achievement_repository_accessor(const std::vector<MemoryRange> &exec_ranges,
                                                  uintptr_t fire_achievement_trigger_addr,
                                                  uintptr_t is_achievement_completed_addr,
                                                  uintptr_t repository_get_addr) {
    uintptr_t fire_accessor = resolve_achievement_repository_accessor_from_wrapper(
            exec_ranges,
            fire_achievement_trigger_addr,
            repository_get_addr,
            "fireAchievementTrigger");
    uintptr_t completed_accessor = resolve_achievement_repository_accessor_from_wrapper(
            exec_ranges,
            is_achievement_completed_addr,
            repository_get_addr,
            "isAchievementCompleted");

    uintptr_t accessor = fire_accessor != 0 ? fire_accessor : completed_accessor;
    if (fire_accessor != 0 && completed_accessor != 0 && fire_accessor != completed_accessor) {
        ALOGW("AE_TRACE achievement repository accessor mismatch fire=0x%" PRIxPTR
              " completed=0x%" PRIxPTR,
              fire_accessor,
              completed_accessor);
        return 0;
    }
    if (accessor == 0) {
        ALOGW("AE_TRACE achievement repository accessor skipped fire=0x%" PRIxPTR
              " completed=0x%" PRIxPTR " get=0x%" PRIxPTR,
              fire_achievement_trigger_addr,
              is_achievement_completed_addr,
              repository_get_addr);
        return 0;
    }

    ALOGI("AE_TRACE achievement repository accessor resolved addr=0x%" PRIxPTR
          " get=0x%" PRIxPTR,
          accessor,
          repository_get_addr);
    return accessor;
}

bool resolve_lua_registered_function(const std::vector<MemoryRange> &readable_ranges,
                                     const std::vector<MemoryRange> &exec_ranges,
                                     const char *name,
                                     bool optional,
                                     uintptr_t *out) {
    if (out == nullptr) return false;
    *out = 0;
    if (name == nullptr || name[0] == '\0') return false;

    std::vector<uintptr_t> string_hits = find_pattern(
            readable_ranges,
            reinterpret_cast<const uint8_t *>(name),
            std::strlen(name) + 1);
    if (string_hits.empty()) {
        if (optional) {
            ALOGI("AE_TRACE lua registration %s absent in this libapp version", name);
            return true;
        }
        ALOGW("AE_TRACE lua registration %s skipped stringHits=0", name);
        return false;
    }

    std::vector<uintptr_t> hits;
    size_t direct_refs = 0;
    size_t indirect_refs = 0;
    for (uintptr_t string_address: string_hits) {
        uint8_t pointer_bytes[sizeof(uintptr_t)] = {};
        std::memcpy(pointer_bytes, &string_address, sizeof(pointer_bytes));
        for (uintptr_t ref: find_pattern(readable_ranges, pointer_bytes, sizeof(pointer_bytes))) {
            // These are native pointer rows, not arbitrary occurrences of the
            // same bytes (low WASM image addresses can occur inside packed data).
            if(ref%alignof(uintptr_t)) continue;
            uintptr_t adjacent = 0;
            if (!read_range_ptr(readable_ranges, ref + sizeof(uintptr_t), &adjacent)) continue;

            // Current builds expose both direct Lua registration rows:
            //   { "setGlobalFlag", function_ptr }
            // and generated loader rows:
            //   { "setGlobalFlag", &direct_row.function_ptr, ... }.
            // Resolving through the adjacent slot handles both layouts and avoids
            // assuming a fragile fixed +24 byte offset from the string reference.
            if (adjacent%4==0 && range_contains(exec_ranges, adjacent, 4)) {
                ++direct_refs;
                push_unique(hits, adjacent);
                continue;
            }

            uintptr_t indirect = 0;
            uintptr_t indirect_name=0;
            if (adjacent>=sizeof(uintptr_t) && adjacent%alignof(uintptr_t)==0 &&
                read_range_ptr(readable_ranges,adjacent-sizeof(uintptr_t),&indirect_name) && indirect_name==string_address &&
                read_range_ptr(readable_ranges, adjacent, &indirect) && indirect%4==0 &&
                range_contains(exec_ranges, indirect, 4)) {
                ++indirect_refs;
                push_unique(hits, indirect);
            }
        }
    }

    if (hits.size() != 1) {
        ALOGW("AE_TRACE lua registration %s skipped stringHits=%zu directRefs=%zu indirectRefs=%zu functionHits=%zu",
              name,
              string_hits.size(),
              direct_refs,
              indirect_refs,
              hits.size());
        return false;
    }

    *out = hits[0];
    ALOGI("AE_TRACE lua registration %s resolved addr=0x%" PRIxPTR " stringHits=%zu directRefs=%zu indirectRefs=%zu",
          name,
          *out,
          string_hits.size(),
          direct_refs,
          indirect_refs);
    return true;
}

#include "item_injection_resolver.h"
#include "mass_shop_resolver.inc"

#include "director_speed.inc"

uintptr_t resolve_named_integer_setter(const std::vector<MemoryRange> &readable,
                                        const std::vector<MemoryRange> &ranges, const char *key) {
    std::vector<uintptr_t> hits;
    const auto shapes = find_code_pattern(ranges, sig_cat_scratch_stamp_total_pattern,
                                          sizeof(sig_cat_scratch_stamp_total_pattern), sig_cat_scratch_stamp_total_pattern_mask);
    for (uintptr_t text: find_pattern(readable, reinterpret_cast<const uint8_t *>(key), std::strlen(key) + 1)) {
        for (uintptr_t ref: find_pc_relative_refs(ranges, text)) {
            // The deserializer converts this named value then passes w0 to its setter.
            for (uintptr_t pc = ref + 8; pc < ref + 256; pc += 4) {
                if (!range_contains(ranges, pc, 4)) break;
                uint32_t word = read_u32(pc);
                if (is_ret_instruction(word) || is_adrp_reg(word, 1)) break;
                uintptr_t target = 0;
                if (!decode_branch_target(pc, true, &target) || pc < 8 ||
                    read_u32(pc - 8) != 0x2A0003E1U ||
                    (read_u32(pc - 4) & 0xFFE0FFFFU) != 0xAA0003E0U) continue;
                if (std::find(shapes.begin(), shapes.end(), target) != shapes.end()) push_unique(hits, target);
            }
        }
    }
    if (hits.size() != 1) { ALOGW("AE_TRACE named setter %s candidates=%zu", key, hits.size()); return 0; }
    return hits[0];
}

uintptr_t resolve_add_pc_exp(const std::vector<MemoryRange> &readable, const std::vector<MemoryRange> &ranges) {
    uintptr_t wrapper = 0;
    if (!resolve_lua_registered_function(readable, ranges, "addPCExp", false, &wrapper)) return 0;
    const auto shapes = find_code_pattern(ranges, sig_add_pc_exp, sizeof(sig_add_pc_exp));
    std::vector<uintptr_t> hits;
    std::vector<uintptr_t> callers{wrapper};
    for (unsigned depth = 0; depth < 2; ++depth) {
        std::vector<uintptr_t> next;
        for (uintptr_t caller: callers) {
            for (uintptr_t pc = caller; pc < caller + 1024; pc += 4) {
                if (!range_contains(ranges, pc, 4) || is_ret_instruction(read_u32(pc))) break;
                uintptr_t target = 0;
                if (!decode_branch_target(pc, true, &target) || !range_contains(ranges, target, 4)) continue;
                if (std::find(shapes.begin(), shapes.end(), target) != shapes.end()) push_unique(hits, target);
                if (depth == 0) push_unique(next, target);
            }
        }
        callers = std::move(next);
    }
    if (hits.size() != 1) { ALOGW("AE_TRACE addPCExp Lua callees=%zu", hits.size()); return 0; }
    return hits[0];
}


uintptr_t decode_adrp_ldr_global(uintptr_t adrp_pc, uintptr_t ldr_pc) {
    uint32_t adrp_instr, ldr_instr;
    std::memcpy(&adrp_instr, reinterpret_cast<const void *>(adrp_pc), 4);
    std::memcpy(&ldr_instr, reinterpret_cast<const void *>(ldr_pc), 4);
    if ((adrp_instr & 0x9F000000U) != 0x90000000U) return 0;   // ADRP
    if ((ldr_instr & 0xFFC00000U) != 0xF9400000U) return 0;    // LDR (64-bit, unsigned offset)
    if ((adrp_instr & 0x1FU) != ((ldr_instr >> 5U) & 0x1FU)) return 0;
    uint64_t immlo = (adrp_instr >> 29U) & 0x3U;
    uint64_t immhi = (adrp_instr >> 5U) & 0x7FFFFU;
    int64_t imm = static_cast<int64_t>((immhi << 2U) | immlo);
    imm = (imm ^ (1LL << 20)) - (1LL << 20);                   // sign-extend the 21-bit ADRP immediate
    uintptr_t page = static_cast<uintptr_t>(
        static_cast<int64_t>(adrp_pc & ~static_cast<uintptr_t>(0xFFF)) + (imm << 12));
    uintptr_t off = static_cast<uintptr_t>(((ldr_instr >> 10U) & 0xFFFU) << 3U);  // 64-bit scale
    return page + off;
}

template<size_t N>
bool read_item_contract(uintptr_t address, const item_catalog::Word (&pattern)[N],
                        uint32_t (&words)[N]) {
    if (vm_read_partial(address, words, sizeof(words)) != static_cast<ssize_t>(sizeof(words))) return false;
    for (size_t i = 0; i < N; ++i) {
        if ((words[i] & pattern[i].mask) != (pattern[i].value & pattern[i].mask)) return false;
    }
    return true;
}

template<size_t N>
std::vector<uintptr_t> find_item_contract(const std::vector<MemoryRange> &ranges,
                                         const item_catalog::Word (&pattern)[N]) {
    std::vector<uintptr_t> hits;
    for_each_snapshot_segment(ranges, [&](uintptr_t base, const uint8_t *bytes, size_t length) {
        const size_t first = (4U - (base & 3U)) & 3U;
        for (size_t off = first; off + N * sizeof(uint32_t) <= length; off += sizeof(uint32_t)) {
            bool match = true;
            for (size_t i = 0; i < N; ++i) {
                uint32_t word = 0;
                std::memcpy(&word, bytes + off + i * sizeof(word), sizeof(word));
                if ((word & pattern[i].mask) != (pattern[i].value & pattern[i].mask)) {
                    match = false;
                    break;
                }
            }
            if (match) hits.push_back(base + off);
        }
    });
    return hits;
}

template<size_t N>
uintptr_t item_role_address(uintptr_t address, const item_catalog::Word (&pattern)[N],
                            item_catalog::Role role) {
    if(!address) return 0;
    for (size_t i = 0; i < N; ++i) {
        if (pattern[i].role == role) return address + i * sizeof(uint32_t);
    }
    return 0;
}

template<size_t N>
uint32_t item_role_word(const uint32_t (&words)[N], const item_catalog::Word (&pattern)[N],
                        item_catalog::Role role) {
    for (size_t i = 0; i < N; ++i) if (pattern[i].role == role) return words[i];
    return 0;
}

template<size_t N>
std::vector<uintptr_t> find_anchored_item_contract(const std::vector<MemoryRange> &readable,
                                                 const std::vector<MemoryRange> &exec,
                                                 const char *anchor,
                                                 const item_catalog::Word (&pattern)[N]) {
    auto blocks = find_item_contract(exec, pattern);
    std::vector<uintptr_t> hits;
    if (blocks.empty()) return hits;
    auto strings = find_pattern(readable, reinterpret_cast<const uint8_t *>(anchor), std::strlen(anchor));
    for (uintptr_t str: strings) {
        for (uintptr_t ref: find_pc_relative_refs(exec, str)) {
            uintptr_t start = find_function_start_before_ref(exec, ref);
            if (start == 0) continue;
            for (uintptr_t block: blocks) {
                if (block >= start && block + N * sizeof(uint32_t) <= ref) push_unique(hits, block);
            }
        }
    }
    return hits;
}

bool resolve_item_catalog_layout(const std::vector<MemoryRange> &readable,
                                  const std::vector<MemoryRange> &process_readable,
                                  const std::vector<MemoryRange> &exec,
                                  item_catalog::Layout *out) {
    if (out == nullptr) return false;
    if (g_item_layout_ready.load(std::memory_order_acquire)) { *out = g_item_layout; return true; }
    std::lock_guard<std::mutex> lock(g_item_layout_mutex);
    if (g_item_layout_ready.load(std::memory_order_acquire)) { *out = g_item_layout; return true; }

    using namespace item_catalog;
    auto names = find_anchored_item_contract(readable, exec,
        "static int toybox::script::ScriptFunction::getItemName", name_call);
    auto lookups = find_anchored_item_contract(readable, exec,
        "std::shared_ptr<DomainItem> toybox::DomainItemRepository::get(DomainItemID)", numeric_lookup);
    if (names.size() != 1 || lookups.size() != 1) {
        append_item_dump_log("layout_unresolved", 0, static_cast<int>(names.size()),
                            static_cast<int>(lookups.size()), "name/ID-lookup candidates must each be unique");
        return false;
    }

    uint32_t name_words[std::size(name_call)]{};
    uint32_t lookup_words[std::size(numeric_lookup)]{};
    if (!read_item_contract(names[0], name_call, name_words) ||
        !read_item_contract(lookups[0], numeric_lookup, lookup_words)) return false;

    const uintptr_t name_start = find_function_start_before_ref(exec, names[0]);
    std::vector<uintptr_t> singletons;
    if (name_start == 0) return false;
    for (uintptr_t pc = name_start; pc < names[0]; pc += sizeof(uint32_t)) {
        uintptr_t target = 0;
        uint32_t words[std::size(singleton)]{};
        if (decode_branch_target(pc, true, &target) &&
            range_contains(exec, target, sizeof(words)) && read_item_contract(target, singleton, words)) {
            push_unique(singletons, target);
        }
    }
    if (singletons.size() != 1) {
        append_item_dump_log("layout_unresolved", 0, static_cast<int>(singletons.size()), 0,
                            "singleton candidates must be unique");
        return false;
    }
    uint32_t singleton_words[std::size(singleton)]{};
    if (!read_item_contract(singletons[0], singleton, singleton_words)) return false;
    const auto sw = [&](Role role) { return item_role_word(singleton_words, singleton, role); };
    const auto nw = [&](Role role) { return item_role_word(name_words, name_call, role); };
    const auto lw = [&](Role role) { return item_role_word(lookup_words, numeric_lookup, role); };

    Layout layout{};
    layout.global = decode_adrp_ldr_global(item_role_address(singletons[0], singleton, Role::global_page),
                                          item_role_address(singletons[0], singleton, Role::global_load));
    layout.allocation_size = (sw(Role::allocation_size) >> 5U) & 0xFFFFU;
    const int64_t root = signed_bits((lw(Role::root) >> 12U) & 0x1FFU, 9);
    const int64_t item = signed_bits((lw(Role::item) >> 15U) & 0x7FU, 7) * static_cast<int64_t>(sizeof(uintptr_t));
    if (root < 0 || item < 0 || layout.global == 0 ||
        !range_contains(process_readable, layout.global, sizeof(uintptr_t)) ||
        unsigned_offset(sw(Role::global_load), 3) != unsigned_offset(sw(Role::global_store), 3)) return false;
    layout.root = static_cast<size_t>(root);
    layout.left = unsigned_offset(lw(Role::left), 3);
    layout.right = layout.left + unsigned_offset(lw(Role::right_delta), 0);
    layout.key = unsigned_offset(lw(Role::key), 2);
    layout.item = static_cast<size_t>(item);
    layout.name_slot = unsigned_offset(nw(Role::name_slot), 3);
    layout.lookup = lookups[0];
    layout.name_callsite = names[0];
    if (layout.root % alignof(uintptr_t) != 0 || layout.root + sizeof(uintptr_t) > layout.allocation_size ||
        layout.key != unsigned_offset(lw(Role::key_check), 2) ||
        layout.left == layout.right || layout.left % alignof(uintptr_t) != 0 ||
        layout.right % alignof(uintptr_t) != 0 || layout.item % alignof(uintptr_t) != 0 ||
        layout.name_slot == 0 || layout.name_slot > 4096) return false;

    // The numeric getter preserves this and the indirect shared_ptr result in these
    // registers before the matched search. Do not accept the same loop in another context.
    uintptr_t lookup_start = find_function_start_before_ref(exec, lookups[0]);
    bool input_contract = false;
    for (uintptr_t pc = lookup_start; lookup_start != 0 && pc + 8 <= lookups[0]; pc += 4) {
        if (read_u32(pc) == 0xAA0803F4U && read_u32(pc + 4) == 0xAA0003F5U) input_contract = true;
    }
    if (!input_contract) return false;

    const size_t fields[][2] = {{layout.left, sizeof(uintptr_t)}, {layout.right, sizeof(uintptr_t)},
                                {layout.key, sizeof(int32_t)}, {layout.item, sizeof(uintptr_t) * 2}};
    for (size_t i = 0; i < std::size(fields); ++i) {
        layout.node_bytes = std::max(layout.node_bytes, fields[i][0] + fields[i][1]);
        for (size_t j = 0; j < i; ++j) {
            if (fields[i][0] < fields[j][0] + fields[j][1] && fields[j][0] < fields[i][0] + fields[i][1]) return false;
        }
    }
    if (layout.node_bytes > 512) return false;  // allocation/read bound, not an object offset

    // Confirm the libc++ string ABI used by ItemNameRet at the actual callsite.
    // x8 is AAPCS64's indirect-result register. A changed return convention is unsupported.
    const size_t result = unsigned_offset(nw(Role::string_result), 0);
    if (result != unsigned_offset(nw(Role::string_alias), 0) ||
        result != unsigned_offset(nw(Role::string_tag), 0) ||
        result + offsetof(ItemNameRet, w2) != unsigned_offset(nw(Role::string_data), 3)) return false;

    std::vector<uintptr_t> string_gaps;
    for (uintptr_t pc = name_start; pc < names[0]; pc += sizeof(uint32_t)) {
        uintptr_t target = 0;
        uint32_t mw[std::size(achievement_members)]{};
        if (!decode_branch_target(pc, true, &target)) continue;
        uintptr_t mb = scoped_layout_contract(exec, target, achievement_members, mw);
        if (mb == 0) continue;
        uintptr_t tree_call_pc = item_role_address(mb, achievement_members, Role::tree_call), tree = 0;
        uint32_t tw[std::size(achievement_tree)]{};
        if (!decode_branch_target(tree_call_pc, true, &tree) ||
            !scoped_layout_contract(exec, tree, achievement_tree, tw)) continue;
        size_t key_field = unsigned_offset(item_role_word(tw, achievement_tree, Role::key), 0);
        int64_t value_field = signed_bits((item_role_word(mw, achievement_members, Role::map_value) >> 15U) & 127U, 7) * 8;
        if (key_field == unsigned_offset(item_role_word(tw, achievement_tree, Role::key_check), 0) &&
            value_field > static_cast<int64_t>(key_field)) push_unique(string_gaps, static_cast<uintptr_t>(value_field) - key_field);
    }
    if (string_gaps.size() == 1) layout.string_pair_gap = string_gaps[0];

    g_item_layout = layout;
    g_item_layout_ready.store(true, std::memory_order_release);
    *out = layout;
    char detail[384];
    std::snprintf(detail, sizeof(detail),
        "instruction-derived root=%zu left=%zu right=%zu key=%zu item=%zu nameSlot=%zu nodeBytes=%zu allocation=%zu stringPairGap=%zu",
        layout.root, layout.left, layout.right, layout.key, layout.item, layout.name_slot,
        layout.node_bytes, layout.allocation_size, layout.string_pair_gap);
    append_item_dump_log("layout_resolved", 0, 1, 1, detail);
    return true;
}

template<size_t N>
uintptr_t scoped_layout_contract(const std::vector<MemoryRange> &exec, uintptr_t function,
                                  const item_catalog::Word (&pattern)[N], uint32_t (&words)[N]) {
    if (function == 0) return 0;
    uintptr_t end = function;
    // A bounded function scan, not a private data offset. Stop at its first return.
    while (end < function + 1024 && range_contains(exec, end, sizeof(uint32_t))) {
        uint32_t word = 0;
        if (vm_read_partial(end, &word, sizeof(word)) != sizeof(word)) return 0;
        end += sizeof(word);
        if (word == 0xD65F03C0U) break;
    }
    std::vector<MemoryRange> scope;
    for (const auto &range: exec) {
        uintptr_t lo = std::max(function, range.start), hi = std::min(end, range.end);
        if (hi > lo) scope.push_back({lo, hi, true, false});
    }
    auto hits = find_item_contract(scope, pattern);
    if (hits.size() != 1 || !read_item_contract(hits[0], pattern, words)) return 0;
    return hits[0];
}

template<size_t N>
uintptr_t layout_call_target(uintptr_t block, const item_catalog::Word (&pattern)[N],
                              item_catalog::Role role) {
    uintptr_t pc = item_role_address(block, pattern, role), target = 0;
    return pc != 0 && decode_branch_target(pc, true, &target) ? target : 0;
}

uintptr_t resolve_userdata_push_from_contract(const std::vector<MemoryRange> &readable,
                                               const std::vector<MemoryRange> &exec) {
    auto hits = find_anchored_item_contract(readable, exec,
        "void toybox::userdata::UserDataFBS::pushBackTokenShopCommodity(", item_catalog::userdata_members);
    return hits.size() == 1 ? find_function_start_before_ref(exec, hits[0]) : 0;
}

item_catalog::ShopContext resolve_shop_context(const std::vector<MemoryRange> &exec, uintptr_t function) {
    item_catalog::ShopContext result{};
    if (function == 0) return result;
    size_t matches = 0;
    for (uintptr_t pc = function; pc < function + 256; pc += sizeof(uint32_t)) {
        uint32_t words[4]{};
        if (!range_contains(exec, pc, sizeof(words)) ||
            vm_read_partial(pc, words, sizeof(words)) != sizeof(words)) break;
        // LDR saved,[this,state]; LDR x0,[saved,selection]; BL; repeated selection load.
        uint32_t saved = words[0] & 31U;
        if ((words[0] & 0xFFC003E0U) != 0xF9400000U || saved < 19 || saved > 28 ||
            (words[1] & 0xFFC003FFU) != (0xF9400000U | (saved << 5U)) ||
            (words[2] & 0xFC000000U) != 0x94000000U || words[1] != words[3]) continue;
        result.state = item_catalog::unsigned_offset(words[0], 3);
        result.selection = item_catalog::unsigned_offset(words[1], 3);
        ++matches;
    }
    result.valid = matches == 1;
    return result;
}

item_catalog::LiveShopLayout resolve_live_shop_layout(const std::vector<MemoryRange> &exec,
                                                      uintptr_t purchase, size_t amount_slot) {
    using namespace item_catalog;
    LiveShopLayout result{};
    uint32_t parent[std::size(shop_accessors)]{};
    uintptr_t block = scoped_layout_contract(exec, purchase, shop_accessors, parent);
    if (block == 0) return result;
    uintptr_t leaves[4]{};
    size_t inner = SIZE_MAX;
    const Role roles[] = {Role::selected_item_call, Role::selected_currency_call,
                          Role::selected_commodity_call, Role::selected_quantity_call};
    for (size_t i = 0; i < std::size(roles); ++i) {
        uintptr_t proxy = layout_call_target(block, shop_accessors, roles[i]);
        uint32_t words[std::size(shop_proxy)]{};
        if (!range_contains(exec, proxy, sizeof(words)) || !read_item_contract(proxy, shop_proxy, words)) return result;
        size_t field = unsigned_offset(item_role_word(words, shop_proxy, Role::model_inner), 3);
        if (inner != SIZE_MAX && inner != field) return result;
        inner = field;
        if (!decode_branch_target(item_role_address(proxy, shop_proxy, Role::model_forward), false, &leaves[i])) return result;
    }
    size_t objects[3]{}, controls[3]{};
    for (size_t i = 0; i < std::size(objects); ++i) {
        uint32_t words[std::size(shop_ref)]{};
        if (!range_contains(exec, leaves[i], sizeof(words)) || !read_item_contract(leaves[i], shop_ref, words)) return result;
        objects[i] = unsigned_offset(item_role_word(words, shop_ref, Role::model_object), 3);
        controls[i] = unsigned_offset(item_role_word(words, shop_ref, Role::model_control), 3);
        if (controls[i] != objects[i] + sizeof(uintptr_t)) return result;  // shared_ptr ABI, corroborated by the getter
    }
    uint32_t qty[std::size(shop_quantity)]{};
    if (!range_contains(exec, leaves[3], sizeof(qty)) || !read_item_contract(leaves[3], shop_quantity, qty)) return result;
    result.quantity = unsigned_offset(item_role_word(qty, shop_quantity, Role::model_quantity), 2);
    std::vector<uintptr_t> refreshes;
    for (uintptr_t hit: find_item_contract(exec, shop_refresh)) {
        uint32_t words[std::size(shop_refresh)]{};
        if (!read_item_contract(hit, shop_refresh, words)) continue;
        auto field = [&](Role role, unsigned scale) { return unsigned_offset(item_role_word(words, shop_refresh, role), scale); };
        if (field(Role::view_item, 3) == objects[0] && field(Role::view_currency, 3) == objects[1] &&
            field(Role::view_commodity, 3) == objects[2] && field(Role::model_quantity, 2) == result.quantity &&
            field(Role::amount_slot, 3) == amount_slot &&
            layout_call_target(hit, shop_refresh, Role::selected_cost_call) ==
                layout_call_target(block, shop_accessors, Role::selected_cost_call)) refreshes.push_back(hit);
    }
    if (refreshes.size() != 1) return result;
    uint32_t words[std::size(shop_refresh)]{};
    if (!read_item_contract(refreshes[0], shop_refresh, words)) return result;
    result.item = objects[0]; result.control = controls[0];
    result.currency = objects[1]; result.currency_control = controls[1];
    result.commodity = objects[2];
    result.marker = unsigned_offset(item_role_word(words, shop_refresh, Role::view_marker), 0);
    result.extent = std::max({result.control + sizeof(uintptr_t), result.currency_control + sizeof(uintptr_t),
                              result.commodity + sizeof(uintptr_t), result.quantity + sizeof(uint32_t),
                              result.marker + sizeof(uint64_t)});
    result.valid = result.item != result.currency && result.item != result.commodity &&
                   result.currency != result.commodity && result.extent <= 65536;
    return result;
}

void resolve_object_layouts(const std::vector<MemoryRange> &exec,
                             uintptr_t item_writer, uintptr_t userdata_push,
                             uintptr_t domain_set_total, uintptr_t token_purchase,
                             uintptr_t appraisal_purchase, uintptr_t achievement_get,
                             uintptr_t fire_achievement) {
    using namespace item_catalog;
    ObjectLayouts result{};
    uint32_t uw[std::size(userdata_members)]{}, dw[std::size(userdata_dirty)]{};
    uintptr_t ub = scoped_layout_contract(exec, userdata_push, userdata_members, uw);
    uintptr_t db = scoped_layout_contract(exec, userdata_push, userdata_dirty, dw);
    auto u = [&](Role role) { return item_role_word(uw, userdata_members, role); };
    uintptr_t secure = ub ? layout_call_target(ub, userdata_members, Role::secure_call) : 0;
    if (secure == 0 && g_item_layout_ready.load(std::memory_order_acquire)) {
        uintptr_t first = find_function_start_before_ref(exec, g_item_layout.lookup);
        for (uintptr_t pc = first; first != 0 && pc < g_item_layout.lookup; pc += 4) {
            uintptr_t target = 0;
            if (decode_branch_target(pc, true, &target)) secure = target;
        }
    }
    uint32_t sw[std::size(secure_integer)]{};
    if (scoped_layout_contract(exec, secure, secure_integer, sw)) {
        auto field = [&](Role role, unsigned scale) {
            return unsigned_offset(item_role_word(sw, secure_integer, role), scale);
        };
        result.secure.getter = secure;
        result.secure.array = field(Role::secure_array, 3);
        result.secure.seed = field(Role::secure_seed, 2);
        result.secure.key = field(Role::secure_key, 3);
        result.secure.index = field(Role::secure_index, 3);
        result.secure.valid = result.secure.array != result.secure.key &&
                              result.secure.key != result.secure.index &&
                              result.secure.array != result.secure.index;
    }
    uint32_t calls[std::size(shop_user_calls)]{};
    uintptr_t cb = scoped_layout_contract(exec, domain_set_total, shop_user_calls, calls);
    uintptr_t get_user = cb ? layout_call_target(cb, shop_user_calls, Role::domain_user_call) : 0;
    uintptr_t set_user = cb ? layout_call_target(cb, shop_user_calls, Role::user_setter_call) : 0;
    uint32_t pw[std::size(shop_user_member)]{}, mw[std::size(shop_master_member)]{};
    uint32_t tw[std::size(shop_user_total)]{};
    uint32_t rw[std::size(shop_user_return)]{};
    uintptr_t rb = scoped_layout_contract(exec, get_user, shop_user_return, rw);
    uintptr_t pb = scoped_layout_contract(exec, get_user, shop_user_member, pw);
    uintptr_t mb = scoped_layout_contract(exec, get_user, shop_master_member, mw);
    uintptr_t tb = scoped_layout_contract(exec, set_user, shop_user_total, tw);
    if (ub && db && pb && mb && tb && rb && result.secure.valid &&
        layout_call_target(tb, shop_user_total, Role::secure_call) == result.secure.getter) {
        auto &shop = result.shop;
        shop.user = static_cast<size_t>(signed_bits((item_role_word(pw, shop_user_member, Role::user_ptr) >> 12U) & 511U, 9));
        shop.master = static_cast<size_t>(signed_bits((item_role_word(mw, shop_master_member, Role::master_ptr) >> 15U) & 127U, 7) * 8);
        shop.control = unsigned_offset(item_role_word(rw, shop_user_return, Role::user_control), 3);
        shop.id = unsigned_offset(u(Role::commodity_id), 0);
        shop.total = unsigned_offset(item_role_word(tw, shop_user_total, Role::commodity_total), 0);
        shop.dirty = unsigned_offset(item_role_word(tw, shop_user_total, Role::commodity_dirty), 0);
        shop.userdata_vector = (u(Role::userdata_vector) >> 5U) & 0xFFFFU;
        shop.userdata_map = (u(Role::userdata_map) >> 5U) & 0xFFFFU;
        shop.userdata_dirty = (item_role_word(dw, userdata_dirty, Role::userdata_dirty) >> 5U) & 0xFFFFU;
        shop.valid = shop.user < 4096 && shop.master < 4096 && shop.user != shop.master &&
                     shop.user == unsigned_offset(item_role_word(rw, shop_user_return, Role::user_ptr_check), 3) &&
                     shop.control != shop.user && shop.id != shop.total &&
                     shop.dirty != shop.id && shop.dirty != shop.total &&
                     shop.userdata_map != shop.userdata_vector &&
                     shop.userdata_dirty != shop.userdata_map && shop.userdata_dirty != shop.userdata_vector;
    }
    result.shop.token = resolve_shop_context(exec, token_purchase);
    result.shop.appraisal = resolve_shop_context(exec, appraisal_purchase);

    // Follow the item-writer's amount-changing call, then decode the virtual amount getter.
    std::vector<uintptr_t> amount_slots;
    for (uintptr_t pc = item_writer; item_writer != 0 && pc < item_writer + 64; pc += 4) {
        if (!range_contains(exec, pc, 4)) break;
        uint32_t word = read_u32(pc);
        if (word == 0xD65F03C0U || (word & 0xFC000000U) == 0x14000000U) break;
        uintptr_t target = 0;
        uint32_t aw[std::size(item_amount_call)]{};
        if (decode_branch_target(pc, true, &target) && scoped_layout_contract(exec, target, item_amount_call, aw)) {
            push_unique(amount_slots, unsigned_offset(item_role_word(aw, item_amount_call, Role::amount_slot), 3));
        }
    }
    if (amount_slots.size() == 1) { result.amount_valid = true; result.amount_slot = amount_slots[0]; }

    uint32_t am[std::size(achievement_members)]{};
    uintptr_t ab = scoped_layout_contract(exec, achievement_get, achievement_members, am);
    uintptr_t tree = ab ? layout_call_target(ab, achievement_members, Role::tree_call) : 0;
    uint32_t at[std::size(achievement_tree)]{};
    if (ab && scoped_layout_contract(exec, tree, achievement_tree, at)) {
        auto &map = result.achievement;
        size_t base = unsigned_offset(item_role_word(am, achievement_members, Role::achievement_map), 0);
        int64_t root = signed_bits((item_role_word(at, achievement_tree, Role::root) >> 12U) & 511U, 9);
        if (root >= 0) {
            map.root = base + static_cast<size_t>(root);
            map.key = unsigned_offset(item_role_word(at, achievement_tree, Role::key), 0);
            map.left = unsigned_offset(item_role_word(at, achievement_tree, Role::left), 3);
            map.right = map.left + unsigned_offset(item_role_word(at, achievement_tree, Role::right_delta), 0);
            map.node_bytes = std::max({map.key + sizeof(std::string), map.left + sizeof(uintptr_t), map.right + sizeof(uintptr_t)});
            map.map_valid = map.root == unsigned_offset(item_role_word(am, achievement_members, Role::achievement_end), 0) &&
                map.key == unsigned_offset(item_role_word(at, achievement_tree, Role::key_check), 0) &&
                map.left != map.right && map.node_bytes <= 512;
        }
    }
    uint32_t ew[std::size(achievement_event)]{};
    if (scoped_layout_contract(exec, fire_achievement, achievement_event, ew)) {
        int64_t label = signed_bits((item_role_word(ew, achievement_event, Role::event_label) >> 12U) & 511U, 9);
        int64_t pair = signed_bits((item_role_word(ew, achievement_event, Role::event_zero_pair) >> 15U) & 127U, 7) * 8;
        if (label >= 0 && label + static_cast<int64_t>(sizeof(uintptr_t)) == pair) {
            result.achievement.event_label = static_cast<size_t>(label);
            result.achievement.event_valid = true;
        }
    }
    if (result.amount_valid) result.live = resolve_live_shop_layout(exec, token_purchase, result.amount_slot);
    g_object_layouts = result;
    g_object_layouts_ready.store(true, std::memory_order_release);
    char detail[640];
    std::snprintf(detail, sizeof(detail),
        "secure=%d shop=%d amount=%d achievementMap=%d event=%d userId=%zu userTotal=%zu userDirty=%zu vector=%zu map=%zu userdataDirty=%zu amountSlot=%zu",
        result.secure.valid, result.shop.valid, result.amount_valid, result.achievement.map_valid,
        result.achievement.event_valid, result.shop.id, result.shop.total, result.shop.dirty,
        result.shop.userdata_vector, result.shop.userdata_map, result.shop.userdata_dirty, result.amount_slot);
    append_item_dump_log("object_layouts", 0, 0, 0, detail);
    std::snprintf(detail, sizeof(detail), "valid=%d item=%zu control=%zu currency=%zu quantity=%zu marker=%zu",
                  result.live.valid, result.live.item, result.live.control, result.live.currency,
                  result.live.quantity, result.live.marker);
    append_item_dump_log("live_shop_layout", 0, 0, 0, detail);
}


template<size_t N>
uintptr_t bridge_receiver_global(const std::vector<MemoryRange> &ranges, uintptr_t address,
                                  const item_catalog::Word (&pattern)[N]) {
    uint32_t words[N]{};
    if (address == 0 || !range_contains(ranges, address, sizeof(words)) ||
        !read_item_contract(address, pattern, words)) return 0;
    return decode_adrp_ldr_global(item_role_address(address, pattern, item_catalog::Role::bridge_receiver_page),
                                 item_role_address(address, pattern, item_catalog::Role::bridge_receiver_load));
}



uintptr_t resolve_currency_token_site(const std::vector<MemoryRange> &ranges) {
    const auto matches = find_item_contract(ranges, item_catalog::purchase_token_read);
    return matches.size() == 1 ? matches.front() + 6 * sizeof(uint32_t) : 0;
}

#include "runtime_feature_signatures.h"

// Hook handles own their process-local instruction trampolines. Config changes
// change proxy behavior, never rewrite executable code or restore saved offsets.
// libapp remains loaded for this module's supported process lifetime.
struct RuntimeFeatureTargets {
    uintptr_t mp_cost = 0, mp_delta = 0, mp_current = 0, mp_max = 0;
    uintptr_t damage = 0, dungeon = 0, encounter = 0;
    bool dungeon_two_args = false;
};
RuntimeFeatureTargets resolve_runtime_feature_targets(const std::vector<MemoryRange> &ranges) {
    using namespace runtime_feature_signatures;
    RuntimeFeatureTargets out;
    const auto unique = [&](const auto &pattern, const uint8_t *mask = nullptr) {
        auto hits = find_code_pattern(ranges, pattern, sizeof(pattern), mask);
        return hits.size() == 1 ? hits.front() : uintptr_t(0);
    };
    out.mp_cost = unique(mp_cost_orig);
    out.mp_delta = unique(mp_delta_orig, mp_delta_mask);
    out.mp_current = unique(pc_mp_orig);
    if (out.mp_current) {
        uintptr_t getter = 0;
        const size_t field = item_catalog::unsigned_offset(read_u32(out.mp_current + 7 * 4), 3);
        std::vector<uintptr_t> candidates;
        if (decode_branch_target(out.mp_current + 11 * 4, true, &getter)) {
            for (auto hit : find_code_pattern(ranges, pc_mp_max_orig, sizeof(pc_mp_max_orig))) {
                uintptr_t callee = 0;
                if (item_catalog::unsigned_offset(read_u32(hit + 2 * 4), 3) == field &&
                    decode_branch_target(hit + 4 * 4, true, &callee) && callee == getter)
                    candidates.push_back(hit);
            }
        }
        if (candidates.size() == 1) out.mp_max = candidates.front();
    }
    out.damage = unique(damage_orig);
    const auto old = find_code_pattern(ranges, dungeon_orig, sizeof(dungeon_orig));
    const auto modern = find_code_pattern(ranges, dungeon_orig_v316, sizeof(dungeon_orig_v316), dungeon_mask_v316);
    if (old.size() + modern.size() == 1) {
        out.dungeon_two_args = !modern.empty();
        out.dungeon = out.dungeon_two_args ? modern.front() : old.front();
    }
    out.encounter = unique(encounter_orig);
    return out;
}


#include "runtime_context_contracts.h"

uintptr_t injection_function_start(const std::vector<MemoryRange> &, const std::vector<MemoryRange> &, uintptr_t);
bool decode_adrp_page(uint32_t, uintptr_t, uintptr_t *, uint32_t *);
bool decode_add_immediate(uint32_t, uint32_t, uintptr_t, uintptr_t *);
uintptr_t unique_primary_vtable(const std::vector<MemoryRange> &, const std::vector<MemoryRange> &, const char *);
bool function_references_address(const std::vector<MemoryRange> &, uintptr_t, size_t, uintptr_t);
std::vector<uintptr_t> find_pc_relative_refs(const std::vector<MemoryRange> &, uintptr_t);

template<size_t N>
uintptr_t context_contract(const std::vector<MemoryRange> &x, uintptr_t function, const item_catalog::Word (&pattern)[N]) {
    uint32_t words[N]{};
    return scoped_layout_contract(x, function, pattern, words);
}
std::vector<uintptr_t> context_named_functions(const std::vector<MemoryRange> &r, const std::vector<MemoryRange> &x, const char *name) {
    std::vector<uintptr_t> result;
    for (const auto string : find_pattern(r, reinterpret_cast<const uint8_t *>(name), std::strlen(name) + 1))
        for (const auto ref : find_pc_relative_refs(x, string)) {
            const auto function = injection_function_start(r, x, ref);
            if (function) push_unique(result, function);
        }
    return result;
}
struct AdLayout {
    uintptr_t owner=0, availability=0, sdk_show=0, sdk_return=0, post_store=0, receiver=0;
    size_t slots[4]{};
    bool valid=false;
};
AdLayout resolve_ad_context(const std::vector<MemoryRange> &r, const std::vector<MemoryRange> &x) {
    using namespace runtime_context_contracts;
    AdLayout out;
    const auto java_class = find_pattern(r, reinterpret_cast<const uint8_t *>("net/wrightflyer/toybox/IronSourceBridge"),
                                        sizeof("net/wrightflyer/toybox/IronSourceBridge"));
    const auto wrapper = [&](const char *method) {
        std::vector<uintptr_t> matches;
        for (const auto function : context_named_functions(r,x,method)) {
            if (context_contract(x,function,ad_bridge_entry) != function) continue;
            for (const auto text : java_class) if (function_references_address(x,function,512,text)) push_unique(matches,function);
        }
        return matches.size()==1 ? matches.front() : uintptr_t(0);
    };
    out.sdk_show = wrapper("showRewardedVideo");
    out.availability = wrapper("isRewardedVideoAvailable");
    if (!out.sdk_show || !out.availability || !context_contract(x,out.availability,ad_availability_return)) return {};
    uintptr_t outer_function=0;
    std::vector<uintptr_t> outer_matches;
    for (const auto at : find_item_contract(x,ad_outer)) {
        uintptr_t availability=0, show=0;
        if (decode_branch_target(at+8,true,&availability) && availability==out.availability &&
            decode_branch_target(at+36,true,&show) && show==out.sdk_show) outer_matches.push_back(at);
    }
    if (outer_matches.size()!=1) return {};
    outer_function=injection_function_start(r,x,outer_matches.front());
    out.sdk_return=outer_matches.front()+40;
    const auto callback_table=unique_primary_vtable(r,x,
        "NSt6__ndk110__function6__funcIZN6toybox3gui15AdColonyUIState4initEvE3$_0NS_9allocatorIS5_EEFvbEEE");
    if (!outer_function || !callback_table) return {};
    std::vector<uintptr_t> owners;
    for (const auto function : context_named_functions(r,x,"advertisement_reward_receive_flag")) {
        const auto callback=context_contract(x,function,ad_callback);
        if (!context_contract(x,function,ad_owner_entry) || !callback) continue;
        uintptr_t target=0,page=0,table=0; uint32_t reg=0;
        if (!decode_branch_target(callback+28,true,&target) || target!=outer_function ||
            !decode_adrp_page(read_u32(callback+4),callback+4,&page,&reg) ||
            !decode_add_immediate(read_u32(callback+8),reg,page,&table) || table!=callback_table) continue;
        const auto state=context_contract(x,function,item_catalog::ad_state);
        if (!state || !decode_branch_target(state+8,false,&target) ||
            !range_contains(x,target,4+sizeof(ad_state_epilogue)/sizeof(ad_state_epilogue[0])*4) ||
            (read_u32(target)&0xFFC003FFU)!=0x39000268U) continue;
        uint32_t words[std::size(ad_state_epilogue)]{};
        if (!read_item_contract(target+4,ad_state_epilogue,words)) continue;
        owners.push_back(function); out.post_store=target+4;
    }
    if (owners.size()!=1) return {};
    out.owner=owners.front();
    const char *names[]={"Java_net_wrightflyer_toybox_IronSourceEvents_onRewardedVideoAdOpened",
        "Java_net_wrightflyer_toybox_IronSourceEvents_onRewardedVideoAdRewarded",
        "Java_net_wrightflyer_toybox_IronSourceEvents_onRewardedVideoAdClosed",
        "Java_net_wrightflyer_toybox_IronSourceEvents_onRewardedVideoAvailabilityChanged"};
    for (size_t i=0;i<4;++i) {
        const auto function=reinterpret_cast<uintptr_t>(resolve_app_symbol(names[i]));
        uintptr_t receiver=0;
        const auto decode=[&](const auto &pattern) {
            const auto global=bridge_receiver_global(x,function,pattern);
            if (!global) return uintptr_t(0);
            const auto instruction=item_role_address(function,pattern,item_catalog::Role::bridge_callback_slot);
            out.slots[i]=item_catalog::unsigned_offset(read_u32(instruction),3);
            return global;
        };
        if (i==1) {
            const auto current=bridge_receiver_global(x,function,item_catalog::bridge_reward);
            const auto legacy=bridge_receiver_global(x,function,item_catalog::bridge_reward_legacy);
            if (bool(current)==bool(legacy)) return {};
            receiver=current ? decode(item_catalog::bridge_reward) : decode(item_catalog::bridge_reward_legacy);
        } else if(i==3) receiver=decode(item_catalog::bridge_available);
        else receiver=decode(item_catalog::bridge_simple);
        if (!receiver || (out.receiver && out.receiver!=receiver) || out.slots[i]>4096) return {};
        out.receiver=receiver;
    }
    out.valid=true;
    return out;
}


struct TeamLayout { uintptr_t call=0, helper=0; size_t kind_slot=0; bool valid=false; };
TeamLayout resolve_team_context(const std::vector<MemoryRange> &x) {
    TeamLayout out;
    const auto tails=find_item_contract(x,item_catalog::team_branch_tail);
    if(tails.size()!=1) return out;
    uint32_t words[std::size(item_catalog::team_actor_type)]{};
    if(!scoped_layout_contract(x,tails.front(),item_catalog::team_actor_type,words)) return out;
    out.kind_slot=item_catalog::unsigned_offset(item_role_word(words,item_catalog::team_actor_type,item_catalog::Role::actor_type_slot),3);
    out.call=tails.front()+sizeof(item_catalog::team_branch_tail)/sizeof(item_catalog::team_branch_tail[0])*4;
    uint32_t body[std::size(runtime_context_contracts::player_hp_delta)]{};
    if(!out.kind_slot || out.kind_slot>4096 || !decode_branch_target(out.call,true,&out.helper) ||
       !range_contains(x,out.helper,sizeof(body)) || !read_item_contract(out.helper,runtime_context_contracts::player_hp_delta,body)) return {};
    out.valid=true;
    return out;
}

#include "injection-audit.h"

#include "lua_registration_model.inc"

#include "resolver-audit.h"

int main(int argc,char**argv){std::string dir=argv[1];std::ifstream in(dir+"/image.bin",std::ios::binary);std::vector<char> raw((std::istreambuf_iterator<char>(in)),{});void *mem=nullptr;posix_memalign(&mem,4096,raw.size());memcpy(mem,raw.data(),raw.size());uintptr_t base=(uintptr_t)mem;audit_base=base;
std::ifstream rel(dir+"/relocs.txt");uint64_t o,v;while(rel>>o>>v){uintptr_t p=base+v;memcpy((void*)(base+o),&p,8);}
std::ifstream symbols(dir+"/symbols.txt");std::string name;while(symbols>>name>>v)audit_symbols[name]=base+v;
std::vector<MemoryRange> ranges,readable_ranges;std::ifstream segs(dir+"/segments.txt");uint64_t a,z,fl;while(segs>>a>>z>>fl){readable_ranges.push_back({base+a,base+a+z,bool(fl&1),bool(fl&2)});if(fl&1)ranges.push_back(readable_ranges.back());}audit_ranges=ranges;audit_readable=readable_ranges;
std::vector<char> before((char*)mem,(char*)mem+raw.size());
printf("AUDIT_VERSION 3\n");
auto report=[&](const char*n,uintptr_t a){printf("RESULT %s 0x%llx\n",n,(unsigned long long)(a?a-base:0));};

uintptr_t item_writer_addr=resolve_masked_pattern_hook_address(
            ranges, "final.itemWriter.patch", sig_item_writer_pattern, sig_item_writer_pattern_mask,
            sizeof(sig_item_writer_pattern));report("final.itemWriter.patch",item_writer_addr);
uintptr_t reward_writer_addr=resolve_pattern_hook_address(
            ranges, "final.rewardWriter.patch", sig_reward_writer, sizeof(sig_reward_writer));report("final.rewardWriter.patch",reward_writer_addr);
uintptr_t quest_reward_addr=resolve_masked_pattern_hook_address(
            ranges, "quest.rewardGrant.patch",
            sig_quest_reward_pattern, sig_quest_reward_pattern_mask,
            sizeof(sig_quest_reward_pattern));report("quest.rewardGrant.patch",quest_reward_addr);
uintptr_t domain_item_set_amount_addr=resolve_pattern_hook_address(
            ranges, "domainItem.setAmount.patch", sig_domain_item_set_amount, sizeof(sig_domain_item_set_amount));report("domainItem.setAmount.patch",domain_item_set_amount_addr);
uintptr_t token_shop_purchase_addr=resolve_masked_pattern_hook_address(
            ranges, "tokenShop.purchase.trace",
            sig_token_shop_purchase, mask_token_shop_purchase,
            sizeof(sig_token_shop_purchase));report("tokenShop.purchase.trace",token_shop_purchase_addr);
uintptr_t appraisal_exchange_shop_purchase_addr=resolve_masked_pattern_hook_address(
            ranges, "appraisalExchange.purchase.trace",
            sig_appraisal_exchange_shop_purchase, mask_appraisal_exchange_shop_purchase,
            sizeof(sig_appraisal_exchange_shop_purchase));report("appraisalExchange.purchase.trace",appraisal_exchange_shop_purchase_addr);
uintptr_t token_shop_trade_complete_addr=resolve_masked_pattern_hook_address(
            ranges, "tokenShop.tradeComplete.trace",
            sig_token_shop_trade_complete, mask_token_shop_trade_complete,
            sizeof(sig_token_shop_trade_complete));report("tokenShop.tradeComplete.trace",token_shop_trade_complete_addr);
uintptr_t domain_token_shop_set_total_addr=resolve_pattern_hook_address(
            ranges, "domainTokenShopCommodity.setTotal.trace",
            sig_domain_token_shop_set_total, sizeof(sig_domain_token_shop_set_total));report("domainTokenShopCommodity.setTotal.trace",domain_token_shop_set_total_addr);
uintptr_t domain_token_shop_reset_total_addr=resolve_pattern_hook_address(
            ranges, "domainTokenShopCommodity.resetTotal.trace",
            sig_domain_token_shop_reset_total, sizeof(sig_domain_token_shop_reset_total));report("domainTokenShopCommodity.resetTotal.trace",domain_token_shop_reset_total_addr);
uintptr_t start_in_addr=resolve_masked_pattern_hook_address(
                ranges, "dialogue.FieldTalkBaseNode.startInAnimation.trace",
                sig_field_talk_start_in_animation, mask_field_talk_start_in_animation,
                sizeof(sig_field_talk_start_in_animation));report("dialogue.FieldTalkBaseNode.startInAnimation.trace",start_in_addr);
uintptr_t start_out_addr=resolve_masked_pattern_hook_address(
                ranges, "dialogue.FieldTalkBaseNode.startOutAnimation.trace",
                sig_field_talk_start_out_animation, mask_field_talk_start_out_animation,
                sizeof(sig_field_talk_start_out_animation));report("dialogue.FieldTalkBaseNode.startOutAnimation.trace",start_out_addr);
uintptr_t on_end_in_addr=resolve_masked_pattern_hook_address(
                ranges, "dialogue.FieldTalkBaseNode.onEndInAnimation.trace",
                sig_field_talk_on_end_in_animation, mask_field_talk_on_end_in_animation,
                sizeof(sig_field_talk_on_end_in_animation));report("dialogue.FieldTalkBaseNode.onEndInAnimation.trace",on_end_in_addr);
uintptr_t lua_getGlobalFlag=0;bool lua_ok_getGlobalFlag=resolve_lua_registered_function(readable_ranges,ranges,"getGlobalFlag",false,&lua_getGlobalFlag);
report("lua.getGlobalFlag",lua_getGlobalFlag);
uintptr_t lua_setGlobalFlag=0;bool lua_ok_setGlobalFlag=resolve_lua_registered_function(readable_ranges,ranges,"setGlobalFlag",false,&lua_setGlobalFlag);
report("lua.setGlobalFlag",lua_setGlobalFlag);
uintptr_t lua_fireAchievementTrigger=0;bool lua_ok_fireAchievementTrigger=resolve_lua_registered_function(readable_ranges,ranges,"fireAchievementTrigger",true,&lua_fireAchievementTrigger);
if(lua_ok_fireAchievementTrigger && !lua_fireAchievementTrigger) printf("OPTIONAL_ABSENT lua.fireAchievementTrigger\n");
report("lua.fireAchievementTrigger",lua_fireAchievementTrigger);
uintptr_t lua_isAchievementCompleted=0;bool lua_ok_isAchievementCompleted=resolve_lua_registered_function(readable_ranges,ranges,"isAchievementCompleted",true,&lua_isAchievementCompleted);
if(lua_ok_isAchievementCompleted && !lua_isAchievementCompleted) printf("OPTIONAL_ABSENT lua.isAchievementCompleted\n");
report("lua.isAchievementCompleted",lua_isAchievementCompleted);
uintptr_t lua_changeReservedItemAmount=0;bool lua_ok_changeReservedItemAmount=resolve_lua_registered_function(readable_ranges,ranges,"changeReservedItemAmount",false,&lua_changeReservedItemAmount);
report("lua.changeReservedItemAmount",lua_changeReservedItemAmount);
uintptr_t lua_changeGimmickItemAmount=0;bool lua_ok_changeGimmickItemAmount=resolve_lua_registered_function(readable_ranges,ranges,"changeGimmickItemAmount",false,&lua_changeGimmickItemAmount);
report("lua.changeGimmickItemAmount",lua_changeGimmickItemAmount);
uintptr_t lua_setMysteryItemAmount=0;bool lua_ok_setMysteryItemAmount=resolve_lua_registered_function(readable_ranges,ranges,"setMysteryItemAmount",true,&lua_setMysteryItemAmount);
if(lua_ok_setMysteryItemAmount && !lua_setMysteryItemAmount) printf("OPTIONAL_ABSENT lua.setMysteryItemAmount\n");
report("lua.setMysteryItemAmount",lua_setMysteryItemAmount);
uintptr_t lua_helixChangeItemAmount=0;bool lua_ok_helixChangeItemAmount=resolve_lua_registered_function(readable_ranges,ranges,"helixChangeItemAmount",true,&lua_helixChangeItemAmount);
if(lua_ok_helixChangeItemAmount && !lua_helixChangeItemAmount) printf("OPTIONAL_ABSENT lua.helixChangeItemAmount\n");
report("lua.helixChangeItemAmount",lua_helixChangeItemAmount);
uintptr_t lua_setAutoFeedMode=0;bool lua_ok_setAutoFeedMode=resolve_lua_registered_function(readable_ranges,ranges,"setAutoFeedMode",true,&lua_setAutoFeedMode);
if(lua_ok_setAutoFeedMode && !lua_setAutoFeedMode) printf("OPTIONAL_ABSENT lua.setAutoFeedMode\n");
report("lua.setAutoFeedMode",lua_setAutoFeedMode);
uintptr_t lua_getAutoFeedMode=0;bool lua_ok_getAutoFeedMode=resolve_lua_registered_function(readable_ranges,ranges,"getAutoFeedMode",true,&lua_getAutoFeedMode);
if(lua_ok_getAutoFeedMode && !lua_getAutoFeedMode) printf("OPTIONAL_ABSENT lua.getAutoFeedMode\n");
report("lua.getAutoFeedMode",lua_getAutoFeedMode);
uintptr_t lua_setAutoFeedWaitTimeLetter=0;bool lua_ok_setAutoFeedWaitTimeLetter=resolve_lua_registered_function(readable_ranges,ranges,"setAutoFeedWaitTimeLetter",true,&lua_setAutoFeedWaitTimeLetter);
if(lua_ok_setAutoFeedWaitTimeLetter && !lua_setAutoFeedWaitTimeLetter) printf("OPTIONAL_ABSENT lua.setAutoFeedWaitTimeLetter\n");
report("lua.setAutoFeedWaitTimeLetter",lua_setAutoFeedWaitTimeLetter);
uintptr_t lua_setAutoFeedWaitTimeMinimum=0;bool lua_ok_setAutoFeedWaitTimeMinimum=resolve_lua_registered_function(readable_ranges,ranges,"setAutoFeedWaitTimeMinimum",true,&lua_setAutoFeedWaitTimeMinimum);
if(lua_ok_setAutoFeedWaitTimeMinimum && !lua_setAutoFeedWaitTimeMinimum) printf("OPTIONAL_ABSENT lua.setAutoFeedWaitTimeMinimum\n");
report("lua.setAutoFeedWaitTimeMinimum",lua_setAutoFeedWaitTimeMinimum);
uintptr_t lua_getShowTalkSkipButtonTime=0;bool lua_ok_getShowTalkSkipButtonTime=resolve_lua_registered_function(readable_ranges,ranges,"getShowTalkSkipButtonTime",true,&lua_getShowTalkSkipButtonTime);
if(lua_ok_getShowTalkSkipButtonTime && !lua_getShowTalkSkipButtonTime) printf("OPTIONAL_ABSENT lua.getShowTalkSkipButtonTime\n");
report("lua.getShowTalkSkipButtonTime",lua_getShowTalkSkipButtonTime);
uintptr_t lua_resetPlaySpeedAndAutoText=0;bool lua_ok_resetPlaySpeedAndAutoText=resolve_lua_registered_function(readable_ranges,ranges,"resetPlaySpeedAndAutoText",true,&lua_resetPlaySpeedAndAutoText);
if(lua_ok_resetPlaySpeedAndAutoText && !lua_resetPlaySpeedAndAutoText) printf("OPTIONAL_ABSENT lua.resetPlaySpeedAndAutoText\n");
report("lua.resetPlaySpeedAndAutoText",lua_resetPlaySpeedAndAutoText);
uintptr_t lua_isTalking=0;bool lua_ok_isTalking=resolve_lua_registered_function(readable_ranges,ranges,"isTalking",true,&lua_isTalking);
if(lua_ok_isTalking && !lua_isTalking) printf("OPTIONAL_ABSENT lua.isTalking\n");
report("lua.isTalking",lua_isTalking);
uintptr_t lua_changeItemAmount=0;bool lua_ok_changeItemAmount=resolve_lua_registered_function(readable_ranges,ranges,"changeItemAmount",false,&lua_changeItemAmount);
report("lua.changeItemAmount",lua_changeItemAmount);
uintptr_t add_pc_exp_addr=resolve_add_pc_exp(readable_ranges,ranges);report("battle.addPCExp.patch",add_pc_exp_addr);
uintptr_t cat_scratch_total_addr=resolve_named_integer_setter(readable_ranges,ranges,"stampTotal");report("catScratch.total",cat_scratch_total_addr);
report("catScratch.namedCount",resolve_named_integer_setter(readable_ranges,ranges,"stampCount"));
item_catalog::Layout il{};bool item_ok=resolve_item_catalog_layout(readable_ranges,readable_ranges,ranges,&il);report("item.global",il.global);
uintptr_t ag=resolve_domain_achievement_repository_get(readable_ranges,ranges);report("achievement.get",ag);report("achievement.dispatch",resolve_achievement_fire_event_dispatch(readable_ranges,ranges));
uintptr_t up=resolve_userdata_push_from_contract(readable_ranges,ranges);report("userdata.push",up);
resolve_object_layouts(ranges,item_writer_addr,up,domain_token_shop_set_total_addr,token_shop_purchase_addr,appraisal_exchange_shop_purchase_addr,ag,lua_fireAchievementTrigger);
// Production initialization publishes the catalog layout before resolving injection.
g_item_layout=il;g_item_layout_ready.store(item_ok);
bool injection_ok=audit_item_injection(readable_ranges,ranges,token_shop_purchase_addr,item_writer_addr);
auto dg=resolve_dialogue_native_hooks(ranges);report("dialogue.renderChecker",dg.rendering_checker);
report("dialogue.native.page_jump_delay",dg.page_jump_delay);
report("dialogue.native.talk_layer_run_auto_touch",dg.talk_layer_run_auto_touch);
report("dialogue.native.still_talk_layer_run_auto_touch",dg.still_talk_layer_run_auto_touch);
report("dialogue.native.get_auto_feed_mode",dg.get_auto_feed_mode);
report("dialogue.native.get_auto_feed_wait_time_letter",dg.get_auto_feed_wait_time_letter);
report("dialogue.native.get_auto_feed_wait_time_minimum",dg.get_auto_feed_wait_time_minimum);
report("dialogue.native.action_timer_value",dg.action_timer_value);
report("dialogue.native.talk_ui_process_wait",dg.talk_ui_process_wait);
report("dialogue.native.talk_layer_touch_handler",dg.talk_layer_touch_handler);
report("dialogue.native.rendering_checker",dg.rendering_checker);
printf("CHECK dialogue.layout_fields %d page_jump_unlocked_offset=%zu talk_ui_state_talk_layer_offset=%zu talk_layer_main_window_offset=%zu talk_main_window_rendering_flag_offset=%zu talk_main_window_ready_offset=%zu talk_main_window_started_offset=%zu talk_main_window_aux_offset=%zu\n",dg.page_jump_unlocked_offset!=0 && dg.talk_ui_state_talk_layer_offset!=0 && dg.talk_layer_main_window_offset!=0 && dg.talk_main_window_rendering_flag_offset!=0 && dg.talk_main_window_ready_offset!=0 && dg.talk_main_window_started_offset!=0 && dg.talk_main_window_aux_offset!=0,static_cast<size_t>(dg.page_jump_unlocked_offset),static_cast<size_t>(dg.talk_ui_state_talk_layer_offset),static_cast<size_t>(dg.talk_layer_main_window_offset),static_cast<size_t>(dg.talk_main_window_rendering_flag_offset),static_cast<size_t>(dg.talk_main_window_ready_offset),static_cast<size_t>(dg.talk_main_window_started_offset),static_cast<size_t>(dg.talk_main_window_aux_offset));
bool bindings_ok=audit_runtime_bindings(readable_ranges,ranges,appraisal_exchange_shop_purchase_addr);
bool models_ok=audit_resolver_models(readable_ranges,ranges);
bool unchanged=memcmp(before.data(),mem,raw.size())==0;
printf("READ_ONLY unchanged=%d\n",unchanged);
bool complete=item_ok&&injection_ok&&bindings_ok&&models_ok&&unchanged;
printf("AUDIT_COMPLETE ok=%d\n",complete);
free(mem);return complete?0:1;}
