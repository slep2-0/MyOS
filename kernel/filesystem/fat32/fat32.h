/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     FAT32 FileSystem Headers.
 */

#ifndef X86_KERNEL_FILESYSTEM_FAT32_HEADER
#define X86_KERNEL_FILESYSTEM_FAT32_HEADER
 // Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../trace.h"
#include "../../drivers/gop/gop.h"
#include "../../mtstatus.h"

#define END_OF_DIRECTORY 0x00
#define DELETED_DIR_ENTRY 0xE5


#define FAT32_FAT_MASK        0x0FFFFFFFU
#define FAT32_FREE_CLUSTER    0x00000000U
#define FAT32_BAD_CLUSTER     0x0FFFFFF7U
#define FAT32_EOC_MIN         0x0FFFFFF8U /* inclusive */
#define FAT32_EOC_MAX         0x0FFFFFFFU /* inclusive */



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
MTSTATUS fat32_init(int disk_index);

// List files in root dir
void fat32_list_root(void);

typedef enum _FAT32_ATTRIBUTES {
	ATTR_READ_ONLY = 0x01,  // File is read-only
	ATTR_HIDDEN = 0x02,  // File is hidden
	ATTR_SYSTEM = 0x04,  // System file
	ATTR_VOLUME_ID = 0x08,  // Volume label
	ATTR_DIRECTORY = 0x10,  // Entry is a directory
	ATTR_ARCHIVE = 0x20,  // File should be archived
	// Special combination values
	ATTR_LONG_NAME = 0x0F,  // Long File Name entry (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)
} FAT32_ATTRIBUTES;

/// <summary>
/// A FAT32 Function that reads the file requested into a dynamically allocated buffer.
/// </summary>
/// <param name="filename">The Filename to read, e.g "file.txt" or "tmp/folder/myfile.txt"</param>
/// <param name="file_size_out">A pointer to put the file size in bytes</param>
/// <param name="bufferOut">A pointer to put the file buffer in (doesn't need to be dynamically allocated)</param>
/// <returns>MTSTATUS Status Code.</returns>
MTSTATUS fat32_read_file(const char* filename, uint32_t* file_size_out, void** buffer_out);

/// <summary>
/// Creates a new directory
/// </summary>
/// <param name="path">The full path to the new directory</param>
/// <returns>MTSTATUS Status code.</returns>
MTSTATUS fat32_create_directory(const char* path);

/// <summary>
/// Creates a new file and writes data to it.
/// </summary>
/// <param name="path">The full path of the file to create</param>
/// <param name="data">A pointer to the data to write.</param>
/// <param name="size">The number of bytes to write</param>
/// <param name="file_modification_mode">Whether to APPEND or REPLACE the file. (in FS_WRITE_MODES enum)</param>
/// <returns>MTSTATUS Status code.</returns>
MTSTATUS fat32_write_file(const char* path, const void* data, uint32_t size, uint32_t file_modification_mode);

/// <summary>
/// Lists the directory given.
/// </summary>
/// <param name="path">Path to directory, e.g "mydir/" </param>
/// <param name="listings">[OUT] Pointer to directory listing. (each seperated with a newline character)</param>
/// <param name="max_len">[IN] Max size of listings buffer.</param>
/// <returns>MTSTATUS Status code.</returns>
MTSTATUS fat32_list_directory(const char* path, char* listings, size_t max_len);

/// <summary>
/// This function deletes the directory given to the function from the system.
/// </summary>
/// <param name="path">Full path to delete directory.</param>
/// <returns>MTSTATUS Status code.</returns>
MTSTATUS fat32_delete_directory(const char* path);

/// <summary>
/// This function deletes the file given to the function from the system.
/// </summary>
/// <param name="path">Full path to delete file.</param>
/// <returns>MTSTATUS Status code.</returns>
MTSTATUS fat32_delete_file(const char* path);

/// <summary>
/// This function returns if the directory given to the function is empty (e.g, has only '.' and '..' entries)
/// </summary>
/// <param name="path">Full path to dir</param>
/// <returns>True or false based if empty or not.</returns>
bool fat32_directory_is_empty(const char* path);

#endif
