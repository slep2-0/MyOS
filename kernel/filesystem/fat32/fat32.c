/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     FAT32 FileSystem Implementation.
 */

#include "fat32.h"
#include "../../drivers/blk/block.h"
#include "../../assert.h"

static FAT32_BPB bpb;
static FAT32_FSINFO fs;
static BLOCK_DEVICE* disk;
extern GOP_PARAMS gop_local;


#define MAX_LFN_ENTRIES 20       // Allows up to 260 chars (20*13)
#define MAX_LFN_LEN 260

typedef struct {
	uint16_t name_chars[13];     // UTF-16 characters from one LFN entry
} LFN_ENTRY_BUFFER;

// Read sector into the buffer.
static bool read_sector(uint32_t lba, void* buf) {
	tracelast_func("read_sector - fat32");
	return disk->read_sector(disk, lba, buf);
}

// Write to sector from buffer
static bool write_sector(uint32_t lba, const void* buf) {
	tracelast_func("write_sector - fat32");
	return disk->write_sector(disk, lba, buf);
}

// Compute checksum of 8.3 name (from specification)
static uint8_t lfn_checksum(const uint8_t short_name[11]) {
	uint8_t sum = 0;
	for (int i = 0; i < 11; i++) {
		sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + short_name[i];
	}
	return sum;
}

// Convert to uppercase.
static inline int toupper(int c) {
	tracelast_func("toupper - fat32");
	if (c >= 'a' && c <= 'z') {
		return c - ('a' - 'A'); // Convert lowercase to uppercase
	}
	return c; // Return unchanged if not lowercase letter
}

// Compare short name
static bool cmp_name(const char* str_1, const char* str_2) {
	tracelast_func("cmp_name - fat32");
	char t[12] = { 0 };
	for (int i = 0; i < 11; i++) { t[i] = str_1[i]; }
	for (int i = 0; i < 11; i++) {
		if (toupper(t[i]) != toupper(str_2[i])) {
			return false;
		}
	}
	return true;
}


// Helper: convert "NAME.EXT" or "NAMEEXT" to 11-byte FAT short-name (uppercased, space-padded).
static void format_short_name(const char* input, char out[11]) {
	// Fill with spaces
	for (int i = 0; i < 11; ++i) out[i] = ' ';
	// Copy name (up to 8 chars)
	int ni = 0;
	const unsigned char* p = (const unsigned char*)input;
	while (*p && *p != '.' && ni < 8) {
		out[ni++] = (char)toupper(*p++);
	}
	// If '.' found, copy extension (up to 3)
	if (*p == '.') {
		++p;
		int ei = 0;
		while (*p && ei < 3) {
			out[8 + ei++] = (char)toupper(*p++);
		}
	}
}

/// <summary>
/// Read LFN Chain and reconstruct full filename.
/// </summary>
/// <param name="dir_entries">Pointer to Directory Entries in sector</param>
/// <param name="entry_count">Number of entries in the buffer supplied</param>
/// <param name="out_name">Output buffer (ASCII), must be >= MAX_LFN_LEN</param>
/// <returns>Pointer to 8.3 entry IF found, NULL Otherwise.</returns>
static FAT32_DIR_ENTRY* read_lfn(FAT32_DIR_ENTRY* cur, uint32_t remaining, char* out_name, uint32_t* out_consumed) {
	if (!cur || remaining == 0) return NULL;
	*out_consumed = 0;

	// Collect LFN pointers (they appear immediately before the 8.3 entry).
	FAT32_DIR_ENTRY* lfn_list[MAX_LFN_ENTRIES];
	uint32_t lfn_count = 0;
	uint32_t i = 0;

	// Walk forward while entries are LFN (0x0F). Stop when we hit a non-LFN or end.
	while (i < remaining && ((uint8_t)cur[i].name[0] != 0x00) && (cur[i].attr == ATTR_LONG_NAME)) {
		if (lfn_count < MAX_LFN_ENTRIES) lfn_list[lfn_count++] = &cur[i];
		i++;
	}

	// i now points to the candidate 8.3 entry (must exist and not be end marker).
	if (i >= remaining) return NULL;
	FAT32_DIR_ENTRY* short_entry = &cur[i];
	if ((uint8_t)short_entry->name[0] == 0x00 || (uint8_t)short_entry->name[0] == 0xE5) {
		// no valid 8.3 here
		return NULL;
	}

	// If no LFN entries collected, just copy short name into out_name and return short_entry.
	if (lfn_count == 0) {
		// format short 11-byte name to null-terminated ascii-ish (no dot)
		char shortname_formatted[11];
		format_short_name(short_entry->name, shortname_formatted);
		kstrcpy(out_name, shortname_formatted);
		out_name[11] = '\0';
		*out_consumed = 1;
		return short_entry;
	}

	// Validate checksum of short name against each LFN entry's checksum field (offset 13)
	uint8_t cs = lfn_checksum((uint8_t*)short_entry->name);
	for (uint32_t j = 0; j < lfn_count; ++j) {
		uint8_t entry_checksum = *((uint8_t*)lfn_list[j] + 13);
		if (entry_checksum != cs) return NULL; // mismatch -> invalid chain
	}

	// Reconstruct name: iterate lfn_list in reverse (last chunk -> first chunk)
	uint32_t pos = 0;
	for (int j = (int)lfn_count - 1; j >= 0; --j) {
		uint8_t* ebytes = (uint8_t*)lfn_list[j];

		// Name1 at offset 1, 5 UTF-16 chars
		uint16_t* name1 = (uint16_t*)(ebytes + 1);
		for (int c = 0; c < 5; ++c) {
			uint16_t ch = name1[c];
			if (ch == 0x0000) { out_name[pos] = '\0'; goto done; }
			if (ch <= 0x7F) out_name[pos++] = (char)ch; else out_name[pos++] = '?';
			if (pos >= MAX_LFN_LEN - 1) goto done;
		}

		// Name2 at offset 14, 6 UTF-16 chars
		uint16_t* name2 = (uint16_t*)(ebytes + 14);
		for (int c = 0; c < 6; ++c) {
			uint16_t ch = name2[c];
			if (ch == 0x0000) { out_name[pos] = '\0'; goto done; }
			if (ch <= 0x7F) out_name[pos++] = (char)ch; else out_name[pos++] = '?';
			if (pos >= MAX_LFN_LEN - 1) goto done;
		}

		// Name3 at offset 28, 2 UTF-16 chars
		uint16_t* name3 = (uint16_t*)(ebytes + 28);
		for (int c = 0; c < 2; ++c) {
			uint16_t ch = name3[c];
			if (ch == 0x0000) { out_name[pos] = '\0'; goto done; }
			if (ch <= 0x7F) out_name[pos++] = (char)ch; else out_name[pos++] = '?';
			if (pos >= MAX_LFN_LEN - 1) goto done;
		}
	}

done:
	out_name[pos] = '\0';
	// consumed entries = number of LFN entries + the 8.3 entry
	*out_consumed = (uint32_t)lfn_count + 1;
	return short_entry;
}

