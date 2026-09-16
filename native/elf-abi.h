#pragma once
#include <cstdint>

// Standard ELF64 file records, for host compilers (including macOS) without elf.h.
// The production resolver reads these records; these are not game-object layouts.
struct Elf64_Ehdr {
    unsigned char e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
};
struct Elf64_Phdr {
    uint32_t p_type, p_flags;
    uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
};
static_assert(sizeof(Elf64_Ehdr) == 64 && sizeof(Elf64_Phdr) == 56);
constexpr char ELFMAG[] = "\177ELF";
constexpr size_t SELFMAG = 4;
constexpr uint16_t EM_AARCH64 = 183;
constexpr uint32_t PT_GNU_EH_FRAME = 0x6474e550;
