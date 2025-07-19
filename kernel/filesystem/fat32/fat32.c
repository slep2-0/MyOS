/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     FAT32 FileSystem Implementation.
 */

#include "fat32.h"

static FAT32_BPB bpb;
static FAT32_FSINFO fs;
static BLOCK_DEVICE* disk;

// Read sector into the buffer.
static bool read_sector(uint32_t lba, void* buf) {
	return disk->read_sector(disk, lba, buf);
}

// Read File-Allocation-Table (FAT) entry.
static uint32_t fat32_read_fat(uint32_t cluster) {
	uint32_t fat_offset = cluster * 4; // advances.
	uint32_t fat_sector = fs.fat_start + (fat_offset / fs.bytes_per_sector);
	uint32_t ent_offset = fat_offset % fs.bytes_per_sector;

	uint32_t buf32;
	read_sector(fat_sector, (uint8_t*)&buf32 + (ent_offset));
	return buf32 & 0x0FFFFFFF;
}

static uint32_t first_sector_of_cluster(uint32_t cluster) {
	return fs.first_data_sector + (cluster - 2) * fs.sectors_per_cluster;
}

// Read BPB (Bios Parameter Block) and initialize.
bool fat32_init(int disk_index) {
	disk = get_block_device(disk_index);
	if (!disk) { return false; }

	uint8_t buf[512];
	if (!read_sector(0, buf)) { return false; }
	kmemcpy(&bpb, buf, sizeof(bpb));

	fs.bytes_per_sector = bpb.bytes_per_sector;
	fs.sectors_per_cluster = bpb.sectors_per_cluster;
	fs.reserved_sector_count = bpb.reserved_sector_count;
	fs.sectors_per_fat = bpb.fat_size_32;
	fs.root_cluster = bpb.root_cluster;

	fs.fat_start = bpb.reserved_sector_count; // technically also reserved_sector_count of fs. holds it as well.
	fs.first_data_sector = fs.fat_start + bpb.num_fats * fs.sectors_per_fat;

	return true;
}

// Convert to uppercase.
static inline int toupper(int c) {
	if (c >= 'a' && c <= 'z') {
		return c - ('a' - 'A'); // Convert lowercase to uppercase
	}
	return c; // Return unchanged if not lowercase letter
}

// Compare short name
static bool cmp_name(const char* str_1, const char* str_2) {
	char t[12] = { 0 };
	for (int i = 0; i < 11; i++) { t[i] = str_1[i]; }
	for (int i = 0; i < 11; i++) {
		if (toupper(t[i]) != toupper(str_2[i])) {
			return false;
		}
	}
	return true;
}

// Walk cluster chain and read directory entries.
void fat32_list_root(void) {
	uint32_t cluster = fs.root_cluster;
	do {
		uint32_t sector = first_sector_of_cluster(cluster);
		for (uint32_t i = 0; i < fs.sectors_per_cluster; i++) {
			uint8_t buf[512];
			if (!read_sector(sector + i, buf)) {
				return;
			}

			FAT32_DIR_ENTRY* dir = (FAT32_DIR_ENTRY*)buf;

			for (uint32_t j = 0; j < fs.bytes_per_sector / sizeof(*dir); j++, dir++) {
				if (dir->name[0] == 0x00) {
					return; // No more entries.
				}

				if ((uint8_t)dir->name[0] == 0xE5) {
					continue;
				}

				if (dir->attr == ATTR_LONG_NAME) {
					// i don't support long names for now.
					continue;
				}

				print_to_screen("Found: ", COLOR_CYAN);
				for (uint32_t k = 0; k < 11; k++) {
					myos_printf(COLOR_LIGHT_GRAY, "%c", dir->name[k]);
				}
				print_to_screen("\r\n", COLOR_CYAN);
			}
		}
		cluster = fat32_read_fat(cluster);
	} while (cluster < 0x0FFFFFF8);
}

// Read a file into buffer.
bool fat32_read_file(const char* filename, void* buffer, uint32_t buffer_size) {
	uint32_t cluster = fs.root_cluster;

	do {
		uint32_t sector = first_sector_of_cluster(cluster);
		for (uint32_t i = 0; i < fs.sectors_per_cluster; i++) {
			uint8_t sblk[512];
			if (!read_sector(sector + i, sblk)) {
				return false;
			}

			FAT32_DIR_ENTRY* dir = (FAT32_DIR_ENTRY*)sblk;

			for (uint32_t j = 0; j < fs.bytes_per_sector / sizeof(*dir); j++, dir++) {
				if (dir->name[0] == 0x00) {
					return false; // No more entries
				}

				if (dir->attr == ATTR_LONG_NAME) {
					continue;
				}

				if (cmp_name(dir->name, filename)) {
					uint32_t file_cluster = (uint32_t)((dir->fst_clus_hi << 16) | dir->fst_clus_lo);
					uint32_t remain = dir->file_size;
					uint8_t* dst = (uint8_t*)buffer;

					// Read file data cluster by cluster
					while (file_cluster < 0x0FFFFFF8 && remain > 0 && buffer_size > 0) {
						uint32_t first_sector = first_sector_of_cluster(file_cluster);

						for (uint32_t sc = 0; sc < fs.sectors_per_cluster && remain > 0 && buffer_size > 0; sc++) {
							uint8_t sector_buf[512];
							if (!read_sector(first_sector + sc, sector_buf)) {
								return false;
							}

							uint32_t bytes_to_copy = fs.bytes_per_sector;
							if (bytes_to_copy > remain) bytes_to_copy = remain;
							if (bytes_to_copy > buffer_size) bytes_to_copy = buffer_size;

							// Copy only valid bytes
							for (uint32_t b = 0; b < bytes_to_copy; b++) {
								dst[b] = sector_buf[b];
							}

							dst += bytes_to_copy;
							remain -= bytes_to_copy;
							buffer_size -= bytes_to_copy;
						}
						file_cluster = fat32_read_fat(file_cluster);
					}
					return true;
				}
			}
		}
		cluster = fat32_read_fat(cluster);
	} while (cluster < 0x0FFFFFF8);

	return false;
}