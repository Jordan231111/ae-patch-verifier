#pragma once
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace native_scan {
struct Range {uintptr_t start,end;bool executable,writable;};
struct Segment {size_t offset,length;};

template<class Read>
std::vector<Segment> capture_range(uintptr_t start,size_t length,uint8_t *out,size_t page,Read read) {
    std::vector<Segment> segments;
    if(!out || !page || length>UINTPTR_MAX-start)return segments;
    size_t pos=0;
    while(pos<length) {
        const auto got=read(start+pos,out+pos,length-pos);
        if(got>0) {
            if(size_t(got)>length-pos)return {};
            segments.push_back({pos,size_t(got)});pos+=size_t(got);
            if(pos==length)break;
        }
        // The request may begin inside a page. Advance using the live address,
        // so an unreadable page never hides bytes in the next readable page.
        const size_t skip=page-(start+pos)%page;
        if(skip>=length-pos)break;
        pos+=skip;
    }
    return segments;
}

class Snapshot {
    struct CapturedRange {
        uintptr_t live_start;size_t length,buffer_offset;
        std::vector<Segment> segments;
    };
    std::unordered_map<std::string,std::vector<uintptr_t>> queries;
    std::unique_ptr<uint8_t[]> buffer;
    size_t buffer_capacity = 0;
    size_t query_bytes=0;
    static constexpr size_t max_query_bytes=4*1024*1024,max_queries=1024;
public:
    size_t allocated_bytes() const { return buffer_capacity; }
    std::vector<CapturedRange> ranges;
    uint64_t maps_generation=0;

    template<class Read>
    void build(const std::vector<Range> &src,uint64_t generation,size_t page,Read read) {
        ranges.clear();queries.clear();query_bytes=0;
        size_t total=0;
        for(const auto &r:src)if(r.end>r.start) {
            if(r.end-r.start>SIZE_MAX-total)throw std::length_error("Snapshot size overflow");
            total+=r.end-r.start;
        }
        // The reader fills captured segments. Holes are never exposed by read or visit.
        if (total > buffer_capacity) {
            buffer.reset(new uint8_t[total]);
            buffer_capacity = total;
        }
        ranges.reserve(src.size());
        size_t offset=0;
        for(const auto &r:src)if(r.end>r.start) {
            const size_t length=r.end-r.start;
            ranges.push_back({r.start,length,offset,capture_range(r.start,length,buffer.get()+offset,page,read)});
            offset+=length;
        }
        std::stable_sort(ranges.begin(),ranges.end(),[](const auto &a,const auto &b){return a.live_start<b.live_start;});
        maps_generation=generation;
    }
    const CapturedRange *find(uintptr_t start,size_t length) const {
        auto it=std::upper_bound(ranges.begin(),ranges.end(),start,
            [](uintptr_t value,const auto &range){return value<range.live_start;});
        while(it!=ranges.begin()) {
            const auto &r=*--it;const size_t offset=start-r.live_start;
            if(offset<=r.length && length<=r.length-offset)return &r;
        }
        return nullptr;
    }
    bool read(uintptr_t address,void *out,size_t length) const {
        const auto *r=find(address,length);if(!r)return false;
        const size_t offset=address-r->live_start;
        for(const auto &s:r->segments)if(offset>=s.offset && offset-s.offset<=s.length && length<=s.length-(offset-s.offset)) {
            std::memcpy(out,buffer.get()+r->buffer_offset+offset,length);return true;
        }
        return false;
    }
    template<class Visit>
    bool visit(uintptr_t start,size_t length,Visit &&visit) const {
        const auto *r=find(start,length);if(!r)return false;
        const size_t offset=start-r->live_start;
        for(const auto &s:r->segments) {
            const size_t lo=std::max(offset,s.offset),hi=std::min(offset+length,s.offset+s.length);
            if(hi>lo)visit(r->live_start+lo,buffer.get()+r->buffer_offset+lo,hi-lo);
        }
        return true; // captured holes remain holes; never refill from live bytes
    }
    bool fully_read() const {
        for(const auto &r:ranges) {
            size_t size=0;for(const auto &s:r.segments)size+=s.length;
            if(size!=r.length)return false;
        }
        return true;
    }
    size_t captured_bytes() const {
        size_t size=0;for(const auto &r:ranges)for(const auto &s:r.segments)size+=s.length;
        return size;
    }
    std::string query_key(const std::vector<Range> &scope,char kind) const {
        for(const auto &r:scope)if(r.end>r.start && !find(r.start,r.end-r.start))return {};
        std::string key(1,kind);append(key,scope.size());
        // Preserve caller order, duplicates and clipped scopes.
        for(const auto &r:scope) {append(key,r.start);append(key,r.end);}
        return key;
    }
    template<class T> static void append(std::string &key,const T &value) {
        key.append(reinterpret_cast<const char *>(&value),sizeof(value));
    }
    const std::vector<uintptr_t> *lookup(const std::string &key) const {
        const auto it=queries.find(key);return it==queries.end()?nullptr:&it->second;
    }
    void remember(std::string key,const std::vector<uintptr_t> &hits) {
        if(key.empty() || queries.size()>=max_queries || key.size()>max_query_bytes ||
           hits.size()>(max_query_bytes-key.size())/sizeof(uintptr_t))return;
        const size_t bytes=key.size()+hits.size()*sizeof(uintptr_t);
        if(bytes<=max_query_bytes-query_bytes && queries.emplace(std::move(key),hits).second)query_bytes+=bytes;
    }
};

inline std::vector<Range> scope(const std::vector<Range> &ranges,uintptr_t start,size_t length) {
    if(!start)return ranges;
    if(length>UINTPTR_MAX-start)return {};
    std::vector<Range> out;
    for(const auto &r:ranges) {
        const auto lo=std::max(r.start,start),hi=std::min(r.end,start+length);
        if(hi>lo)out.push_back({lo,hi,r.executable,r.writable});
    }
    return out;
}
template<class Word,size_t N,class Role>
uintptr_t capture(uintptr_t at,const Word (&pattern)[N],Role role) {
    if(at)for(size_t i=0;i<N;++i)if(pattern[i].role==role && at<=UINTPTR_MAX-i*4)return at+i*4;
    return 0;
}

template<class Word,size_t N,class Emit>
void match_words(uintptr_t base,const uint8_t *bytes,size_t length,const Word (&pattern)[N],Emit emit) {
    static_assert(N>0);
    if(length<N*4)return;
    size_t anchor=0;
    for(size_t i=1;i<N;++i)if(std::popcount(pattern[i].mask)>std::popcount(pattern[anchor].mask))anchor=i;
    const uint32_t mask=pattern[anchor].mask,value=pattern[anchor].value&mask;
    auto match=[&](size_t at) {
        for(size_t i=0;i<N;++i)if(i!=anchor) {
            uint32_t word=0;std::memcpy(&word,bytes+at+i*4,4);
            if((word&pattern[i].mask)!=(pattern[i].value&pattern[i].mask))return;
        }
        emit(base+at);
    };
    const size_t limit=length-N*4;
    size_t at=(4-(base&3))&3;
#if defined(__aarch64__)
    const auto masks=vdupq_n_u32(mask),values=vdupq_n_u32(value);
    alignas(16) uint32_t lanes[4];
    for(;at<=limit && limit-at>=12;at+=16) {
        const auto words=vld1q_u32(reinterpret_cast<const uint32_t *>(bytes+at+anchor*4));
        const auto hits=vceqq_u32(vandq_u32(words,masks),values);
        if(!vmaxvq_u32(hits))continue;
        vst1q_u32(lanes,hits);
        for(size_t i=0;i<4;++i)if(lanes[i])match(at+i*4);
    }
#endif
    for(;at<=limit;at+=4) {
        uint32_t word=0;std::memcpy(&word,bytes+at+anchor*4,4);
        if((word&mask)==value)match(at);
    }
}
} // namespace native_scan