// Read the FAT for the given cluster, to inspect data about this specific cluster, like which sectors are free, used, what's the next sector, and which sector are EOF (end of file = 0x0FFFFFFF)
static uint32_t fat32_read_fat(uint32_t cluster) {
	tracelast_func("fat32_read_fat");

	uint32_t fat_offset = cluster * 4;
	uint32_t fat_sector = fs.fat_start + (fat_offset / fs.bytes_per_sector);
	uint32_t ent_offset = fat_offset % fs.bytes_per_sector;

	void* buf = MtAllocateVirtualMemory(512, 512);
	if (!buf) return 0x0FFFFFFF; // treat as EOF on allocation failure

	if (!read_sector(fat_sector, buf)) {
		tracelast_func("Couldn't read sector.");
		MtFreeVirtualMemory(buf);
		return 0x0FFFFFFF;
	}

	uint32_t val = *(uint32_t*)((uint8_t*)buf + ent_offset);
	val &= 0x0FFFFFFF;
	MtFreeVirtualMemory(buf);
	return val;
}

static uint32_t first_sector_of_cluster(uint32_t cluster) {
	tracelast_func("first_sector_of_cluster");
	return fs.first_data_sector + (cluster - 2) * fs.sectors_per_cluster;
}


static bool fat32_write_fat(uint32_t cluster, uint32_t value) {
	tracelast_func("fat32_write_fat");

	uint32_t fat_offset = cluster * 4;
	uint32_t fat_sector = fs.fat_start + (fat_offset / fs.bytes_per_sector);
	uint32_t ent_offset = fat_offset % fs.bytes_per_sector;

	// We must read the sector first, modify it, then write it back
	void* buf = MtAllocateVirtualMemory(512, 512);
	if (!buf) return false;

	if (!read_sector(fat_sector, buf)) {
		// free the memory before returning
		MtFreeVirtualMemory(buf);
		return false;
	}

	// Modify the entry in the buffer
	uint32_t* fat_entry = (uint32_t*)((uint8_t*)buf + ent_offset);
	*fat_entry = (*fat_entry & 0xF0000000) | (value & 0x0FFFFFFF); // Preserve top 4 bits.

	// Write the modified sector back to to disk
	bool success = write_sector(fat_sector, buf);

	// Cleanup
	MtFreeVirtualMemory(buf);
	return success;
}

static uint32_t fat32_find_free_cluster(void) {
	tracelast_func("fat32_find_free_cluster");
	// Start searching from cluster 2 (the first usable cluster)
	// In a more advanced implementation, we would use the FSInfo sector to find a hint. But even then that hint can be misleading (read osdev on FAT)
	uint32_t total_clusters = (bpb.total_sectors_32 - fs.first_data_sector) / fs.sectors_per_cluster;
	for (uint32_t i = 2; i < total_clusters; i++) {
		if (fat32_read_fat(i) == FAT32_FREE_CLUSTER) {
			return i;
		}
	}
	return 0; // no free clusters found..
}

static bool zero_cluster(uint32_t cluster) {
	void* buf = MtAllocateVirtualMemory(512, 512);
	bool success = true;
	if (!buf) return false;
	kmemset(buf, 0, 512);

	uint32_t sector = first_sector_of_cluster(cluster);
	for (uint32_t i = 0; i < fs.sectors_per_cluster; i++) {
		if (!write_sector(sector + i, buf)) {
			success = false;
			break;
		}
	}

	MtFreeVirtualMemory(buf);
	return success;
}

