/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     FAT32 FileSystem Headers.
 */

#ifndef X86_KERNEL_FILESYSTEM_FAT32_HEADER
#define X86_KERNEL_FILESYSTEM_FAT32_HEADER
#include "../../kernel.h"

#define ATTR_LONG_NAME 0x0F
#define ATTR_DIRECTORY 0x10
#define ATTR_VOLUME_ID 0x08

#ifdef _MSC_VER
#pragma pack(push, 1)
typedef struct _FAT32_BPB {
#else
typedef struct __attribute__((packed)) _FAT32_BPB {
#endif
	uint8_t jump[3];
	uint8_t oem[8];
	uint16_t bytes_per_sector;
	uint8_t sectors_per_cluster;
	uint16_t reserved_sector_count;
	uint8_t num_fats;
	uint16_t root_entry_count;
	uint16_t total_sectors_16;
	uint8_t media;
	uint16_t fat_size_16;
	uint16_t sectors_per_track;
	uint16_t num_heads;
	uint32_t hidden_sectors;
	uint32_t total_sectors_32;
	uint32_t fat_size_32;
	uint16_t ext_flags;
	uint16_t fs_version;
	uint32_t root_cluster;
	uint16_t fs_info_sector;
	uint16_t backup_root_sector;
} FAT32_BPB;
#ifdef _MSC_VER
#pragma pack(pop)
#endif

#ifdef _MSC_VER
#pragma pack(push, 1)
typedef struct _FAT32_DIR_ENTRY {
#else
typedef struct __attribute__((packed)) _FAT32_DIR_ENTRY {
#endif
	char name[11];
	uint8_t attr;
	uint8_t nt_res;
	uint8_t crt_time_tenth;
	uint16_t crt_time;
	uint16_t crt_date;
	uint16_t lst_acc_date;
	uint16_t fst_clus_hi;
	uint16_t wrt_time;
	uint16_t wrt_date;
	uint16_t fst_clus_lo;
	uint32_t file_size;
} FAT32_DIR_ENTRY;
#ifdef _MSC_VER
#pragma pack(pop)
#endif

typedef struct _FAT32_FSINFO {
	uint32_t first_data_sector;
	uint32_t root_cluster;
	uint32_t sectors_per_fat;
	uint32_t bytes_per_sector;
	uint32_t sectors_per_cluster;
	uint32_t fat_start;
	uint16_t reserved_sector_count;
} FAT32_FSINFO;

// Initialize a FAT32 FileSystem on a given block device.
bool fat32_init(int disk_index);

// List files in root dir
void fat32_list_root(void);

// Read file into buffer (use kmalloc before using function, important.)
bool fat32_read_file(const char* filename, void* buffer, uint32_t buffer_size);

#endif