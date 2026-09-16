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
#include <vector>
#include <string>
#include <unordered_map>
#include <sys/types.h>
#include "elf-abi.h"
#include "item_injection_contracts.h"
#include "item_injection_queue_test.h"
#include "item_catalog_signatures.h"
#define ALOGI(...) do { printf(__VA_ARGS__); puts(""); } while(0)
#define ALOGW(...) ALOGI(__VA_ARGS__)
struct MemoryRange {uintptr_t start,end;bool executable,writable;};
uintptr_t audit_base=0;size_t writes=0;std::vector<MemoryRange> audit_ranges;
constexpr const char *kTargetLib="libapp.so";
uintptr_t find_module_base(const char *){return audit_base;}
uintptr_t decode_adrp_ldr_global(uintptr_t,uintptr_t);
std::unordered_map<std::string,uintptr_t> audit_symbols;
void *resolve_app_symbol(const char *n){return (void*)audit_symbols[n];}
std::vector<MemoryRange> app_exec_ranges(){return audit_ranges;}
std::mutex g_token_purchase_patch_mutex;std::atomic<uintptr_t> g_token_purchase_owned_count_address{0};std::atomic<bool> g_token_purchase_owned_count_patch_enabled{false};
#define mass_trace_log(...) ALOGI(__VA_ARGS__)
std::mutex g_speed_patch_mutex;uintptr_t g_speed_patch_address=0;
std::mutex g_item_layout_mutex;std::atomic<bool> g_item_layout_ready{false};item_catalog::Layout g_item_layout{};
std::atomic<bool> g_object_layouts_ready{false};item_catalog::ObjectLayouts g_object_layouts{};
struct ItemNameRet {uint64_t w0,w1,w2;};
void append_item_dump_log(const char *kind,int,int,int,const char *d){printf("LAYOUT %s %s\n",kind,d);}
ssize_t vm_read_partial(uintptr_t a,void *p,size_t n){memcpy(p,(void*)a,n);return n;}
template<typename T> void for_each_snapshot_segment(const std::vector<MemoryRange>& rs,T visit){for(auto r:rs)visit(r.start,(const uint8_t*)r.start,r.end-r.start);}
bool patch_memory(uintptr_t a,const void *p,size_t n){++writes;printf("WRITE 0x%llx size=%zu\n",(unsigned long long)(a-audit_base),n);memcpy((void*)a,p,n);return true;}
struct RuntimeConfig {bool enabled=true,speedy=true,battle_mp=true,all_damage=true,dungeon_skip=true,team_god_mode=true,ad_bypass=true,encounter_freeze=true,encounter_force=false;};

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

bool encode_branch(uint32_t opcode, uintptr_t source, uintptr_t target, uint32_t *out) {
    int64_t delta = static_cast<int64_t>(target) - static_cast<int64_t>(source);
    if ((delta & 3) != 0) return false;
    int64_t imm26 = delta / 4;
    if (imm26 < -0x2000000LL || imm26 > 0x1FFFFFFLL) return false;
    *out = opcode | (static_cast<uint32_t>(imm26) & 0x03FFFFFFU);
    return true;
}

bool encode_cbz_w(uint32_t reg, uintptr_t source, uintptr_t target, uint32_t *out) {
    int64_t delta = static_cast<int64_t>(target) - static_cast<int64_t>(source);
    if ((delta & 3) != 0) return false;
    int64_t imm19 = delta / 4;
    if (imm19 < -0x40000LL || imm19 > 0x3FFFFLL) return false;
    *out = 0x34000000U | ((static_cast<uint32_t>(imm19) & 0x7FFFFU) << 5U) | (reg & 0x1FU);
    return true;
}

void append_u32(std::vector<uint8_t> &bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value & 0xFF));
    bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    bytes.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    bytes.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