/// <summary>
/// Finds a directory entry for a given path
/// </summary>
/// <param name="path">The full path to the entry</param>
/// <param name="out_entry">[OUT] Pointer to store the found directory entry</param>
/// <param name="out_parent_cluster">[OUT] Pointer to store the cluster number of the parent directory.</param>
/// <returns>True if the entry was found, false otherwise.</returns>
static bool fat32_find_entry(const char* path, FAT32_DIR_ENTRY* out_entry, uint32_t* out_parent_cluster) {
	char path_copy[260];
	kstrcpy(path_copy, path); // Use a mutable copy

	uint32_t current_cluster = fs.root_cluster;
	uint32_t parent_cluster_of_last_found = fs.root_cluster;

	// Handle the root path "/" separately.
	if (kstrcmp(path_copy, "/") == 0 || path_copy[0] == '\0') {
		if (out_entry) {
			kmemset(out_entry, 0, sizeof(FAT32_DIR_ENTRY));
			out_entry->attr = ATTR_DIRECTORY;
			out_entry->fst_clus_lo = (uint16_t)(fs.root_cluster & 0xFFFF);
			out_entry->fst_clus_hi = (uint16_t)(fs.root_cluster >> 16);
		}
		if (out_parent_cluster) *out_parent_cluster = fs.root_cluster; // Root's parent is itself
		return true;
	}

	FAT32_DIR_ENTRY last_found_entry;
	kmemset(&last_found_entry, 0, sizeof(FAT32_DIR_ENTRY));
	bool any_token_found = false;

	char* token = kstrtok(path_copy, "/");

	while (token != NULL) {
		bool found_this_token = false;
		parent_cluster_of_last_found = current_cluster;

		void* sector_buf = MtAllocateVirtualMemory(512, 512);
		if (!sector_buf) return false;

		// Search the current directory (current_cluster) for the token
		do {
			uint32_t sector = first_sector_of_cluster(current_cluster);
			for (uint32_t i = 0; i < fs.sectors_per_cluster; i++) {
				if (!read_sector(sector + i, sector_buf)) { MtFreeVirtualMemory(sector_buf); return false; }

				FAT32_DIR_ENTRY* entries = (FAT32_DIR_ENTRY*)sector_buf;
				uint32_t num_entries = fs.bytes_per_sector / sizeof(FAT32_DIR_ENTRY);

				for (uint32_t j = 0; j < num_entries; ) {
					if (entries[j].name[0] == END_OF_DIRECTORY) goto next_cluster_search;
					if ((uint8_t)entries[j].name[0] == DELETED_DIR_ENTRY) { j++; continue; }

					char lfn_buf[MAX_LFN_LEN];
					uint32_t consumed = 0;
					FAT32_DIR_ENTRY* sfn = read_lfn(&entries[j], num_entries - j, lfn_buf, &consumed);

					if (sfn && kstrcmp(lfn_buf, token) == 0) {
						kmemcpy(&last_found_entry, sfn, sizeof(FAT32_DIR_ENTRY));
						found_this_token = true;

						// Move to the next level of the path
						current_cluster = (sfn->fst_clus_hi << 16) | sfn->fst_clus_lo;
						goto token_found_and_continue;
					}
					j += (consumed > 0) ? consumed : 1;
				}
			}
		next_cluster_search:
			current_cluster = fat32_read_fat(current_cluster);
		} while (current_cluster < FAT32_EOC_MIN);

	token_found_and_continue:
		// free sector_buf
		MtFreeVirtualMemory(sector_buf);
		if (!found_this_token) {
			return false; // Path component not found, so the whole path is invalid.
		}

		any_token_found = true;

		// Get the next part of the path
		token = kstrtok(NULL, "/");

		// If we found a component, but it's not a directory and there's more path to parse, it's an error.
		if (token != NULL && !(last_found_entry.attr & ATTR_DIRECTORY)) {
			return false;
		}
	}

	if (any_token_found) {
		if (out_entry) kmemcpy(out_entry, &last_found_entry, sizeof(FAT32_DIR_ENTRY));
		if (out_parent_cluster) *out_parent_cluster = parent_cluster_of_last_found;
		return true;
	}

	return false; // Should not be reached if path is not root, but good for safety.
}

static bool fat32_extend_directory(uint32_t dir_cluster) {
	uint32_t new_cluster = fat32_find_free_cluster();
	if (new_cluster == 0) return false;

	// Zero out the new cluster
	if (!zero_cluster(new_cluster)) {
		fat32_write_fat(new_cluster, FAT32_FREE_CLUSTER); // Free it back
		return false;
	}

	fat32_write_fat(new_cluster, FAT32_EOC_MAX); // Mark as new end of chain

	// Find the last cluster in the original chain and link it to the new one
	uint32_t current = dir_cluster;
	uint32_t next = 0;
	while ((next = fat32_read_fat(current)) < FAT32_EOC_MIN) {
		current = next;
	}

	return fat32_write_fat(current, new_cluster);
}

