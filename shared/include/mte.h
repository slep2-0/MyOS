/*
 * PROJECT:      MatanelOS
 * LICENSE:      GPLv3
 * PURPOSE:      On-disk Matanel executable image definitions.
 */

#ifndef MATANELOS_SHARED_MTE_H
#define MATANELOS_SHARED_MTE_H

#include <stdint.h>
#include "annotations.h"

#define MTE_MAGIC_SIZE 4U             // Number of bytes in the MTE image signature.
#define MTE_HEADER_SIZE 128U          // Fixed on-disk header size.
#define MTE_RELOCATION_X86_64_RELATIVE 8U // Image-base relocation supported by the loader.

#pragma pack(push, 1)

typedef struct _MTE_HEADER {
    // All RVAs are measured from the beginning of the mapped image. A directory
    // is absent when both its RVA and size are zero; directory sizes are bytes.
    uint8_t Magic[MTE_MAGIC_SIZE]; // Must contain "MTE\0".
    uint64_t PreferredImageBase;   // Link-time image base used to calculate rebasing delta.
    uint64_t EntryRVA;             // Entry point RVA, or zero when the image has no entry point.
    uint64_t TextRVA;              // First byte of the executable code region.
    uint64_t TextSize;             // Number of bytes in the executable code region.
    uint64_t DataRVA;              // First byte of the initialized writable-data region.
    uint64_t DataSize;             // Number of file-backed bytes in the data region.
    uint64_t BssSize;              // Number of zero-filled bytes following initialized data.
    uint64_t exports_rva;          // RVA of the packed MT_EXPORT_ENTRY array.
    uint64_t exports_size;         // Total byte size of the export array.
    uint64_t reloc_rva;            // RVA of the packed MTE_RELOCATION array.
    uint64_t reloc_size;           // Total byte size of the relocation array.
    uint64_t imports_rva;          // RVA of the packed MT_IMPORT_ENTRY array.
    uint64_t imports_size;         // Total byte size of the import array.
    uint64_t tls_rva;              // RVA of one MTE_TLS_DIRECTORY, or zero when TLS is absent.
    uint64_t tls_size;             // Size of the TLS directory itself, not a thread's TLS block.
    uint8_t Reserved[4];           // Reserved for a later format revision; writers must zero it.
} MTE_HEADER, *PMTE_HEADER;

VALIDATE_SIZE(MTE_HEADER, MTE_HEADER_SIZE);

#define MTE_TLS_DIRECTORY_SIZE 48U          // Fixed size of MTE_TLS_DIRECTORY.
#define MTE_TLS_MODULE_INDEX_FIXUP_SIZE 8U  // Fixed size of one TLS fixup record.

typedef struct _MTE_TLS_DIRECTORY {
    uint64_t template_rva; // RVA of the initialized bytes copied into each thread's TLS block.
    uint64_t template_size; // Number of initialized bytes, equivalent to PT_TLS p_filesz.
    uint64_t total_size; // Complete per-thread block size, including zero-fill, from p_memsz.
    uint64_t alignment; // Required starting alignment for every allocated TLS block.
    uint64_t module_index_fixups_rva; // RVA of the MTE_TLS_MODULE_INDEX_FIXUP array.
    uint64_t module_index_fixups_size; // Total byte size of the module-index fixup array.
} MTE_TLS_DIRECTORY, *PMTE_TLS_DIRECTORY;

VALIDATE_SIZE(MTE_TLS_DIRECTORY, MTE_TLS_DIRECTORY_SIZE);

typedef struct _MT_TLS_INDEX {
    uint64_t ModuleIndex;
    uint64_t Offset;
} MT_TLS_INDEX, * PMT_TLS_INDEX;

typedef struct _MTE_TLS_MODULE_INDEX_FIXUP {
    // The loader writes this module's assigned TLS index to the target before
    // any code in the image can call __tls_get_addr.
    uint64_t target_rva; // RVA of an eight-byte R_X86_64_DTPMOD64 destination.
} MTE_TLS_MODULE_INDEX_FIXUP, *PMTE_TLS_MODULE_INDEX_FIXUP;

VALIDATE_SIZE(
    MTE_TLS_MODULE_INDEX_FIXUP,
    MTE_TLS_MODULE_INDEX_FIXUP_SIZE
);

typedef struct _MTE_RELOCATION {
    uint64_t r_offset; // RVA of the pointer that the loader must rewrite.
    uint64_t r_info;   // Relocation type; currently MTE_RELOCATION_X86_64_RELATIVE.
    int64_t r_addend;  // Image-relative addend combined with the actual load base.
} MTE_RELOCATION, *PMTE_RELOCATION;

typedef struct _MT_EXPORT_ENTRY {
    uint64_t name_rva; // RVA of the exported null-terminated symbol name.
    uint64_t func_rva; // RVA of the exported function or data object.
} MT_EXPORT_ENTRY, *PMT_EXPORT_ENTRY;

typedef struct _MT_IMPORT_ENTRY {
    uint64_t lib_name_rva;  // RVA of the null-terminated provider module name.
    uint64_t func_name_rva; // RVA of the null-terminated imported symbol name.
    uint64_t iat_addr_rva;  // RVA of the pointer slot written by the loader.
} MT_IMPORT_ENTRY, *PMT_IMPORT_ENTRY;

#pragma pack(pop)

#endif /* MATANELOS_SHARED_MTE_H */
