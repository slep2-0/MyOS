#pragma once

#include <stdint.h>

typedef struct {
    uint64_t r_offset; /* Address (RVA) */
    uint64_t r_info;   /* Relocation type and symbol index */
    int64_t  r_addend; /* Addend */
} Rela;

#define R_X86_64_RELATIVE 8

#pragma pack(push, 1)
typedef struct {
    uint8_t  Magic[4];
    uint64_t PreferredImageBase; /* __image_base */
    uint64_t EntryRVA;           /* __entry_rva */
    uint64_t TextRVA;            /* __text_rva */
    uint64_t TextSize;
    uint64_t DataRVA;
    uint64_t DataSize;
    uint64_t BssSize;
    uint64_t exports_rva;
    uint64_t exports_size;
    uint64_t reloc_rva;
    uint64_t reloc_size;
    uint64_t imports_rva; // RVA To import array, then absolute addresses.
    uint64_t imports_size; // Size of total imports (to find out total we divide by MT_IMPORT_ENTRIES)
    uint8_t  Reserved[20];      /* pad the rest to 128 bytes */
} MTE_HEADER;
#pragma pack(pop)

// Exports are RVA
typedef struct {
    uint64_t name_rva;
    uint64_t func_rva;
} MT_EXPORT_ENTRY;

// Imports are absolutes.
typedef struct {
    uint64_t lib_name_absolute;   // RVA to string "kernel32.dll"
    uint64_t func_name_absolute;  // RVA to string "PrintString"
    uint64_t iat_addr_absolute;   // RVA to the function pointer to be patched
} MT_IMPORT_ENTRY;