static bool fat32_find_free_dir_slots(uint32_t dir_cluster, uint32_t count, uint32_t* out_sector, uint32_t* out_entry_index) {
	uint32_t current_cluster = dir_cluster;
	void* sector_buf = MtAllocateVirtualMemory(512, 512);
	if (!sector_buf) return false;

	do {
		uint32_t sector_lba = first_sector_of_cluster(current_cluster);
		for (uint32_t i = 0; i < fs.sectors_per_cluster; i++) {
			if (!read_sector(sector_lba + i, sector_buf)) { MtFreeVirtualMemory(sector_buf); return false; }

			FAT32_DIR_ENTRY* entries = (FAT32_DIR_ENTRY*)sector_buf;
			uint32_t num_entries = fs.bytes_per_sector / sizeof(FAT32_DIR_ENTRY);

			uint32_t consecutive_free = 0;
			for (uint32_t j = 0; j < num_entries; j++) {
				uint8_t first_byte = (uint8_t)entries[j].name[0];
				if (first_byte == END_OF_DIRECTORY || first_byte == DELETED_DIR_ENTRY) {
					consecutive_free++;
					if (consecutive_free == count) {
						*out_sector = sector_lba + i;
						*out_entry_index = j - (count - 1);
						MtFreeVirtualMemory(sector_buf);
						return true;
					}
				}
				else {
					consecutive_free = 0;
				}
			}
		}

		uint32_t next_cluster = fat32_read_fat(current_cluster);
		if (next_cluster >= FAT32_EOC_MIN) {
			// End of chain, no space found, try to extend
			if (fat32_extend_directory(dir_cluster)) {
				// After extending, restart the search. A more optimized way exists,
				// but this is safer and simpler to implement.
				return fat32_find_free_dir_slots(dir_cluster, count, out_sector, out_entry_index);
			}
			else {
				MtFreeVirtualMemory(sector_buf);
				return false; // Cannot extend directory
			}
		}
		current_cluster = next_cluster;
	} while (true);

	MtFreeVirtualMemory(sector_buf);
	return false;
}

#define BPB_SECTOR_START 2048

// Read BPB (Bios Parameter Block) and initialize.
bool fat32_init(int disk_index) {
	tracelast_func("fat32_init");
	disk = get_block_device(disk_index);
	if (!disk) { return false; }

	void* buf = MtAllocateVirtualMemory(512, 512);
	if (!read_sector(BPB_SECTOR_START, buf)) { return false; } // First sector contains the BPB for FAT.
	kmemcpy(&bpb, buf, sizeof(bpb)); // So copy that first sector into our local BPB structure.

	// Then initialize it.
	fs.bytes_per_sector = bpb.bytes_per_sector;
	fs.sectors_per_cluster = bpb.sectors_per_cluster;
	fs.reserved_sector_count = bpb.reserved_sector_count;
	fs.sectors_per_fat = bpb.fat_size_32;
	fs.root_cluster = bpb.root_cluster;

	fs.fat_start = BPB_SECTOR_START + bpb.reserved_sector_count; // technically also reserved_sector_count of fs. holds it as well.
	fs.first_data_sector = fs.fat_start + bpb.num_fats * fs.sectors_per_fat;
	MtFreeVirtualMemory(buf);
	return true;
}

// Simple, strict compare: dir_name is on-disk 11 bytes, short_name is formatted 11 bytes
static bool cmp_short_name(const char* dir_name, const char short_name[11]) {
	for (int i = 0; i < 11; ++i) {
		if ((unsigned char)dir_name[i] != (unsigned char)short_name[i]) return false;
	}
	return true;
}

// Walk cluster chain and read directory entries.
void fat32_list_root(void) {
	tracelast_func("fat32_list_root");
	uint32_t cluster = fs.root_cluster;

	void* buf = MtAllocateVirtualMemory(512, 512);
	if (!buf) return;

	// Temp buffer to accumulate LFN entries (and eventually the 8.3 entry).
	FAT32_DIR_ENTRY temp_entries[MAX_LFN_ENTRIES + 1];
	uint32_t lfn_accum = 0;

	do {
		uint32_t sector = first_sector_of_cluster(cluster);
		for (uint32_t i = 0; i < fs.sectors_per_cluster; ++i) {
			if (!read_sector(sector + i, buf)) return;

			FAT32_DIR_ENTRY* dir = (FAT32_DIR_ENTRY*)buf;
			uint32_t entries = fs.bytes_per_sector / sizeof(*dir);

			for (uint32_t j = 0; j < entries; ++j, ++dir) {
				uint8_t first = (uint8_t)dir->name[0];

				// End of directory: stop everything
				if (first == 0x00) {
					MtFreeVirtualMemory(buf);
					return;
				}

				// Deleted entry: if we were accumulating an LFN chain, drop it.
				if (first == 0xE5) {
					lfn_accum = 0;
					continue;
				}

				// If it's an LFN entry, copy it into the temp accumulator (preserve order read-on-disk)
				if (dir->attr == ATTR_LONG_NAME) {
					if (lfn_accum < MAX_LFN_ENTRIES) {
						kmemcpy(&temp_entries[lfn_accum], dir, sizeof(FAT32_DIR_ENTRY));
						lfn_accum++;
					}
					else {
						// too many parts: drop accumulator to avoid overflow
						lfn_accum = 0;
					}
					continue;
				}
				// Non-LFN entry: this is the 8.3 entry that ends any preceding LFN chain (if present).
				// If we have accumulated LFN entries, build a contiguous array: [LFN...][8.3]
				char bufferLfn[MAX_LFN_LEN];
				uint32_t consumed = 0;
				FAT32_DIR_ENTRY* real = NULL;

				if (lfn_accum > 0) {
					// copy the 8.3 entry as the last element
					kmemcpy(&temp_entries[lfn_accum], dir, sizeof(FAT32_DIR_ENTRY));
					// call read_lfn on our temp buffer (which starts with LFN entries)
					real = read_lfn(temp_entries, lfn_accum + 1, bufferLfn, &consumed);
					// reset accumulator regardless (we've handled or attempted to)
					lfn_accum = 0;
				}
				else {
					// No accumulated LFN entries: handle short-name-only entry
					// We can call read_lfn directly on the sector buffer at current position
					uint32_t remaining = entries - j;
					real = read_lfn(&dir[0], remaining, bufferLfn, &consumed);
					// note: consumed will be at least 1 if successful
				}

				// If read_lfn returned a real 8.3 entry (it should), print the name
				if (real) {
					gop_printf(0xFF00FFFF, "Found: %s\n", bufferLfn);
				}
				else {
					// Fallback: if read_lfn failed for some reason, print raw 8.3 as a last resort
					char fallback[12];
					for (int k = 0; k < 11; ++k) fallback[k] = dir->name[k];
					fallback[11] = '\0';
					gop_printf(0xFF00FFFF, "Found (raw): %s\n", fallback);
				}

				// continue to next entry (we already advanced j by loop)
			} // for each dir entry in sector
		} // for each sector in cluster

		cluster = fat32_read_fat(cluster);
	} while (cluster < 0x0FFFFFF8);
}