std::vector<uintptr_t> find_pattern(const std::vector<MemoryRange>& rs,const uint8_t*p,size_t n){std::vector<uintptr_t> h;for(auto r:rs){auto b=(const uint8_t*)r.start,e=(const uint8_t*)r.end;while(b+n<=e){auto q=std::search(b,e,p,p+n);if(q==e)break;h.push_back((uintptr_t)q);b=q+1;}}return h;}
std::vector<uintptr_t> find_masked_pattern(const std::vector<MemoryRange>& rs,const uint8_t*p,const uint8_t*m,size_t n){std::vector<uintptr_t> h;for(auto r:rs)for(uintptr_t a=r.start;a+n<=r.end;++a)if((!m[0]||*(uint8_t*)a==p[0])&&memory_matches_mask(a,p,m,n))h.push_back(a);return h;}

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
struct BytePatch {
    const char *name;
    const uint8_t *original_full;
    size_t original_full_len;
    const uint8_t *patched_full;
    size_t patched_full_len;
    size_t patch_offset;
    const uint8_t *original_patch;
    const uint8_t *patched_patch;
    size_t patch_len;
    const uint8_t *original_full_mask = nullptr;
    const uint8_t *patched_full_mask = nullptr;
};
template<size_t N> bool read_item_contract(uintptr_t, const item_catalog::Word (&)[N], uint32_t (&)[N]);
template<size_t N> std::vector<uintptr_t> find_item_contract(const std::vector<MemoryRange> &, const item_catalog::Word (&)[N]);
template<size_t N> uint32_t item_role_word(const uint32_t (&)[N], const item_catalog::Word (&)[N], item_catalog::Role);
template<size_t N> uintptr_t scoped_layout_contract(const std::vector<MemoryRange> &, uintptr_t, const item_catalog::Word (&)[N], uint32_t (&)[N]);
uintptr_t find_function_start_before_ref(const std::vector<MemoryRange> &, uintptr_t);
void *resolve_app_symbol(const char *);


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
    std::vector<DialoguePageJumpCandidate> candidates = find_dialogue_page_jump_candidates(ranges);
    DialoguePageJumpCandidate selected{0, 0};
    size_t after_count = 0;
    if (talk_ui_process_wait_addr != 0) {
        for (const DialoguePageJumpCandidate &candidate: candidates) {
            if (candidate.address <= talk_ui_process_wait_addr) continue;
            ++after_count;
            if (selected.address == 0 ||
                candidate.address - talk_ui_process_wait_addr <
                selected.address - talk_ui_process_wait_addr) {
                selected = candidate;
            }
        }
    }
    if (selected.address == 0 && candidates.size() == 1) {
        selected = candidates[0];
    }
    if (selected.address == 0) {
        ALOGW("AE_TRACE dialogue pageJumpDelay skipped candidates=%zu afterTalkUi=%zu",
              candidates.size(),
              after_count);
        return false;
    }
    out->page_jump_delay = selected.address;
    out->page_jump_unlocked_offset = selected.unlocked_offset;
    ALOGI("AE_TRACE dialogue pageJumpDelay resolved addr=0x%" PRIxPTR
          " unlockedOffset=0x%" PRIxPTR " candidates=%zu afterTalkUi=%zu",
          selected.address,
          selected.unlocked_offset,
          candidates.size(),
          after_count);
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
            uintptr_t adjacent = 0;
            if (!read_range_ptr(readable_ranges, ref + sizeof(uintptr_t), &adjacent)) continue;

            // Current builds expose both direct Lua registration rows:
            //   { "setGlobalFlag", function_ptr }
            // and generated loader rows:
            //   { "setGlobalFlag", &direct_row.function_ptr, ... }.
            // Resolving through the adjacent slot handles both layouts and avoids
            // assuming a fragile fixed +24 byte offset from the string reference.
            if (range_contains(exec_ranges, adjacent, 4)) {
                ++direct_refs;
                push_unique(hits, adjacent);
                continue;
            }

            uintptr_t indirect = 0;
            if (read_range_ptr(readable_ranges, adjacent, &indirect) &&
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


void apply_token_purchase_owned_count_zero_patch(bool enable, const char *reason) {
    static constexpr uint32_t patched[] = {0x52800000, 0xD503201F, 0xD503201F};
    static uintptr_t owned_address = 0;
    static uint32_t original[3]{};
    std::lock_guard<std::mutex> lock(g_token_purchase_patch_mutex);
    const uintptr_t address = g_token_purchase_owned_count_address.load(std::memory_order_acquire);
    if (address == 0 || !range_contains(app_exec_ranges(), address, sizeof(original))) return;
    if (owned_address == 0) {
        if (!enable) return;
        uint32_t words[std::size(item_catalog::owned_count_read)]{};
        if (!read_item_contract(address, item_catalog::owned_count_read, words)) return;
        std::copy_n(words, std::size(original), original);
        owned_address = address;
    }
    if (address != owned_address) return;
    const bool is_original = memory_equals(address, reinterpret_cast<const uint8_t *>(original), sizeof(original));
    const bool is_patched = memory_equals(address, reinterpret_cast<const uint8_t *>(patched), sizeof(patched));
    if (!is_original && !is_patched) {
        mass_trace_log("AE_MPTRACE TOKEN_PURCHASE_OWNED_COUNT_PATCH trigger=%s enable=%d ok=0 reason=unexpected-bytes",
                       reason == nullptr ? "unknown" : reason, enable ? 1 : 0);
        return;
    }
    if ((enable && is_patched) || (!enable && is_original)) {
        g_token_purchase_owned_count_patch_enabled.store(enable, std::memory_order_relaxed);
        return;
    }
    const bool ok = patch_memory(address, reinterpret_cast<const uint8_t *>(enable ? patched : original), sizeof(original));
    if (ok) g_token_purchase_owned_count_patch_enabled.store(enable, std::memory_order_relaxed);
    mass_trace_log("AE_MPTRACE TOKEN_PURCHASE_OWNED_COUNT_PATCH trigger=%s enable=%d addr=0x%" PRIxPTR " ok=%d",
                   reason == nullptr ? "unknown" : reason, enable ? 1 : 0, address, ok ? 1 : 0);
}

bool apply_byte_patch(const std::vector<MemoryRange> &ranges, const BytePatch &patch, bool enable) {
    struct OwnedPatch { uintptr_t address; std::vector<uint8_t> original; };
    static std::mutex mutex;
    static std::unordered_map<std::string, OwnedPatch> owned;
    std::lock_guard<std::mutex> lock(mutex);
    const auto original_hits = find_code_pattern(ranges, patch.original_full, patch.original_full_len, patch.original_full_mask);
    const auto patched_hits = find_code_pattern(ranges, patch.patched_full, patch.patched_full_len, patch.patched_full_mask);
    if (original_hits.size() + patched_hits.size() != 1) {
        ALOGW("AE_TRACE byte patch %s skipped originalHits=%zu patchedHits=%zu", patch.name, original_hits.size(), patched_hits.size());
        return false;
    }
    const bool original = !original_hits.empty();
    const uintptr_t address = (original ? original_hits[0] : patched_hits[0]) + patch.patch_offset;
    if (!range_contains(ranges, address, patch.patch_len)) return false;
    auto found = owned.find(patch.name);
    if (original) {
        if (!enable) return true;
        if (found == owned.end()) {
            std::vector<uint8_t> bytes(patch.patch_len);
            if (vm_read_partial(address, bytes.data(), bytes.size()) != static_cast<ssize_t>(bytes.size())) return false;
            found = owned.emplace(patch.name, OwnedPatch{address, std::move(bytes)}).first;
        }
        if (found->second.address != address || !memory_equals(address, found->second.original.data(), patch.patch_len)) return false;
    } else {
        // No guessed undo instruction: only restore the bytes captured by this process.
        if (found == owned.end() || found->second.address != address ||
            !memory_equals(address, patch.patched_patch, patch.patch_len)) return false;
        if (enable) return true;
    }
    const uint8_t *replacement = enable ? patch.patched_patch : found->second.original.data();
    const bool ok = patch_memory(address, replacement, patch.patch_len);
    ALOGI("AE_TRACE byte patch %s enable=%d addr=0x%" PRIxPTR " ok=%d", patch.name, enable ? 1 : 0, address, ok ? 1 : 0);
    return ok;
}

bool apply_present_byte_patch(const std::vector<MemoryRange> &ranges, const BytePatch *variants, size_t n, bool enable) {
    const BytePatch *selected = nullptr;
    for (size_t i = 0; i < n; ++i) {
        const BytePatch &p = variants[i];
        auto src = find_code_pattern(ranges, p.original_full, p.original_full_len, p.original_full_mask);
        auto dst = find_code_pattern(ranges, p.patched_full, p.patched_full_len, p.patched_full_mask);
        if (src.empty() && dst.empty()) continue;
        if (selected != nullptr || src.size() + dst.size() != 1) {
            ALOGW("AE_TRACE byte patch variants ambiguous; no bytes changed");
            return false;
        }
        selected = &p;
    }
    if (selected != nullptr) return apply_byte_patch(ranges, *selected, enable);
    if (n > 0) ALOGW("AE_TRACE byte patch %s skipped: no variant signature present", variants[0].name);
    return false;
}

void apply_team_god_patch(const std::vector<MemoryRange> &ranges, bool enable) {
    static constexpr uint8_t branch_original[] = {0x68, 0x02, 0x40, 0xF9};
    static constexpr uint8_t cave_prefix[] = {
            0x66, 0xC8, 0x66, 0xC8, 0x66, 0xC8, 0x66, 0xC8,
            0x66, 0xC8, 0x66, 0xC8, 0x66, 0xC8, 0x66, 0xC8,
            0x04, 0x22, 0x04, 0x22, 0x04, 0x22, 0x04, 0x22,
            0x04, 0x22, 0x04, 0x22, 0x04, 0x22, 0x04, 0x22,
            0xEB, 0xBA, 0xEB, 0xBA, 0xEB, 0xBA, 0xEB, 0xBA,
            0xEB, 0xBA, 0xEB, 0xBA, 0xEB, 0xBA, 0xEB, 0xBA
    };
    static constexpr uint8_t cave_suffix[] = {
            0xFF, 0x83, 0x06, 0xD1, 0xE8, 0x03, 0x03, 0xAA,
            0xAC, 0x50, 0x89, 0x52, 0xFD, 0x7B, 0xBA, 0xA9,
            0xFD, 0x03, 0x00, 0x91
    };
    using namespace item_catalog;
    auto tail_hits = find_item_contract(ranges, team_branch_tail);
    if (tail_hits.size() != 1 || tail_hits[0] < sizeof(uint32_t)) return;
    uintptr_t branch_address = tail_hits[0] - sizeof(uint32_t);
    uint32_t tail_words[std::size(team_branch_tail)]{};
    uint32_t type_words[std::size(team_actor_type)]{};
    if (!read_item_contract(tail_hits[0], team_branch_tail, tail_words) ||
        !scoped_layout_contract(ranges, tail_hits[0], team_actor_type, type_words)) return;

    struct Cave { uintptr_t address; size_t size; };
    std::vector<Cave> caves;
    auto prefixes = find_pattern(ranges, cave_prefix, sizeof(cave_prefix));
    auto suffixes = find_pattern(ranges, cave_suffix, sizeof(cave_suffix));
    for (uintptr_t prefix: prefixes) {
        uintptr_t begin = prefix + sizeof(cave_prefix);
        for (uintptr_t suffix: suffixes) {
            if (suffix > begin && suffix - begin <= 512 && (begin & 3U) == 0 &&
                ((suffix - begin) & 3U) == 0 && range_contains(ranges, begin, suffix - begin)) {
                caves.push_back({begin, suffix - begin});
            }
        }
    }
    if (caves.size() != 1) return;
    uintptr_t cave_address = caves[0].address;
    size_t cave_size = caves[0].size;
    const uintptr_t hp_callsite = tail_hits[0] + sizeof(tail_words);
    uintptr_t hp_target = 0;
    if (!range_contains(ranges, hp_callsite, sizeof(uint32_t)) ||
        !decode_branch_target(hp_callsite, true, &hp_target)) return;

    std::vector<uint8_t> cave_patch;
    append_u32(cave_patch, item_role_word(tail_words, team_branch_tail, Role::saved_value));
    cave_patch.insert(cave_patch.end(), branch_original, branch_original + sizeof(branch_original));
    append_u32(cave_patch, item_role_word(tail_words, team_branch_tail, Role::self_argument));
    append_u32(cave_patch, item_role_word(type_words, team_actor_type, Role::actor_type_slot));
    append_u32(cave_patch, item_role_word(tail_words, team_branch_tail, Role::virtual_call));
    append_u32(cave_patch, 0x7100041FU);  // cmp w0, #1 (player actor kind)
    size_t skip_offset = cave_patch.size();
    append_u32(cave_patch, 0);  // generated EQ branch to the return branch
    cave_patch.insert(cave_patch.end(), branch_original, branch_original + sizeof(branch_original));
    for (size_t i = 0; i < std::size(team_branch_tail); ++i) {
        if (team_branch_tail[i].role != Role::saved_value) append_u32(cave_patch, tail_words[i]);
    }
    uint32_t instruction = 0;
    if (!encode_branch(0x94000000U, cave_address + cave_patch.size(), hp_target, &instruction)) return;
    append_u32(cave_patch, instruction);
    size_t return_offset = cave_patch.size();
    if (!encode_branch(0x14000000U, cave_address + return_offset,
                       hp_callsite + sizeof(uint32_t), &instruction)) return;
    append_u32(cave_patch, instruction);
    uint32_t skip = 0x54000000U | (static_cast<uint32_t>((return_offset - skip_offset) / 4U) << 5U);
    std::memcpy(cave_patch.data() + skip_offset, &skip, sizeof(skip));
    if (cave_patch.size() > cave_size) return;
    cave_patch.resize(cave_size, 0);
    uint32_t branch_patch = 0;
    if (!encode_branch(0x14000000U, branch_address, cave_address, &branch_patch)) return;
    std::vector<uint8_t> empty_cave(cave_size, 0);
    bool original = memory_equals(branch_address, branch_original, sizeof(branch_original)) &&
                    memory_equals(cave_address, empty_cave.data(), empty_cave.size());
    bool patched = memory_equals(branch_address, reinterpret_cast<const uint8_t *>(&branch_patch), sizeof(branch_patch)) &&
                   memory_equals(cave_address, cave_patch.data(), cave_patch.size());
    bool ok = true;
    if (enable && original) {
        ok = patch_memory(cave_address, cave_patch.data(), cave_patch.size()) &&
             patch_memory(branch_address, reinterpret_cast<const uint8_t *>(&branch_patch), sizeof(branch_patch));
    } else if (!enable && patched) {
        ok = patch_memory(branch_address, branch_original, sizeof(branch_original)) &&
             patch_memory(cave_address, empty_cave.data(), empty_cave.size());
    } else if (!(enable ? patched : original)) {
        ALOGW("AE_TRACE team god unknown/mixed state; no bytes changed");
        return;
    }
    ALOGI("AE_TRACE team god enable=%d cave=0x%" PRIxPTR " branch=0x%" PRIxPTR
          " modelSlot=%zu actorTypeSlot=%zu ok=%d", enable, cave_address, branch_address,
          unsigned_offset(item_role_word(tail_words, team_branch_tail, Role::hp_model_slot), 3),
          unsigned_offset(item_role_word(type_words, team_actor_type, Role::actor_type_slot), 3), ok);
}

void apply_ad_bypass_patch(const std::vector<MemoryRange> &ranges, bool enable) {
    static constexpr uint8_t availability_return_original[] = {
            0x60, 0x02, 0x00, 0x12, 0xF4, 0x4F, 0x45, 0xA9,
            0xFD, 0x7B, 0x44, 0xA9, 0xFF, 0x83, 0x01, 0x91,
            0xC0, 0x03, 0x5F, 0xD6
    };
    static constexpr uint8_t availability_return_patched[] = {
            0x20, 0x00, 0x80, 0x52, 0xF4, 0x4F, 0x45, 0xA9,
            0xFD, 0x7B, 0x44, 0xA9, 0xFF, 0x83, 0x01, 0x91,
            0xC0, 0x03, 0x5F, 0xD6
    };
    static constexpr uint8_t availability_original[] = {0x60, 0x02, 0x00, 0x12};
    static constexpr uint8_t availability_patch[] = {0x20, 0x00, 0x80, 0x52};
    static constexpr uint8_t availability_wrapper_prologue[] = {
            0xFF, 0x83, 0x01, 0xD1, 0xFD, 0x7B, 0x04, 0xA9,
            0xF4, 0x4F, 0x05, 0xA9, 0xFD, 0x03, 0x01, 0x91,
            0x54, 0xD0, 0x3B, 0xD5
    };
    // Stable return epilogue; its call targets are decoded below.
    static constexpr uint8_t sdk_show_epilogue[] = {
            0x60, 0x02, 0x00, 0x12, 0xF4, 0x4F, 0x42, 0xA9,
            0xF5, 0x0B, 0x40, 0xF9, 0xFD, 0x7B, 0xC3, 0xA8,
            0xC0, 0x03, 0x5F, 0xD6
    };
    static constexpr uint8_t sdk_show_wrapper_prologue[] = {
            0xFF, 0x83, 0x01, 0xD1, 0xFD, 0x7B, 0x04, 0xA9
    };
    static constexpr uint8_t sdk_show_call_patch[] = {0x1F, 0x20, 0x03, 0xD5};
    static constexpr uint8_t cave_prefix[] = {
            0x66, 0xC8, 0x66, 0xC8, 0x66, 0xC8, 0x66, 0xC8,
            0x66, 0xC8, 0x66, 0xC8, 0x66, 0xC8, 0x66, 0xC8,
            0x04, 0x22, 0x04, 0x22, 0x04, 0x22, 0x04, 0x22,
            0x04, 0x22, 0x04, 0x22, 0x04, 0x22, 0x04, 0x22,
            0xEB, 0xBA, 0xEB, 0xBA, 0xEB, 0xBA, 0xEB, 0xBA,
            0xEB, 0xBA, 0xEB, 0xBA, 0xEB, 0xBA, 0xEB, 0xBA
    };
    static constexpr uint8_t cave_suffix_head[] = {0xFD, 0x7B, 0xA3, 0xA9};
    static constexpr uint8_t cave_suffix_tail[] = {
            0xFD, 0x03, 0x00, 0x91, 0xE8, 0x27, 0x06, 0x6D
    };
    // Pair each unique SDK call site with its actual availability call. No
    // inter-call distance is assumed, including when the show call is already NOP'd.
    struct SdkSite { uintptr_t site, function, availability; };
    std::vector<SdkSite> sdk_sites;
    for (uintptr_t epi: find_pattern(ranges, sdk_show_epilogue, sizeof(sdk_show_epilogue))) {
        if (epi < sizeof(uint32_t)) continue;
        uintptr_t site = epi - sizeof(uint32_t), start = find_function_start_before_ref(ranges, site);
        if (start == 0 || !range_contains(ranges, site, sizeof(uint32_t))) continue;
        uint32_t word = read_u32(site);
        if (word != 0xD503201FU) {
            uintptr_t show = 0;
            if (!decode_branch_target(site, true, &show) ||
                !range_contains(ranges, show, sizeof(sdk_show_wrapper_prologue)) ||
                !memory_equals(show, sdk_show_wrapper_prologue, sizeof(sdk_show_wrapper_prologue))) continue;
        }
        std::vector<uintptr_t> availability;
        for (uintptr_t pc = start; pc < site; pc += sizeof(uint32_t)) {
            uintptr_t target = 0;
            if (decode_branch_target(pc, true, &target) &&
                range_contains(ranges, target, sizeof(availability_wrapper_prologue)) &&
                memory_equals(target, availability_wrapper_prologue, sizeof(availability_wrapper_prologue))) {
                push_unique(availability, target);
            }
        }
        if (availability.size() == 1) sdk_sites.push_back({site, start, availability[0]});
    }
    if (sdk_sites.size() != 1) { ALOGW("AE_TRACE ad resolver SDK sites=%zu", sdk_sites.size()); return; }
    const auto sdk = sdk_sites[0];

    std::vector<uintptr_t> state_store_hits;
    for (uintptr_t hit: find_item_contract(ranges, item_catalog::ad_state)) {
        uintptr_t site = 0;
        uintptr_t branch = item_role_address(hit, item_catalog::ad_state, item_catalog::Role::ad_state_branch);
        if (!decode_branch_target(branch, false, &site) || site < hit + std::size(item_catalog::ad_state) * sizeof(uint32_t) ||
            site > hit + 64 || !range_contains(ranges, site, sizeof(uint32_t))) continue;
        bool padding_ok = true;
        for (uintptr_t pc = hit + std::size(item_catalog::ad_state) * sizeof(uint32_t); pc < site; pc += 4) {
            if (read_u32(pc) != 0xD503201FU) padding_ok = false;
        }
        bool calls_sdk = false;
        uintptr_t lo = hit >= 512 ? hit - 512 : 0;
        for (uintptr_t pc = hit; pc >= lo; pc -= sizeof(uint32_t)) {
            if (!range_contains(ranges, pc, sizeof(uint32_t))) break;
            uintptr_t target = 0;
            if (decode_branch_target(pc, true, &target) && target == sdk.function) calls_sdk = true;
            if (pc == lo) break;
        }
        if (padding_ok && calls_sdk) push_unique(state_store_hits, site);
    }

    struct Cave { uintptr_t address; size_t size; };
    std::vector<Cave> caves;
    for (uintptr_t hit: find_pattern(ranges, cave_prefix, sizeof(cave_prefix))) {
        uintptr_t start = hit + sizeof(cave_prefix);
        for (size_t length = sizeof(uint32_t); length <= 512; length += sizeof(uint32_t)) {
            uintptr_t suffix = start + length;
            if (!range_contains(ranges, suffix, sizeof(cave_suffix_head) + sizeof(uint32_t) + sizeof(cave_suffix_tail))) break;
            if (memory_equals(suffix, cave_suffix_head, sizeof(cave_suffix_head)) &&
                (read_u32(suffix + sizeof(cave_suffix_head)) & 0x9F00001FU) == 0x90000009U &&
                memory_equals(suffix + sizeof(cave_suffix_head) + sizeof(uint32_t), cave_suffix_tail, sizeof(cave_suffix_tail))) {
                caves.push_back({start, length});
            }
        }
    }

    std::vector<uintptr_t> availability_return_hits;
    for (uintptr_t pc = sdk.availability; pc < sdk.availability + 512; pc += sizeof(uint32_t)) {
        if (!range_contains(ranges, pc, sizeof(availability_return_original))) break;
        if (memory_equals(pc, availability_return_original, sizeof(availability_return_original)) ||
            memory_equals(pc, availability_return_patched, sizeof(availability_return_patched))) {
            push_unique(availability_return_hits, pc);
        }
        if (read_u32(pc) == 0xD65F03C0U) break;
    }
    if (state_store_hits.size() != 1 || caves.size() != 1 || availability_return_hits.size() != 1) { ALOGW("AE_TRACE ad resolver state=%zu caves=%zu availability=%zu", state_store_hits.size(), caves.size(), availability_return_hits.size()); return; }

    // Resolve each JNI bridge by its exported identity, then validate its complete
    // supported argument/callback contract. Never derive another function by distance.
    uintptr_t opened_bridge_address = reinterpret_cast<uintptr_t>(resolve_app_symbol(
        "Java_net_wrightflyer_toybox_IronSourceEvents_onRewardedVideoAdOpened"));
    uintptr_t closed_bridge_address = reinterpret_cast<uintptr_t>(resolve_app_symbol(
        "Java_net_wrightflyer_toybox_IronSourceEvents_onRewardedVideoAdClosed"));
    uintptr_t availability_bridge = reinterpret_cast<uintptr_t>(resolve_app_symbol(
        "Java_net_wrightflyer_toybox_IronSourceEvents_onRewardedVideoAvailabilityChanged"));
    uintptr_t reward_bridge = reinterpret_cast<uintptr_t>(resolve_app_symbol(
        "Java_net_wrightflyer_toybox_IronSourceEvents_onRewardedVideoAdRewarded"));
    uintptr_t receiver = bridge_receiver_global(ranges, opened_bridge_address, item_catalog::bridge_simple);
    uintptr_t reward_receiver = bridge_receiver_global(ranges, reward_bridge, item_catalog::bridge_reward);
    uintptr_t legacy_receiver = bridge_receiver_global(ranges, reward_bridge, item_catalog::bridge_reward_legacy);
    if (receiver == 0 || receiver != bridge_receiver_global(ranges, closed_bridge_address, item_catalog::bridge_simple) ||
        receiver != bridge_receiver_global(ranges, availability_bridge, item_catalog::bridge_available) ||
        (reward_receiver != 0) == (legacy_receiver != 0) || receiver != (reward_receiver != 0 ? reward_receiver : legacy_receiver)) { ALOGW("AE_TRACE ad resolver bridge contracts unavailable"); return; }
    std::vector<uintptr_t> reward_hits{reward_bridge}, availability_hits{availability_bridge};

    uintptr_t availability_address = availability_return_hits[0];
    uintptr_t sdk_show_address = sdk.site;
    uintptr_t state_store_address = state_store_hits[0];
    uintptr_t cave_address = caves[0].address;
    size_t cave_size = caves[0].size;
    uint32_t state_word = read_u32(state_store_address);
    // The original field store is copied from the matched game instruction; on the
    // ON form it is the first instruction of our fully verified cave program.
    if ((state_word & 0xFFC003FFU) != 0x39000268U) state_word = read_u32(cave_address);
    if ((state_word & 0xFFC003FFU) != 0x39000268U) return;
    uint8_t state_store_original[sizeof(state_word)];
    std::memcpy(state_store_original, &state_word, sizeof(state_word));

    // The original SDK-show BL is version-specific (its branch offset re-encodes every build), so
    // cache it the first time we see the site unpatched; the disable/undo path restores these
    // exact bytes instead of a hard-coded literal. In-memory patches do not persist across game
    // launches, so every process starts unpatched and populates this before any toggle-off.
    static uint32_t g_ad_sdk_show_original_word = 0;
    uint32_t sdk_show_word = read_u32(sdk_show_address);
    if ((sdk_show_word & 0xFC000000U) == 0x94000000U) {
        g_ad_sdk_show_original_word = sdk_show_word;
    }

    std::vector<uint8_t> cave_patch(state_store_original, state_store_original + sizeof(state_store_original));
    size_t cbz_offset = cave_patch.size();
    append_u32(cave_patch, 0);
    static constexpr uint8_t frame_setup[] = {
            0xFD, 0x7B, 0xBF, 0xA9, 0xFD, 0x03, 0x00, 0x91
    };
    cave_patch.insert(cave_patch.end(), frame_setup, frame_setup + sizeof(frame_setup));

    uint32_t instruction = 0;
    if (!encode_branch(0x94000000U, cave_address + cave_patch.size(), opened_bridge_address, &instruction)) {
        ALOGW("AE_TRACE ad bypass skipped: opened bridge encode failed");
        return;
    }
    append_u32(cave_patch, instruction);

    static constexpr uint8_t mov_x2_null[] = {0xE2, 0x03, 0x1F, 0xAA};
    cave_patch.insert(cave_patch.end(), mov_x2_null, mov_x2_null + sizeof(mov_x2_null));

    if (!encode_branch(0x94000000U, cave_address + cave_patch.size(), reward_hits[0], &instruction)) {
        ALOGW("AE_TRACE ad bypass skipped: reward bridge encode failed");
        return;
    }
    append_u32(cave_patch, instruction);

    if (!encode_branch(0x94000000U, cave_address + cave_patch.size(), closed_bridge_address, &instruction)) {
        ALOGW("AE_TRACE ad bypass skipped: closed bridge encode failed");
        return;
    }
    append_u32(cave_patch, instruction);

    static constexpr uint8_t mov_w2_true[] = {0x22, 0x00, 0x80, 0x52};
    cave_patch.insert(cave_patch.end(), mov_w2_true, mov_w2_true + sizeof(mov_w2_true));

    if (!encode_branch(0x94000000U, cave_address + cave_patch.size(), availability_hits[0], &instruction)) {
        ALOGW("AE_TRACE ad bypass skipped: availability bridge encode failed");
        return;
    }
    append_u32(cave_patch, instruction);

    static constexpr uint8_t frame_restore[] = {0xFD, 0x7B, 0xC1, 0xA8};
    cave_patch.insert(cave_patch.end(), frame_restore, frame_restore + sizeof(frame_restore));

    uint32_t return_branch = 0;
    if (!encode_branch(0x14000000U, cave_address + cave_patch.size(), state_store_address + 4, &return_branch)) {
        ALOGW("AE_TRACE ad bypass skipped: return branch encode failed");
        return;
    }

    uint32_t cbz = 0;
    if (!encode_cbz_w(8, cave_address + cbz_offset, cave_address + cave_patch.size(), &cbz)) {
        ALOGW("AE_TRACE ad bypass skipped: state-active guard encode failed");
        return;
    }
    cave_patch[cbz_offset] = static_cast<uint8_t>(cbz & 0xFF);
    cave_patch[cbz_offset + 1] = static_cast<uint8_t>((cbz >> 8) & 0xFF);
    cave_patch[cbz_offset + 2] = static_cast<uint8_t>((cbz >> 16) & 0xFF);
    cave_patch[cbz_offset + 3] = static_cast<uint8_t>((cbz >> 24) & 0xFF);
    append_u32(cave_patch, return_branch);
    if (cave_patch.size() > cave_size) return;
    cave_patch.resize(cave_size, 0);

    uint32_t state_branch = 0;
    if (!encode_branch(0x14000000U, state_store_address, cave_address, &state_branch)) {
        ALOGW("AE_TRACE ad bypass skipped: state trampoline branch encode failed");
        return;
    }
    uint8_t state_store_patch[4] = {
            static_cast<uint8_t>(state_branch & 0xFF),
            static_cast<uint8_t>((state_branch >> 8) & 0xFF),
            static_cast<uint8_t>((state_branch >> 16) & 0xFF),
            static_cast<uint8_t>((state_branch >> 24) & 0xFF),
    };
    std::vector<uint8_t> cave_zero(cave_size, 0);

    bool availability_is_original = memory_equals(availability_address, availability_original, sizeof(availability_original));
    bool availability_is_patched = memory_equals(availability_address, availability_patch, sizeof(availability_patch));
    bool sdk_is_original = (sdk_show_word & 0xFC000000U) == 0x94000000U;  // a BL (any offset)
    bool sdk_is_patched = sdk_show_word == 0xD503201FU;                    // NOP
    bool state_is_original = memory_equals(state_store_address, state_store_original, sizeof(state_store_original));
    bool state_is_patched = memory_equals(state_store_address, state_store_patch, sizeof(state_store_patch));
    bool cave_is_original = memory_equals(cave_address, cave_zero.data(), cave_zero.size());
    bool cave_is_patched = memory_equals(cave_address, cave_patch.data(), cave_patch.size());

    if (enable) {
        if (availability_is_patched && sdk_is_patched && state_is_patched && cave_is_patched) {
            return;
        }
        if (!(availability_is_original && sdk_is_original && state_is_original && cave_is_original)) {
            ALOGW("AE_TRACE ad bypass partial state; no bytes changed");
            return;
        }

        bool cave_ok = patch_memory(cave_address, cave_patch.data(), cave_patch.size());
        bool sdk_ok = cave_ok && patch_memory(sdk_show_address, sdk_show_call_patch, sizeof(sdk_show_call_patch));
        bool availability_ok = sdk_ok && patch_memory(availability_address, availability_patch, sizeof(availability_patch));
        bool state_ok = availability_ok && patch_memory(state_store_address, state_store_patch, sizeof(state_store_patch));
        ALOGI("AE_TRACE ad bypass enable availability=0x%" PRIxPTR " sdk=0x%" PRIxPTR
              " state=0x%" PRIxPTR " cave=0x%" PRIxPTR " ok=%d",
              availability_address,
              sdk_show_address,
              state_store_address,
              cave_address,
              state_ok ? 1 : 0);
    } else {
        if (availability_is_original && sdk_is_original && state_is_original && cave_is_original) {
            return;
        }
        if (!(availability_is_patched && sdk_is_patched && state_is_patched && cave_is_patched)) {
            ALOGW("AE_TRACE ad bypass partial state while disabling; no bytes changed");
            return;
        }

        bool state_ok = patch_memory(state_store_address, state_store_original, sizeof(state_store_original));
        uint8_t sdk_restore[4] = {
                static_cast<uint8_t>(g_ad_sdk_show_original_word & 0xFF),
                static_cast<uint8_t>((g_ad_sdk_show_original_word >> 8) & 0xFF),
                static_cast<uint8_t>((g_ad_sdk_show_original_word >> 16) & 0xFF),
                static_cast<uint8_t>((g_ad_sdk_show_original_word >> 24) & 0xFF),
        };
        bool sdk_ok = state_ok && g_ad_sdk_show_original_word != 0 &&
                      patch_memory(sdk_show_address, sdk_restore, sizeof(sdk_restore));
        bool availability_ok = sdk_ok && patch_memory(availability_address, availability_original, sizeof(availability_original));
        bool cave_ok = availability_ok && patch_memory(cave_address, cave_zero.data(), cave_zero.size());
        ALOGI("AE_TRACE ad bypass disable availability=0x%" PRIxPTR " sdk=0x%" PRIxPTR
              " state=0x%" PRIxPTR " cave=0x%" PRIxPTR " ok=%d",
              availability_address,
              sdk_show_address,
              state_store_address,
              cave_address,
              cave_ok ? 1 : 0);
    }
}

bool patch_speed_constants(const std::vector<MemoryRange> &ranges, bool enable) {
    std::lock_guard<std::mutex> lock(g_speed_patch_mutex);
    if (!enable && g_speed_patch_address == 0) return true;
    const float original = 1000000.0f, patched = 31250.0f;
    const auto *original_bytes = reinterpret_cast<const uint8_t *>(&original);
    const auto *patched_bytes = reinterpret_cast<const uint8_t *>(&patched);
    if (g_speed_patch_address == 0) {
        auto hits = find_pattern(ranges, original_bytes, sizeof(original));
        hits.erase(std::remove_if(hits.begin(), hits.end(), [](uintptr_t address) {
            return address % alignof(float) != 0;
        }), hits.end());
        if (hits.size() != 1) {
            ALOGW("AE_TRACE speed constant skipped: expected one aligned original, hits=%zu", hits.size());
            return false;
        }
        g_speed_patch_address = hits[0];
    }
    uintptr_t address = g_speed_patch_address;
    if (!range_contains(ranges, address, sizeof(float))) return false;
    bool is_original = memory_equals(address, original_bytes, sizeof(original));
    bool is_patched = memory_equals(address, patched_bytes, sizeof(patched));
    if (!is_original && !is_patched) {
        ALOGW("AE_TRACE speed constant changed by another writer; preserving bytes");
        return false;
    }
    bool ok = enable ? (is_patched || patch_memory(address, patched_bytes, sizeof(patched)))
                     : (is_original || patch_memory(address, original_bytes, sizeof(original)));
    // Undo never scans for the replacement value: it may already exist elsewhere.
    // Keep ownership on failure so a later request can retry the exact same site.
    if (ok && !enable) g_speed_patch_address = 0;
    ALOGI("AE_TRACE speed constant enable=%d addr=0x%" PRIxPTR " ok=%d", enable, address, ok);
    return ok;
}

void apply_encounter_judge_patch(const std::vector<MemoryRange> &ranges, bool freeze, bool force) {
    static constexpr uint8_t original_full[] = {
            0xFF, 0x03, 0x02, 0xD1, 0xFD, 0x7B, 0x05, 0xA9,
            0xF6, 0x57, 0x06, 0xA9, 0xF4, 0x4F, 0x07, 0xA9,
            0xFD, 0x43, 0x01, 0x91, 0x55, 0xD0, 0x3B, 0xD5,
            0xA8, 0x16, 0x40, 0xF9, 0xA8, 0x83, 0x1F, 0xF8,
            0x00, 0x70, 0x40, 0xBD, 0x08, 0x20, 0x20, 0x1E,
            0xA9, 0x04, 0x00, 0x54, 0xF3, 0x03, 0x00, 0xAA
    };
    static constexpr uint8_t freeze_full[] = {
            0x00, 0x00, 0x80, 0x52, 0xC0, 0x03, 0x5F, 0xD6,
            0xF6, 0x57, 0x06, 0xA9, 0xF4, 0x4F, 0x07, 0xA9,
            0xFD, 0x43, 0x01, 0x91, 0x55, 0xD0, 0x3B, 0xD5,
            0xA8, 0x16, 0x40, 0xF9, 0xA8, 0x83, 0x1F, 0xF8,
            0x00, 0x70, 0x40, 0xBD, 0x08, 0x20, 0x20, 0x1E,
            0xA9, 0x04, 0x00, 0x54, 0xF3, 0x03, 0x00, 0xAA
    };
    static constexpr uint8_t force_full[] = {
            0x20, 0x00, 0x80, 0x52, 0xC0, 0x03, 0x5F, 0xD6,
            0xF6, 0x57, 0x06, 0xA9, 0xF4, 0x4F, 0x07, 0xA9,
            0xFD, 0x43, 0x01, 0x91, 0x55, 0xD0, 0x3B, 0xD5,
            0xA8, 0x16, 0x40, 0xF9, 0xA8, 0x83, 0x1F, 0xF8,
            0x00, 0x70, 0x40, 0xBD, 0x08, 0x20, 0x20, 0x1E,
            0xA9, 0x04, 0x00, 0x54, 0xF3, 0x03, 0x00, 0xAA
    };
    static constexpr uint8_t return_false[] = {
            0x00, 0x00, 0x80, 0x52, 0xC0, 0x03, 0x5F, 0xD6
    };
    static constexpr uint8_t return_true[] = {
            0x20, 0x00, 0x80, 0x52, 0xC0, 0x03, 0x5F, 0xD6
    };
    static constexpr uint8_t original_head[] = {
            0xFF, 0x03, 0x02, 0xD1, 0xFD, 0x7B, 0x05, 0xA9
    };

    enum Mode {
        Original = 0,
        Freeze = 1,
        Force = 2,
    };

    Mode desired = Original;
    if (freeze) {
        desired = Freeze;
    } else if (force) {
        desired = Force;
    }

    std::vector<uintptr_t> hits;
    Mode current = Original;
    for (uintptr_t hit: find_code_pattern(ranges, original_full, sizeof(original_full))) {
        push_unique(hits, hit);
        current = Original;
    }
    for (uintptr_t hit: find_code_pattern(ranges, freeze_full, sizeof(freeze_full))) {
        push_unique(hits, hit);
        current = Freeze;
    }
    for (uintptr_t hit: find_code_pattern(ranges, force_full, sizeof(force_full))) {
        push_unique(hits, hit);
        current = Force;
    }

    if (hits.empty()) {
        if (desired != Original) {
            ALOGW("AE_TRACE encounter judge patch skipped sourceHits=0 desired=%d", desired);
        }
        return;
    }
    if (hits.size() != 1) {
        ALOGW("AE_TRACE encounter judge patch skipped hits=%zu desired=%d", hits.size(), desired);
        return;
    }
    if (current == desired) return;

    const uint8_t *replacement = original_head;
    if (desired == Freeze) {
        replacement = return_false;
    } else if (desired == Force) {
        replacement = return_true;
    }

    bool ok = patch_memory(hits[0], replacement, sizeof(original_head));
    (void) ok;
    ALOGI("AE_TRACE encounter judge patch desired=%d previous=%d addr=0x%" PRIxPTR " ok=%d",
          desired,
          current,
          hits[0],
          ok ? 1 : 0);
}

void apply_runtime_byte_patches(const RuntimeConfig &cfg) {
    std::vector<MemoryRange> ranges = app_exec_ranges();
    if (ranges.empty()) return;

    static constexpr uint8_t mp_cost_orig[] = {
            0x08, 0x20, 0x40, 0x39, 0x68, 0x01, 0x00, 0x34, 0xF3, 0x03, 0x00, 0xAA, 0x00, 0x00, 0x40, 0xF9,
            0xF4, 0x03, 0x01, 0xAA, 0x08, 0x00, 0x40, 0xF9, 0x08, 0x31, 0x40, 0xF9, 0x00, 0x01, 0x3F, 0xD6
    };
    static constexpr uint8_t mp_cost_patch[] = {
            0xE8, 0x03, 0x1F, 0x2A, 0x68, 0x01, 0x00, 0x34, 0xF3, 0x03, 0x00, 0xAA, 0x00, 0x00, 0x40, 0xF9,
            0xF4, 0x03, 0x01, 0xAA, 0x08, 0x00, 0x40, 0xF9, 0x08, 0x31, 0x40, 0xF9, 0x00, 0x01, 0x3F, 0xD6
    };
    static constexpr uint8_t mp_delta_orig[] = {
            0xE0, 0x03, 0x17, 0xCB, 0xED, 0xD1, 0x63, 0x94, 0xE1, 0x03, 0x00, 0x2A, 0xE0, 0x03, 0x18, 0xAA,
            0x96, 0xAA, 0xFE, 0x97, 0x68, 0x02, 0x40, 0xF9, 0xE0, 0x03, 0x13, 0xAA, 0x08, 0x31, 0x40, 0xF9,
            0x00, 0x01, 0x3F, 0xD6, 0x00, 0x00, 0x40, 0xF9, 0x08, 0x00, 0x40, 0xF9, 0x08, 0x1D, 0x40, 0xF9,
            0x00, 0x01, 0x3F, 0xD6, 0xF8, 0x03, 0x00, 0x2A, 0xE0, 0x5F, 0xFD, 0x97, 0x71, 0x62, 0xFD, 0x97,
            0x19, 0x5C, 0x40, 0xA9, 0xB9, 0x5F, 0x3E, 0xA9, 0x97, 0x00, 0x00, 0xB4, 0xE1, 0x22, 0x00, 0x91,
            0x20, 0x00, 0x80, 0x52, 0xB8, 0xD6, 0x7D, 0x94, 0x99, 0x00, 0x00, 0xB4, 0xC1, 0x02, 0x18, 0x4B,
            0xE0, 0x03, 0x19, 0xAA
    };
    static constexpr uint8_t mp_delta_patch[] = {
            0xE0, 0x03, 0x17, 0xCB, 0xED, 0xD1, 0x63, 0x94, 0xE1, 0x03, 0x1F, 0x2A, 0xE0, 0x03, 0x18, 0xAA,
            0x96, 0xAA, 0xFE, 0x97, 0x68, 0x02, 0x40, 0xF9, 0xE0, 0x03, 0x13, 0xAA, 0x08, 0x31, 0x40, 0xF9,
            0x00, 0x01, 0x3F, 0xD6, 0x00, 0x00, 0x40, 0xF9, 0x08, 0x00, 0x40, 0xF9, 0x08, 0x1D, 0x40, 0xF9,
            0x00, 0x01, 0x3F, 0xD6, 0xF8, 0x03, 0x00, 0x2A, 0xE0, 0x5F, 0xFD, 0x97, 0x71, 0x62, 0xFD, 0x97,
            0x19, 0x5C, 0x40, 0xA9, 0xB9, 0x5F, 0x3E, 0xA9, 0x97, 0x00, 0x00, 0xB4, 0xE1, 0x22, 0x00, 0x91,
            0x20, 0x00, 0x80, 0x52, 0xB8, 0xD6, 0x7D, 0x94, 0x99, 0x00, 0x00, 0xB4, 0xC1, 0x02, 0x18, 0x4B,
            0xE0, 0x03, 0x19, 0xAA
    };
    static constexpr uint8_t mp_delta_mask[] = {
            0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF
    };
    static_assert(sizeof(mp_delta_orig) == sizeof(mp_delta_mask),
                  "mp_delta mask length mismatch");
    static constexpr uint8_t pc_mp_orig[] = {
            0xFD, 0x7B, 0xBE, 0xA9, 0xF3, 0x0B, 0x00, 0xF9, 0xFD, 0x03, 0x00, 0x91,
            0x08, 0x08, 0x40, 0xF9, 0xF3, 0x03, 0x00, 0xAA, 0x00, 0xC1, 0x02, 0x91,
            0xB4, 0x28, 0xB2, 0x97, 0x68, 0xAA, 0x40, 0xF9, 0xF3, 0x03, 0x00, 0x2A,
            0x01, 0x08, 0x80, 0x52, 0xE0, 0x03, 0x08, 0xAA, 0x81, 0x0C, 0x00, 0x94,
            0x1F, 0x04, 0x00, 0x71, 0x08, 0xC4, 0x9F, 0x1A, 0x1F, 0x01, 0x13, 0x6B,
            0x00, 0xB1, 0x93, 0x1A, 0xF3, 0x0B, 0x40, 0xF9, 0xFD, 0x7B, 0xC2, 0xA8,
            0xC0, 0x03, 0x5F, 0xD6,
    };
    static constexpr uint8_t pc_mp_patch[] = {
            0xE0, 0xFF, 0x9F, 0x52, 0xC0, 0x03, 0x5F, 0xD6, 0xFD, 0x03, 0x00, 0x91,
            0x08, 0x08, 0x40, 0xF9, 0xF3, 0x03, 0x00, 0xAA, 0x00, 0xC1, 0x02, 0x91,
            0xB4, 0x28, 0xB2, 0x97, 0x68, 0xAA, 0x40, 0xF9, 0xF3, 0x03, 0x00, 0x2A,
            0x01, 0x08, 0x80, 0x52, 0xE0, 0x03, 0x08, 0xAA, 0x81, 0x0C, 0x00, 0x94,
            0x1F, 0x04, 0x00, 0x71, 0x08, 0xC4, 0x9F, 0x1A, 0x1F, 0x01, 0x13, 0x6B,
            0x00, 0xB1, 0x93, 0x1A, 0xF3, 0x0B, 0x40, 0xF9, 0xFD, 0x7B, 0xC2, 0xA8,
            0xC0, 0x03, 0x5F, 0xD6,
    };
    static constexpr uint8_t pc_mp_max_orig[] = {
            0xFD, 0x7B, 0xBF, 0xA9, 0xFD, 0x03, 0x00, 0x91, 0x00, 0xA8, 0x40, 0xF9,
            0x01, 0x08, 0x80, 0x52, 0x54, 0x19, 0x00, 0x94, 0x1F, 0x04, 0x00, 0x71,
            0x00, 0xC4, 0x9F, 0x1A, 0xFD, 0x7B, 0xC1, 0xA8, 0xC0, 0x03, 0x5F, 0xD6,
    };
    static constexpr uint8_t pc_mp_max_patch[] = {
            0xE0, 0xFF, 0x9F, 0x52, 0xC0, 0x03, 0x5F, 0xD6, 0x00, 0xA8, 0x40, 0xF9,
            0x01, 0x08, 0x80, 0x52, 0x54, 0x19, 0x00, 0x94, 0x1F, 0x04, 0x00, 0x71,
            0x00, 0xC4, 0x9F, 0x1A, 0xFD, 0x7B, 0xC1, 0xA8, 0xC0, 0x03, 0x5F, 0xD6,
    };
    static constexpr uint8_t ret_65535[] = {0xE0, 0xFF, 0x9F, 0x52, 0xC0, 0x03, 0x5F, 0xD6};
    static constexpr uint8_t zero_w1[] = {0xE1, 0x03, 0x1F, 0x2A};
    static constexpr uint8_t mov_w8_wzr[] = {0xE8, 0x03, 0x1F, 0x2A};
    static constexpr uint8_t ldrb_w8[] = {0x08, 0x20, 0x40, 0x39};
    static constexpr uint8_t mov_w1_w0[] = {0xE1, 0x03, 0x00, 0x2A};
    static constexpr uint8_t prologue_be[] = {0xFD, 0x7B, 0xBE, 0xA9, 0xF3, 0x0B, 0x00, 0xF9};
    static constexpr uint8_t prologue_bf[] = {0xFD, 0x7B, 0xBF, 0xA9, 0xFD, 0x03, 0x00, 0x91};

    static constexpr uint8_t damage_orig[] = {
            0xC8, 0x02, 0x40, 0xF9, 0xA1, 0x8A, 0x4A, 0x29, 0x08, 0x0D, 0x40, 0xF9, 0xE3, 0xA3, 0x00, 0x91,
            0xE0, 0x03, 0x16, 0xAA, 0x00, 0x01, 0x3F, 0xD6, 0xC8, 0x02, 0x40, 0xF9, 0xA5, 0x43, 0x5F, 0xB8,
            0xE4, 0x03, 0x00, 0x2A, 0x09, 0x15, 0x40, 0xF9, 0xE8, 0x03, 0x13, 0xAA, 0xE0, 0x03, 0x16, 0xAA,
            0xE1, 0x03, 0x15, 0xAA, 0xE2, 0x03, 0x14, 0xAA, 0xE3, 0x03, 0x19, 0xAA, 0x20, 0x01, 0x3F, 0xD6,
            0x68, 0x02, 0x40, 0xF9, 0x09, 0x03, 0x00, 0x12, 0x09, 0x01, 0x01, 0x39, 0xF3, 0x1B, 0x40, 0xF9
    };
    static constexpr uint8_t damage_patch[] = {
            0xC8, 0x02, 0x40, 0xF9, 0xA1, 0x8A, 0x4A, 0x29, 0x08, 0x0D, 0x40, 0xF9, 0xE3, 0xA3, 0x00, 0x91,
            0xE0, 0x03, 0x16, 0xAA, 0x00, 0x01, 0x3F, 0xD6, 0xC8, 0x02, 0x40, 0xF9, 0xA5, 0x43, 0x5F, 0xB8,
            0xE4, 0x03, 0x00, 0x2A, 0x09, 0x15, 0x40, 0xF9, 0xE8, 0x03, 0x13, 0xAA, 0xE0, 0x03, 0x16, 0xAA,
            0xE1, 0x03, 0x15, 0xAA, 0xE2, 0x03, 0x14, 0xAA, 0x23, 0xB3, 0x6D, 0xD3, 0x20, 0x01, 0x3F, 0xD6,
            0x68, 0x02, 0x40, 0xF9, 0x09, 0x03, 0x00, 0x12, 0x09, 0x01, 0x01, 0x39, 0xF3, 0x1B, 0x40, 0xF9
    };
    static constexpr uint8_t mov_x3_x25[] = {0xE3, 0x03, 0x19, 0xAA};
    static constexpr uint8_t lsl_x3_x25_19[] = {0x23, 0xB3, 0x6D, 0xD3};

    static constexpr uint8_t dungeon_orig[] = {
            0xFF, 0x43, 0x01, 0xD1, 0xFD, 0x7B, 0x02, 0xA9, 0xF5, 0x1B, 0x00, 0xF9,
            0xF4, 0x4F, 0x04, 0xA9, 0xFD, 0x83, 0x00, 0x91, 0x55, 0xD0, 0x3B, 0xD5,
            0xA8, 0x16, 0x40, 0xF9, 0xA8, 0x83, 0x1F, 0xF8, 0x08, 0x00, 0x40, 0xF9,
            0xE8, 0x04, 0x00, 0xB4, 0xF3, 0x03, 0x00, 0xAA, 0x31, 0x0C, 0xED, 0x97,
            0xF4, 0x03, 0x00, 0xAA, 0xE1, 0x0D, 0xFF, 0x90, 0x21, 0x10, 0x35, 0x91,
            0xE0, 0x03, 0x00, 0x91, 0x47, 0xB9, 0xB6, 0x97, 0xE1, 0x03, 0x00, 0x91,
            0xE0, 0x03, 0x14, 0xAA, 0x4E, 0x0C, 0xED, 0x97, 0xE8, 0x03, 0x40, 0x39,
            0xF4, 0x03, 0x00, 0x2A, 0x68, 0x00, 0x00, 0x36, 0xE0, 0x0B, 0x40, 0xF9,
            0x38, 0x5E, 0x2D, 0x94, 0xF4, 0x02, 0x00, 0x36, 0x60, 0x02, 0x40, 0xF9,
            0xAB, 0x1B, 0x00, 0x94, 0x80, 0x02, 0x00, 0x36, 0x60, 0x02, 0x40, 0xF9,
            0x82, 0x10, 0x00, 0x94, 0x20, 0x02, 0x00, 0x36,
    };
    static constexpr uint8_t dungeon_patch[] = {
            0x20, 0x00, 0x80, 0x52, 0xC0, 0x03, 0x5F, 0xD6, 0x1F, 0x20, 0x03, 0xD5,
            0xF4, 0x4F, 0x04, 0xA9, 0xFD, 0x83, 0x00, 0x91, 0x55, 0xD0, 0x3B, 0xD5,
            0xA8, 0x16, 0x40, 0xF9, 0xA8, 0x83, 0x1F, 0xF8, 0x08, 0x00, 0x40, 0xF9,
            0xE8, 0x04, 0x00, 0xB4, 0xF3, 0x03, 0x00, 0xAA, 0x31, 0x0C, 0xED, 0x97,
            0xF4, 0x03, 0x00, 0xAA, 0xE1, 0x0D, 0xFF, 0x90, 0x21, 0x10, 0x35, 0x91,
            0xE0, 0x03, 0x00, 0x91, 0x47, 0xB9, 0xB6, 0x97, 0xE1, 0x03, 0x00, 0x91,
            0xE0, 0x03, 0x14, 0xAA, 0x4E, 0x0C, 0xED, 0x97, 0xE8, 0x03, 0x40, 0x39,
            0xF4, 0x03, 0x00, 0x2A, 0x68, 0x00, 0x00, 0x36, 0xE0, 0x0B, 0x40, 0xF9,
            0x38, 0x5E, 0x2D, 0x94, 0xF4, 0x02, 0x00, 0x36, 0x60, 0x02, 0x40, 0xF9,
            0xAB, 0x1B, 0x00, 0x94, 0x80, 0x02, 0x00, 0x36, 0x60, 0x02, 0x40, 0xF9,
            0x82, 0x10, 0x00, 0x94, 0x20, 0x02, 0x00, 0x36,
    };
    static constexpr uint8_t dungeon_prologue[] = {0xFF, 0x43, 0x01, 0xD1, 0xFD, 0x7B, 0x02, 0xA9, 0xF5, 0x1B, 0x00, 0xF9};
    static constexpr uint8_t dungeon_return_true[] = {0x20, 0x00, 0x80, 0x52, 0xC0, 0x03, 0x5F, 0xD6, 0x1F, 0x20, 0x03, 0xD5};

    // AE 3.16.0 recompiled the dungeon-skip gate (the function that reads
    // "skip_dungeon_enabled"): frame 0x50->0x70, an extra callee-saved pair
    // (stp x22,x21), stack-guard reg x21->x22, and the cbz branch offset re-encoded.
    // The legacy prologue no longer appears, so ship the 3.16.0 form too. Only the
    // drifting cbz word (offset 0x24) is masked; the return-true stub is unchanged.
    //  prologue: sub sp,#0x70; stp x29,x30,[sp,#0x40]; stp x22,x21,[sp,#0x50]; stp x20,x19,[sp,#0x60]
    //            add x29,sp,#0x40; mrs x22,tpidr_el0; ldr x8,[x22,#0x28]; stur x8,[x29,#-8]
    //            ldr x8,[x0]; cbz x8,*; mov x20,x0
    static constexpr uint8_t dungeon_orig_v316[] = {
            0xFF, 0xC3, 0x01, 0xD1, 0xFD, 0x7B, 0x04, 0xA9, 0xF6, 0x57, 0x05, 0xA9,
            0xF4, 0x4F, 0x06, 0xA9, 0xFD, 0x03, 0x01, 0x91, 0x56, 0xD0, 0x3B, 0xD5,
            0xC8, 0x16, 0x40, 0xF9, 0xA8, 0x83, 0x1F, 0xF8, 0x08, 0x00, 0x40, 0xF9,
            0x28, 0x08, 0x00, 0xB4, 0xF4, 0x03, 0x00, 0xAA, 0xF3, 0x03, 0x01, 0xAA,
            0xBF, 0x18, 0xEA, 0x97, 0xF5, 0x03, 0x00, 0xAA, 0x41, 0xEA, 0xFE, 0xD0,
            0x21, 0xD4, 0x29, 0x91, 0xE0, 0x83, 0x00, 0x91, 0xBB, 0x53, 0xAB, 0x97,
            0xE1, 0x83, 0x00, 0x91, 0xE0, 0x03, 0x15, 0xAA, 0xDC, 0x18, 0xEA, 0x97,
            0xE8, 0x83, 0x40, 0x39, 0xF5, 0x03, 0x00, 0x2A, 0x68, 0x00, 0x00, 0x36,
            0xE0, 0x1B, 0x40, 0xF9, 0x24, 0x59, 0x31, 0x94, 0x15, 0x06, 0x00, 0x36,
            0x80, 0x02, 0x40, 0xF9, 0x24, 0x26, 0x00, 0x94, 0xA0, 0x05, 0x00, 0x36,
            0x80, 0x02, 0x40, 0xF9, 0xE3, 0x1A, 0x00, 0x94,
    };
    static constexpr uint8_t dungeon_patch_v316[] = {
            0x20, 0x00, 0x80, 0x52, 0xC0, 0x03, 0x5F, 0xD6, 0x1F, 0x20, 0x03, 0xD5,
            0xF4, 0x4F, 0x06, 0xA9, 0xFD, 0x03, 0x01, 0x91, 0x56, 0xD0, 0x3B, 0xD5,
            0xC8, 0x16, 0x40, 0xF9, 0xA8, 0x83, 0x1F, 0xF8, 0x08, 0x00, 0x40, 0xF9,
            0x28, 0x08, 0x00, 0xB4, 0xF4, 0x03, 0x00, 0xAA, 0xF3, 0x03, 0x01, 0xAA,
            0xBF, 0x18, 0xEA, 0x97, 0xF5, 0x03, 0x00, 0xAA, 0x41, 0xEA, 0xFE, 0xD0,
            0x21, 0xD4, 0x29, 0x91, 0xE0, 0x83, 0x00, 0x91, 0xBB, 0x53, 0xAB, 0x97,
            0xE1, 0x83, 0x00, 0x91, 0xE0, 0x03, 0x15, 0xAA, 0xDC, 0x18, 0xEA, 0x97,
            0xE8, 0x83, 0x40, 0x39, 0xF5, 0x03, 0x00, 0x2A, 0x68, 0x00, 0x00, 0x36,
            0xE0, 0x1B, 0x40, 0xF9, 0x24, 0x59, 0x31, 0x94, 0x15, 0x06, 0x00, 0x36,
            0x80, 0x02, 0x40, 0xF9, 0x24, 0x26, 0x00, 0x94, 0xA0, 0x05, 0x00, 0x36,
            0x80, 0x02, 0x40, 0xF9, 0xE3, 0x1A, 0x00, 0x94,
    };
    static constexpr uint8_t dungeon_mask_v316[] = {
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };
    static_assert(sizeof(dungeon_orig_v316) == sizeof(dungeon_mask_v316),
                  "dungeon v316 mask length mismatch");
    static_assert(sizeof(dungeon_patch_v316) == sizeof(dungeon_orig_v316),
                  "dungeon v316 patch length mismatch");
    static constexpr uint8_t dungeon_prologue_v316[] = {0xFF, 0xC3, 0x01, 0xD1, 0xFD, 0x7B, 0x04, 0xA9, 0xF6, 0x57, 0x05, 0xA9};

    // Reconcile the max getter with the current getter's property field and getter BL.
    // Both original and ON current forms retain this tail unchanged.
    std::vector<uintptr_t> pc_current = find_code_pattern(ranges, pc_mp_orig, sizeof(pc_mp_orig));
    for (uintptr_t hit: find_code_pattern(ranges, pc_mp_patch, sizeof(pc_mp_patch))) push_unique(pc_current, hit);
    std::vector<MemoryRange> pc_max_ranges;
    if (pc_current.size() == 1) {
        uintptr_t property_getter = 0;
        const size_t field = item_catalog::unsigned_offset(read_u32(pc_current[0] + 7 * sizeof(uint32_t)), 3);
        if (decode_branch_target(pc_current[0] + 11 * sizeof(uint32_t), true, &property_getter)) {
            std::vector<uintptr_t> candidates = find_code_pattern(ranges, pc_mp_max_orig, sizeof(pc_mp_max_orig));
            for (uintptr_t hit: find_code_pattern(ranges, pc_mp_max_patch, sizeof(pc_mp_max_patch))) push_unique(candidates, hit);
            for (uintptr_t hit: candidates) {
                uintptr_t callee = 0;
                if (item_catalog::unsigned_offset(read_u32(hit + 2 * sizeof(uint32_t)), 3) == field &&
                    decode_branch_target(hit + 4 * sizeof(uint32_t), true, &callee) && callee == property_getter)
                    pc_max_ranges.push_back({hit, hit + sizeof(pc_mp_max_orig), true, false});
            }
        }
    }
    const BytePatch mp_patches[] = {
            {"battle.mp.cost", mp_cost_orig, sizeof(mp_cost_orig), mp_cost_patch, sizeof(mp_cost_patch), 0, ldrb_w8, mov_w8_wzr, sizeof(ldrb_w8)},
            {"battle.mp.delta", mp_delta_orig, sizeof(mp_delta_orig), mp_delta_patch, sizeof(mp_delta_patch), 8, mov_w1_w0, zero_w1, sizeof(mov_w1_w0), mp_delta_mask, mp_delta_mask},
            {"battle.mp.current", pc_mp_orig, sizeof(pc_mp_orig), pc_mp_patch, sizeof(pc_mp_patch), 0, prologue_be, ret_65535, sizeof(prologue_be)},
            {"battle.mp.max", pc_mp_max_orig, sizeof(pc_mp_max_orig), pc_mp_max_patch, sizeof(pc_mp_max_patch), 0, prologue_bf, ret_65535, sizeof(prologue_bf)},
    };
    for (const BytePatch &patch: mp_patches) {
        apply_byte_patch(std::strcmp(patch.name, "battle.mp.max") == 0 ? pc_max_ranges : ranges, patch, cfg.enabled && cfg.battle_mp);
    }

    const BytePatch damage = {"damage.x524288", damage_orig, sizeof(damage_orig), damage_patch, sizeof(damage_patch), 56, mov_x3_x25, lsl_x3_x25_19, sizeof(mov_x3_x25)};
    apply_byte_patch(ranges, damage, cfg.enabled && cfg.all_damage);

    const BytePatch skip_variants[] = {
            {"dungeon.skip", dungeon_orig, sizeof(dungeon_orig), dungeon_patch, sizeof(dungeon_patch), 0, dungeon_prologue, dungeon_return_true, sizeof(dungeon_prologue)},
            {"dungeon.skip.v316", dungeon_orig_v316, sizeof(dungeon_orig_v316), dungeon_patch_v316, sizeof(dungeon_patch_v316), 0, dungeon_prologue_v316, dungeon_return_true, sizeof(dungeon_prologue_v316), dungeon_mask_v316, dungeon_mask_v316},
    };
    apply_present_byte_patch(ranges, skip_variants, sizeof(skip_variants) / sizeof(skip_variants[0]), cfg.enabled && cfg.dungeon_skip);

    apply_encounter_judge_patch(ranges,
                                cfg.enabled && cfg.encounter_freeze,
                                cfg.enabled && cfg.encounter_force);

    patch_speed_constants(ranges, cfg.enabled && cfg.speedy);

    apply_team_god_patch(ranges, cfg.enabled && cfg.team_god_mode);

    apply_ad_bypass_patch(ranges, cfg.enabled && cfg.ad_bypass);
}

#include "injection-audit.h"

int main(int argc,char**argv){std::string dir=argv[1];std::ifstream in(dir+"/image.bin",std::ios::binary);std::vector<char> raw((std::istreambuf_iterator<char>(in)),{});void *mem=nullptr;posix_memalign(&mem,4096,raw.size());memcpy(mem,raw.data(),raw.size());uintptr_t base=(uintptr_t)mem;audit_base=base;
std::ifstream rel(dir+"/relocs.txt");uint64_t o,v;while(rel>>o>>v){uintptr_t p=base+v;memcpy((void*)(base+o),&p,8);}
std::ifstream symbols(dir+"/symbols.txt");std::string name;while(symbols>>name>>v)audit_symbols[name]=base+v;
std::vector<MemoryRange> ranges,readable_ranges;std::ifstream segs(dir+"/segments.txt");uint64_t a,z,fl;while(segs>>a>>z>>fl){readable_ranges.push_back({base+a,base+a+z,bool(fl&1),bool(fl&2)});if(fl&1)ranges.push_back(readable_ranges.back());}audit_ranges=ranges;
printf("AUDIT_VERSION 2\n");
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
std::vector<char> before((char*)mem,(char*)mem+raw.size());RuntimeConfig cfg;
uint32_t ow[std::size(item_catalog::owned_count_read)]{};
auto oa=scoped_layout_contract(ranges,token_shop_purchase_addr,item_catalog::owned_count_read,ow);report("mass.ownedCount",oa);g_token_purchase_owned_count_address.store(oa);
apply_runtime_byte_patches(cfg);apply_token_purchase_owned_count_zero_patch(true,"audit");auto apply_writes=writes;
apply_runtime_byte_patches(cfg);apply_token_purchase_owned_count_zero_patch(true,"repeat-audit");bool repeat_on=writes==apply_writes;
cfg.enabled=false;apply_runtime_byte_patches(cfg);apply_token_purchase_owned_count_zero_patch(false,"audit");auto undone=writes;
apply_runtime_byte_patches(cfg);apply_token_purchase_owned_count_zero_patch(false,"repeat-audit");bool repeat_off=writes==undone;
printf("IDEMPOTENT on=%d off=%d owned=%d\n",repeat_on,repeat_off,oa!=0);
bool restored=memcmp(before.data(),mem,raw.size())==0;printf("ROUNDTRIP item=%d applyWrites=%zu undoWrites=%zu restored=%d\n",item_ok,apply_writes,writes-apply_writes,restored);
free(mem);return restored&&item_ok&&injection_ok&&repeat_on&&repeat_off&&oa!=0?0:1;}
