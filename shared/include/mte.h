/*
 * PROJECT:      MatanelOS
 * LICENSE:      GPLv3
 * PURPOSE:      On-disk Matanel executable image definitions.
 */

#ifndef MATANELOS_SHARED_MTE_H
#define MATANELOS_SHARED_MTE_H

#include <stdint.h>

#define MTE_MAGIC_SIZE 4U
#define MTE_HEADER_SIZE 128U
#define MTE_RELOCATION_X86_64_RELATIVE 8U

#pragma pack(push, 1)

typedef struct _MTE_HEADER {
    uint8_t Magic[MTE_MAGIC_SIZE];
    uint64_t PreferredImageBase;
    uint64_t EntryRVA;
    uint64_t TextRVA;
    uint64_t TextSize;
    uint64_t DataRVA;
    uint64_t DataSize;
    uint64_t BssSize;
    uint64_t exports_rva;
    uint64_t exports_size;
    uint64_t reloc_rva;
    uint64_t reloc_size;
    uint64_t imports_rva;
    uint64_t imports_size;
    uint8_t Reserved[20];
} MTE_HEADER, *PMTE_HEADER;

typedef struct _MTE_RELOCATION {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} MTE_RELOCATION, *PMTE_RELOCATION;

typedef struct _MT_EXPORT_ENTRY {
    uint64_t name_rva;
    uint64_t func_rva;
} MT_EXPORT_ENTRY, *PMT_EXPORT_ENTRY;

typedef struct _MT_IMPORT_ENTRY {
    uint64_t lib_name_rva;
    uint64_t func_name_rva;
    uint64_t iat_addr_rva;
} MT_IMPORT_ENTRY, *PMT_IMPORT_ENTRY;

#pragma pack(pop)

#endif /* MATANELOS_SHARED_MTE_H */