// Helper to detect if a filename has a slash in it (/), and so the filename is in a directory
static bool is_filename_in_dir(const char* filename) {
	if (!filename) return false;

	while (*filename) {
		if (*filename == '/') return true;
		filename++;
	}

	return false;
}

static uint32_t extract_dir_cluster(const char* filename) {
	tracelast_func("extract_dir_cluster - fat32");

	if (!filename || filename[0] == '\0') return fs.root_cluster;

	// Make a mutable copy
	char path_copy[260];
	kstrcpy(path_copy, filename);

	// Remove trailing slashes (keep a single leading '/' if path is "/")
	int len = (int)kstrlen(path_copy);
	while (len > 1 && path_copy[len - 1] == '/') {
		path_copy[len - 1] = '\0';
		len--;
	}

	// Find last slash
	int last_slash = -1;
	for (int i = len - 1; i >= 0; --i) {
		if (path_copy[i] == '/') { last_slash = i; break; }
	}

	// No slash -> file is in root
	if (last_slash == -1) {
		return fs.root_cluster;
	}

	// Parent path is "/" if last_slash == 0, otherwise substring [0..last_slash-1]
	char parent[260];
	if (last_slash == 0) {
		parent[0] = '/';
		parent[1] = '\0';
	}
	else {
		// copy up to last_slash
		for (int i = 0; i < last_slash; ++i) parent[i] = path_copy[i];
		parent[last_slash] = '\0';
	}

	// Resolve the parent path to a directory entry
	FAT32_DIR_ENTRY parent_entry;
	if (!fat32_find_entry(parent, &parent_entry, NULL)) return 0;

	// Ensure it's a directory
	if (!(parent_entry.attr & ATTR_DIRECTORY)) return 0;

	uint32_t cluster = ((uint32_t)parent_entry.fst_clus_hi << 16) | parent_entry.fst_clus_lo;
	if (cluster == 0) cluster = fs.root_cluster; // safety fallback
	return cluster;
}

void* fat32_read_file(const char* filename, uint32_t* file_size_out) {
	tracelast_func("fat32_read_file_alloc");

	// We still need a temporary buffer for reading sectors
	void* sblk = MtAllocateVirtualMemory(512, 512);
	if (!sblk) return NULL;

	// Get the cluster of the directory filename points to (e.g "tmp/folder/myfile.txt", we need the "folder" cluster.)
	uint32_t cluster = 0;

	if (is_filename_in_dir(filename)) {
		cluster = extract_dir_cluster(filename);
		if (!cluster) {
			MtFreeVirtualMemory(sblk);
			return NULL;
		}
	}
	else {
		cluster = fs.root_cluster;
	}

	do {
		uint32_t sector = first_sector_of_cluster(cluster);
		for (uint32_t i = 0; i < fs.sectors_per_cluster; ++i) {
			if (!read_sector(sector + i, sblk)) {
				// Free sblk before returning
				MtFreeVirtualMemory(sblk);
				return NULL;
			}

			FAT32_DIR_ENTRY* dir_entries = (FAT32_DIR_ENTRY*)sblk;
			uint32_t entries_per_sector = fs.bytes_per_sector / sizeof(FAT32_DIR_ENTRY);

			for (uint32_t j = 0; j < entries_per_sector; ) {
				FAT32_DIR_ENTRY* current_entry = &dir_entries[j];

				if (current_entry->name[0] == END_OF_DIRECTORY) {
					// Free sblk
					MtFreeVirtualMemory(sblk);
					return NULL; // End of directory, file not found
				}
				if ((uint8_t)current_entry->name[0] == DELETED_DIR_ENTRY) {
					j++;
					continue;
				}

				char lfn_buf[MAX_LFN_LEN];
				uint32_t consumed_entries = 0;
				FAT32_DIR_ENTRY* sfn_entry = read_lfn(current_entry, entries_per_sector - j, lfn_buf, &consumed_entries);

				if (sfn_entry) {
					// Check if either the long or short filename matches
					if (kstrcmp(filename, lfn_buf) == 0) {
						goto file_found;
					}

					char shortname_formatted[11];
					format_short_name(filename, shortname_formatted);
					if (cmp_short_name(sfn_entry->name, shortname_formatted)) {
						goto file_found;
					}

					j += consumed_entries; // Skip past all LFN entries and the SFN entry
					continue;

				file_found:
					{
						uint32_t file_size = sfn_entry->file_size;
						if (file_size_out) {
							*file_size_out = file_size;
						}

						// Now allocate the final buffer for the file content
						void* file_buffer = MtAllocateVirtualMemory(file_size, 4096);
						if (!file_buffer) {
							// Free sblk
							MtFreeVirtualMemory(sblk);
							return NULL;
						}

						uint32_t file_cluster = (uint32_t)((sfn_entry->fst_clus_hi << 16) | sfn_entry->fst_clus_lo);
						uint32_t remaining_bytes = file_size;
						uint8_t* dst = (uint8_t*)file_buffer;

						while (file_cluster < FAT32_EOC_MIN && remaining_bytes > 0) {
							uint32_t current_sector = first_sector_of_cluster(file_cluster);
							for (uint32_t sc = 0; sc < fs.sectors_per_cluster && remaining_bytes > 0; ++sc) {
								if (!read_sector(current_sector + sc, sblk)) {
									// Free both buffers
									MtFreeVirtualMemory(file_buffer);
									MtFreeVirtualMemory(sblk);
									return NULL;
								}

								uint32_t bytes_to_copy = fs.bytes_per_sector;
								if (bytes_to_copy > remaining_bytes) {
									bytes_to_copy = remaining_bytes;
								}

								kmemcpy(dst, sblk, bytes_to_copy);
								dst += bytes_to_copy;
								remaining_bytes -= bytes_to_copy;
							}
							file_cluster = fat32_read_fat(file_cluster);
						}

						// Free the temporary sector buffer and return the file buffer
						MtFreeVirtualMemory(sblk);
						return file_buffer;
					}
				}
				else {
					j++; // Move to the next entry if read_lfn fails
				}
			}
		}
		cluster = fat32_read_fat(cluster);
	} while (cluster < FAT32_EOC_MIN);

	// Free sblk
	MtFreeVirtualMemory(sblk);
	return NULL; // File not found after searching the entire directory
}

