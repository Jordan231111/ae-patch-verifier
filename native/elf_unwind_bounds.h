#pragma once
#include <cstdint>
#include <cstddef>
#include <limits>

// ELF .eh_frame records, not game layout constants. Only the augmentation and
// PC extent are decoded; interpreting CFI instructions is unnecessary here.
namespace elf_unwind {
template<class Read> struct Cursor {
    Read &read;uintptr_t at,end;
    template<class T> bool take(T &value) {
        if(at>end || sizeof(T)>end-at || !read(at,&value,sizeof(T))) return false;
        at+=sizeof(T);return true;
    }
    bool uleb(uint64_t &value) {
        value=0;
        for(unsigned shift=0;shift<64;shift+=7) {
            uint8_t byte=0;if(!take(byte) || (shift==63 && (byte&0x7f)>1)) return false;
            value|=uint64_t(byte&0x7f)<<shift;
            if(!(byte&0x80)) return true;
        }
        return false;
    }
    bool sleb() {
        for(unsigned i=0;i<10;++i) {
            uint8_t byte=0;if(!take(byte) || (i==9 && byte!=0 && byte!=0x7f)) return false;
            if(!(byte&0x80)) return true;
        }
        return false;
    }
    bool skip_pointer(uint8_t encoding) {
        if((encoding&0x70)==0x50) {
            if(at>UINTPTR_MAX-(sizeof(uintptr_t)-1)) return false;
            at=(at+sizeof(uintptr_t)-1)&~(sizeof(uintptr_t)-1);
        }
        const auto format=encoding&0x0f;
        if(format==1) {uint64_t ignored;return uleb(ignored);}
        if(format==9) return sleb();
        const size_t size=format==0 ? sizeof(uintptr_t) : (format==2 || format==10) ? 2 :
            (format==3 || format==11) ? 4 : (format==4 || format==12) ? 8 : 0;
        if(!size || at>end || size>end-at) return false;
        at+=size;return true;
    }
};

template<class Read>
bool fde_contains(Read &read,uintptr_t fde,uintptr_t expected,uintptr_t pc,uintptr_t *end=nullptr) {
    uint32_t length=0,back=0;
    if(fde>UINTPTR_MAX-8 || !read(fde,&length,4) || length<12 || length==UINT32_MAX ||
       length>UINTPTR_MAX-fde-4 || !read(fde+4,&back,4) || back>fde+4) return false;
    const uintptr_t cie=fde+4-back;
    uint32_t cie_length=0,tag=1;
    if(!read(cie,&cie_length,4) || cie_length<9 || cie_length==UINT32_MAX || cie>UINTPTR_MAX-4 ||
       cie_length>UINTPTR_MAX-cie-4 || !read(cie+4,&tag,4) || tag) return false;
    Cursor<Read> cursor{read,cie+8,cie+4+cie_length};
    uint8_t version=0;if(!cursor.take(version) || (version!=1 && version!=3 && version!=4)) return false;
    char augmentation[32]{};size_t size=0;
    for(;size<sizeof(augmentation);++size) {
        if(!cursor.take(augmentation[size])) return false;
        if(!augmentation[size]) break;
    }
    if(size==sizeof(augmentation) || augmentation[0]!='z') return false;
    if(version==4) {
        uint8_t address_size=0,segment_size=0;
        if(!cursor.take(address_size) || !cursor.take(segment_size) || address_size!=8 || segment_size!=0) return false;
    }
    uint64_t ignored=0,augmentation_size=0;
    if(!cursor.uleb(ignored) || !ignored || !cursor.sleb()) return false;
    if(version==1) {uint8_t reg=0;if(!cursor.take(reg)) return false;}
    else if(!cursor.uleb(ignored)) return false;
    if(!cursor.uleb(augmentation_size) || augmentation_size>cursor.end-cursor.at) return false;
    cursor.end=cursor.at+augmentation_size;
    uint8_t encoding=0xff;
    for(size_t i=1;i<size;++i) {
        uint8_t value=0;
        switch(augmentation[i]) {
            case 'P': if(!cursor.take(value) || !cursor.skip_pointer(value)) return false;break;
            case 'L': if(!cursor.take(value)) return false;break;
            case 'R': if(!cursor.take(encoding)) return false;break;
            case 'S': break;
            default: return false;
        }
    }
    // Android ARM64's five observed CIE variants use pcrel/sdata4 for PCs.
    // Unknown encodings never fall back to the preceding table entry.
    if(encoding!=0x1b) return false;
    int32_t start=0,extent=0;
    if(!read(fde+8,&start,4) || !read(fde+12,&extent,4) || extent<=0) return false;
    if((start<0 && static_cast<uintptr_t>(-static_cast<int64_t>(start))>fde+8) ||
       (start>=0 && static_cast<uintptr_t>(start)>UINTPTR_MAX-fde-8)) return false;
    const uintptr_t begin=(fde+8)+static_cast<intptr_t>(start);
    if(begin!=expected || uintptr_t(extent)>UINTPTR_MAX-begin || pc<begin ||
       pc>UINTPTR_MAX-4 || pc+4>begin+static_cast<uintptr_t>(extent)) return false;
    if(end) *end=begin+static_cast<uintptr_t>(extent);
    return true;
}
}