bool fat32_create_directory(const char* path) {
	tracelast_func("fat32_create_directory_full");

	// 1. Check if an entry already exists at this path
	if (fat32_find_entry(path, NULL, NULL)) {
#ifdef DEBUG
		gop_printf(0xFFFF0000, "Error: Path '%s' already exists.\n", path);
#endif
		return false;
	}

	// 2. Separate parent path and new directory name
	char path_copy[260];
	kstrcpy(path_copy, path);
	char* new_dir_name = NULL;
	char* parent_path = "/";
	int last_slash = -1;
	for (int i = 0; path_copy[i] != '\0'; i++) {
		if (path_copy[i] == '/') last_slash = i;
	}
	if (last_slash != -1) {
		new_dir_name = &path_copy[last_slash + 1];
		if (last_slash > 0) {
			path_copy[last_slash] = '\0';
			parent_path = path_copy;
		}
	}
	else {
		new_dir_name = path_copy;
	}

	// 3. Find the parent directory cluster
	FAT32_DIR_ENTRY parent_entry;
	uint32_t parent_cluster;
	if (!fat32_find_entry(parent_path, &parent_entry, NULL)) {
#ifdef DEBUG
		gop_printf(0xFFFF0000, "Error: Parent path '%s' not found.\n", parent_path);
#endif
		return false;
	}
	if (!(parent_entry.attr & ATTR_DIRECTORY)) {
#ifdef DEBUG
		gop_printf(0xFFFF0000, "Error: Parent path is not a directory.\n", parent_path);
#endif
		return false;
	}
	parent_cluster = (parent_entry.fst_clus_hi << 16) | parent_entry.fst_clus_lo;

	// 4. Allocate a new cluster for this directory's contents
	uint32_t new_cluster = fat32_find_free_cluster();
	if (new_cluster == 0) return false;

	fat32_write_fat(new_cluster, FAT32_EOC_MAX);
	zero_cluster(new_cluster);

	// 5. Create '.' and '..' entries in the new cluster
	void* sector_buf = MtAllocateVirtualMemory(512, 512);
	if (!sector_buf) { /* handle error */ return false; }
	kmemset(sector_buf, 0, 512);
	FAT32_DIR_ENTRY* dot_entries = (FAT32_DIR_ENTRY*)sector_buf;

	kmemcpy(dot_entries[0].name, ".          ", 11);
	dot_entries[0].attr = ATTR_DIRECTORY;
	dot_entries[0].fst_clus_lo = (uint16_t)new_cluster;
	dot_entries[0].fst_clus_hi = (uint16_t)(new_cluster >> 16);

	kmemcpy(dot_entries[1].name, "..         ", 11);
	dot_entries[1].attr = ATTR_DIRECTORY;
	dot_entries[1].fst_clus_lo = (uint16_t)parent_cluster;
	dot_entries[1].fst_clus_hi = (uint16_t)(parent_cluster >> 16);

	write_sector(first_sector_of_cluster(new_cluster), sector_buf);

	// 6. Create the entry in the parent directory
	// For simplicity, we'll use a simple SFN. A full implementation needs `fat32_generate_sfn`.
	char sfn[11];
	format_short_name(new_dir_name, sfn); // Using your existing simple formatter

	uint32_t entry_sector, entry_index;
	if (!fat32_find_free_dir_slots(parent_cluster, 1, &entry_sector, &entry_index)) {
		// free sector_buf, free cluster...
		MtFreeVirtualMemory(sector_buf);
		fat32_write_fat(new_cluster, FAT32_FREE_CLUSTER);
		return false;
	}

	// Read the target sector, modify it, write it back
	if (!read_sector(entry_sector, sector_buf)) { MtFreeVirtualMemory(sector_buf); return false; }

	FAT32_DIR_ENTRY* new_entry = &((FAT32_DIR_ENTRY*)sector_buf)[entry_index];
	kmemset(new_entry, 0, sizeof(FAT32_DIR_ENTRY));
	kmemcpy(new_entry->name, sfn, 11);
	new_entry->attr = ATTR_DIRECTORY;
	new_entry->fst_clus_lo = (uint16_t)new_cluster;
	new_entry->fst_clus_hi = (uint16_t)(new_cluster >> 16);

	bool success = write_sector(entry_sector, sector_buf);

	// free sector_buf
	MtFreeVirtualMemory(sector_buf);
	return success;
}

static uint32_t fat32_create_lfn_entries(FAT32_DIR_ENTRY* entry_buffer, const char* long_name, uint8_t sfn_checksum) {
	uint32_t len = kstrlen(long_name);
	uint32_t num_lfn_entries = (len + 12) / 13;

	// Fill LFN entries backwards
	for (uint32_t i = 0; i < num_lfn_entries; i++) {
		FAT32_DIR_ENTRY* lfn = &entry_buffer[i];
		uint32_t lfn_seq = (num_lfn_entries - i);
		if (i == 0) lfn_seq |= 0x40; // Last LFN entry marker

		lfn->attr = ATTR_LONG_NAME;
		lfn->nt_res = 0;
		lfn->crt_time_tenth = sfn_checksum; // This field holds the checksum
		lfn->fst_clus_lo = 0;

		// This is the sequence and type field in disguise
		*(uint8_t*)(&lfn->name[0]) = lfn_seq;

		// Character positions for LFN entry
		// pointers into the entry (UTF-16 wide chars)
		uint8_t* ebytes = (uint8_t*)lfn;
		uint16_t* name1_map = (uint16_t*)(ebytes + 1);   // 5 chars
		uint16_t* name2_map = (uint16_t*)(ebytes + 14);  // 6 chars
		uint16_t* name3_map = (uint16_t*)(ebytes + 28);  // 2 chars

		// Populate with UTF-16 characters
		uint32_t char_idx = (num_lfn_entries - 1 - i) * 13;
		for (int k = 0; k < 13; k++) {
			uint16_t uchar = 0xFFFF; // Fill with 0xFFFF
			if (char_idx < len) {
				uchar = long_name[char_idx];
			}
			else if (char_idx == len) {
				uchar = 0x0000; // Null terminator
			}

			if (k < 5)       ((uint16_t*)name1_map)[k] = uchar;
			else if (k < 11) ((uint16_t*)name2_map)[k - 5] = uchar;
			else             ((uint16_t*)name3_map)[k - 11] = uchar;

			char_idx++;
		}
	}
	return num_lfn_entries;
}


bool fat32_write_file(const char* path, const void* data, uint32_t size) {
	tracelast_func("fat32_write_file_full");

	// Steps 1 & 2: Same as create_directory to find parent path and new filename
	if (fat32_find_entry(path, NULL, NULL)) { return false; } // Exists
	char path_copy[260];
	kstrcpy(path_copy, path);
	char* filename = NULL;
	char* parent_path = "/";
	int last_slash = -1;
	for (int i = 0; path_copy[i] != '\0'; i++) { if (path_copy[i] == '/') last_slash = i; }
	if (last_slash != -1) {
		filename = &path_copy[last_slash + 1];
		if (last_slash > 0) { path_copy[last_slash] = '\0'; parent_path = path_copy; }
	}
	else { filename = path_copy; }

	// Step 3: Find parent directory cluster
	FAT32_DIR_ENTRY parent_entry;
	if (!fat32_find_entry(parent_path, &parent_entry, NULL) || !(parent_entry.attr & ATTR_DIRECTORY)) { return false; }
	uint32_t parent_cluster = (parent_entry.fst_clus_hi << 16) | parent_entry.fst_clus_lo;

	// Step 4: Allocate clusters for file data
	uint32_t first_cluster = 0;
	if (size > 0) {
		uint32_t cluster_size = fs.sectors_per_cluster * fs.bytes_per_sector;
		uint32_t clusters_needed = (size + cluster_size - 1) / cluster_size;
		uint32_t prev_cluster = 0;

		for (uint32_t i = 0; i < clusters_needed; i++) {
			uint32_t new_cluster = fat32_find_free_cluster();
			if (new_cluster == 0) { return false; }
			if (i == 0) first_cluster = new_cluster; else fat32_write_fat(prev_cluster, new_cluster);
			prev_cluster = new_cluster;
		}
		fat32_write_fat(prev_cluster, FAT32_EOC_MAX);
	}

	// Step 5: Write data to the allocated clusters
	if (size > 0) {
		uint32_t bytes_left = size;
		const uint8_t* data_ptr = (const uint8_t*)data;
		uint32_t current_cluster = first_cluster;
		void* sector_buf = MtAllocateVirtualMemory(512, 512);
		if (!sector_buf) { /* Error handling */ return false; }

		while (bytes_left > 0 && current_cluster < FAT32_EOC_MIN) {
			uint32_t sector_lba = first_sector_of_cluster(current_cluster);
			for (uint32_t i = 0; i < fs.sectors_per_cluster; i++) {
				uint32_t to_write = (bytes_left > 512) ? 512 : bytes_left;
				if (to_write == 0) break;
				kmemcpy(sector_buf, data_ptr, to_write);
				if (to_write < 512) kmemset((uint8_t*)sector_buf + to_write, 0, 512 - to_write);

				write_sector(sector_lba + i, sector_buf);
				data_ptr += to_write;
				bytes_left -= to_write;
			}
			current_cluster = fat32_read_fat(current_cluster);
		}
		// free sector_buf
		MtFreeVirtualMemory(sector_buf);
	}

	// Step 6: Create and write directory entries (LFN + SFN)
	char sfn[11];
	format_short_name(filename, sfn); // Again, using simple SFN for now
	uint8_t checksum = lfn_checksum((uint8_t*)sfn);

	uint32_t lfn_count = (kstrlen(filename) + 12) / 13;
	uint32_t total_entries = lfn_count + 1;
	FAT32_DIR_ENTRY entry_buf[total_entries];

	fat32_create_lfn_entries(entry_buf, filename, checksum);

	// Create the SFN entry at the end of the buffer
	FAT32_DIR_ENTRY* sfn_entry = &entry_buf[lfn_count];
	kmemset(sfn_entry, 0, sizeof(FAT32_DIR_ENTRY));
	kmemcpy(sfn_entry->name, sfn, 11);
	sfn_entry->attr = 0; // Not a directory, archive bit can be set.
	sfn_entry->file_size = size;
	sfn_entry->fst_clus_lo = (uint16_t)first_cluster;
	sfn_entry->fst_clus_hi = (uint16_t)(first_cluster >> 16);

	// Find space and write the entries
	uint32_t entry_sector, entry_index;
	if (!fat32_find_free_dir_slots(parent_cluster, total_entries, &entry_sector, &entry_index)) {
		// cleanup clusters...
		return false;
	}

	void* write_buf = MtAllocateVirtualMemory(512, 512);
	read_sector(entry_sector, write_buf);
	kmemcpy(&((FAT32_DIR_ENTRY*)write_buf)[entry_index], entry_buf, total_entries * sizeof(FAT32_DIR_ENTRY));
	bool success = write_sector(entry_sector, write_buf);

	// free write_buf
	MtFreeVirtualMemory(write_buf);
	return success;
}

void fat32_list_directory(const char* path) {
	tracelast_func("fat32_list_directory");

	// 1. Find the directory entry for the given path to get its starting cluster.
	FAT32_DIR_ENTRY dir_entry;
	if (!fat32_find_entry(path, &dir_entry, NULL) || !(dir_entry.attr & ATTR_DIRECTORY)) {
		gop_printf(0xFFFF0000, "Error: Directory not found or path is not a directory: %s\n", path);
		return;
	}

	uint32_t cluster = (uint32_t)((dir_entry.fst_clus_hi << 16) | dir_entry.fst_clus_lo);
	if (cluster == 0) { // Root directory special case on some FAT16/12, but for FAT32 it should be root_cluster.
		cluster = fs.root_cluster;
	}

	gop_printf(0xFF00FFFF, "Listing of '%s':\n", path);

	void* buf = MtAllocateVirtualMemory(512, 512);
	if (!buf) return;

	// 2. The rest of the logic is the same as fat32_list_root, just starting from `cluster`.
	do {
		uint32_t sector = first_sector_of_cluster(cluster);
		for (uint32_t i = 0; i < fs.sectors_per_cluster; ++i) {
			if (!read_sector(sector + i, buf)) { MtFreeVirtualMemory(buf); return; }

			FAT32_DIR_ENTRY* dir = (FAT32_DIR_ENTRY*)buf;
			uint32_t entries = fs.bytes_per_sector / sizeof(*dir);

			for (uint32_t j = 0; j < entries; ) {
				FAT32_DIR_ENTRY* current_entry = &dir[j];

				if (current_entry->name[0] == END_OF_DIRECTORY) {
					MtFreeVirtualMemory(buf);
					return;
				}
				// Skip deleted, '.', and '..' entries from the listing
				if ((uint8_t)current_entry->name[0] == DELETED_DIR_ENTRY || current_entry->name[0] == '.') {
					j++;
					continue;
				}

				char lfn_name[MAX_LFN_LEN];
				uint32_t consumed = 0;
				FAT32_DIR_ENTRY* sfn_entry = read_lfn(current_entry, entries - j, lfn_name, &consumed);

				if (sfn_entry) {
					if (sfn_entry->attr & ATTR_DIRECTORY) {
						gop_printf(0xFF00FFFF, "  <DIR>  %s\n", lfn_name);
					}
					else {
						gop_printf(0xFFFFFFFF, "         %s   (%u bytes)\n", lfn_name, sfn_entry->file_size);
					}
					j += consumed;
				}
				else {
					j++; // Move to the next entry if read_lfn fails
				}
			}
		}
		cluster = fat32_read_fat(cluster);
	} while (cluster < FAT32_EOC_MIN);

	MtFreeVirtualMemory(buf);
}
