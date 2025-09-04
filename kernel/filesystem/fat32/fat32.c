/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     FAT32 FileSystem Implementation.
 */

#include "fat32.h"
#include "../../drivers/blk/block.h"
#include "../../assert.h"

#define WRITE_MODE_APPEND_EXISTING 0
#define WRITE_MODE_CREATE_OR_REPLACE 1

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
static MTSTATUS read_sector(uint32_t lba, void* buf) {
	tracelast_func("read_sector - fat32");
	return disk->read_sector(disk, lba, buf);
}

// Write to sector from buffer
static MTSTATUS write_sector(uint32_t lba, const void* buf) {
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
		// Convert 11-byte SFN to human readable name "NAME.EXT"
		const unsigned char* s = (const unsigned char*)short_entry->name;
		int pos = 0;

		// name part (0..7)
		for (int n = 0; n < 8; ++n) {
			if (s[n] == ' ') break;
			if (pos < (int)MAX_LFN_LEN - 1) out_name[pos++] = s[n];
		}

		// extension (8..10)
		bool has_ext = false;
		for (int n = 8; n < 11; ++n) if (s[n] != ' ') { has_ext = true; break; }
		if (has_ext) {
			if (pos < (int)MAX_LFN_LEN - 1) out_name[pos++] = '.';
			for (int n = 8; n < 11; ++n) {
				if (s[n] == ' ') break;
				if (pos < (int)MAX_LFN_LEN - 1) out_name[pos++] = s[n];
			}
		}

		out_name[pos] = '\0';
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
	MTSTATUS status;
	uint32_t fat_offset = cluster * 4;
	uint32_t fat_sector = fs.fat_start + (fat_offset / fs.bytes_per_sector);
	uint32_t ent_offset = fat_offset % fs.bytes_per_sector;

	void* buf = MtAllocateVirtualMemory(512, 512);
	if (!buf) return 0x0FFFFFFF; // treat as EOF on allocation failure
	status = read_sector(fat_sector, buf);
	if (MT_FAILURE(status)) {
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
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector_base = fs.fat_start; // first FAT start
    uint32_t ent_offset = fat_offset % fs.bytes_per_sector;
    uint32_t sec_index = fat_offset / fs.bytes_per_sector;

    void* buf = MtAllocateVirtualMemory(512, 512);
    if (!buf) return false;
	MTSTATUS status;
    bool ok = true;
    for (uint32_t fat_i = 0; fat_i < bpb.num_fats; ++fat_i) {
        uint32_t fat_sector = fat_sector_base + fat_i * fs.sectors_per_fat + sec_index;
		status = read_sector(fat_sector, buf);
        if (MT_FAILURE(status)) { ok = false; break; }

        uint32_t* fat_entry = (uint32_t*)((uint8_t*)buf + ent_offset);
        *fat_entry = (*fat_entry & 0xF0000000) | (value & 0x0FFFFFFF);
		status = write_sector(fat_sector, buf);
        if (MT_FAILURE(status)) { ok = false; break; }
    }

    MtFreeVirtualMemory(buf);
    return ok;
}


static inline uint32_t get_dir_cluster(FAT32_DIR_ENTRY* entry) {
	return ((uint32_t)entry->fst_clus_hi << 16) | entry->fst_clus_lo;
}

// Free a cluster chain starting at start_cluster (set each entry to FREE)
static bool fat32_free_cluster_chain(uint32_t start_cluster) {
	tracelast_func("fat32_free_cluster_chain");
	if (start_cluster < 2 || start_cluster >= FAT32_EOC_MIN) return false;

	uint32_t cur = start_cluster;
	while (cur < FAT32_EOC_MIN) {
		uint32_t next = fat32_read_fat(cur);
		// mark current as free
		if (!fat32_write_fat(cur, FAT32_FREE_CLUSTER)) return false;
		// protect against pathological loops
		if (next == cur) break;
		cur = next;
	}
	return true;
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
	MTSTATUS status;
	uint32_t sector = first_sector_of_cluster(cluster);
	for (uint32_t i = 0; i < fs.sectors_per_cluster; i++) {
		status = write_sector(sector + i, buf);
		if (MT_FAILURE(status)) {
			success = false;
			break;
		}
	}

	MtFreeVirtualMemory(buf);
	return success;
}

// Simple, strict compare: dir_name is on-disk 11 bytes, short_name is formatted 11 bytes
static bool cmp_short_name(const char* dir_name, const char short_name[11]) {
	for (int i = 0; i < 11; ++i) {
		if ((unsigned char)dir_name[i] != (unsigned char)short_name[i]) return false;
	}
	return true;
}

// ASCII case-insensitive compare
static inline bool ci_equal(const char* a, const char* b) {
	size_t la = kstrlen(a);
	size_t lb = kstrlen(b);
	if (la != lb) return false;
	for (size_t i = 0; i < la; ++i) {
		if ((char)toupper((int)a[i]) != (char)toupper((int)b[i])) return false;
	}
	return true;
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
	MTSTATUS status;
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
				status = read_sector(sector + i, sector_buf);
				if (MT_FAILURE(status)) { MtFreeVirtualMemory(sector_buf); return false; }

				FAT32_DIR_ENTRY* entries = (FAT32_DIR_ENTRY*)sector_buf;
				uint32_t num_entries = fs.bytes_per_sector / sizeof(FAT32_DIR_ENTRY);

				for (uint32_t j = 0; j < num_entries; ) {
					if (entries[j].name[0] == END_OF_DIRECTORY) goto next_cluster_search;
					if ((uint8_t)entries[j].name[0] == DELETED_DIR_ENTRY) { j++; continue; }

					char lfn_buf[MAX_LFN_LEN];
					uint32_t consumed = 0;
					FAT32_DIR_ENTRY* sfn = read_lfn(&entries[j], num_entries - j, lfn_buf, &consumed);

					if (sfn) {
						bool match = false;

						// 1) exact LFN match
						if (kstrcmp(lfn_buf, token) == 0) match = true;

						// 2) case-insensitive LFN match
						if (!match) {
							size_t la = kstrlen(lfn_buf);
							size_t lb = kstrlen(token);
							if (la == lb) {
								bool eq = true;
								for (size_t z = 0; z < la; ++z) {
									char ca = lfn_buf[z];
									char cb = token[z];
									if (ca >= 'a' && ca <= 'z') ca -= ('a' - 'A');
									if (cb >= 'a' && cb <= 'z') cb -= ('a' - 'A');
									if (ca != cb) { eq = false; break; }
								}
								if (eq) match = true;
							}
						}

						// 3) SFN compare (format token to 11-byte SFN and compare bytes)
						if (!match) {
							char token_sfn[11];
							format_short_name(token, token_sfn);
							if (cmp_short_name(sfn->name, token_sfn)) match = true;
						}

						if (match) {
							kmemcpy(&last_found_entry, sfn, sizeof(FAT32_DIR_ENTRY));
							found_this_token = true;
							current_cluster = (sfn->fst_clus_hi << 16) | sfn->fst_clus_lo;
							goto token_found_and_continue;
						}
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
	MTSTATUS status;
	do {
		uint32_t sector_lba = first_sector_of_cluster(current_cluster);
		for (uint32_t i = 0; i < fs.sectors_per_cluster; i++) {
			status = read_sector(sector_lba + i, sector_buf);
			if (MT_FAILURE(status)) { MtFreeVirtualMemory(sector_buf); return false; }

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
MTSTATUS fat32_init(int disk_index) {
	tracelast_func("fat32_init");
	MTSTATUS status;
	disk = get_block_device(disk_index);
	if (!disk) { return MT_GENERAL_FAILURE; }

	void* buf = MtAllocateVirtualMemory(512, 512);
	if (!buf) return MT_NO_MEMORY;
	status = read_sector(BPB_SECTOR_START, buf);
	if (MT_FAILURE(status)) { return status; } // First sector contains the BPB for FAT.
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
	return MT_SUCCESS;
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
	MTSTATUS status;
	do {
		uint32_t sector = first_sector_of_cluster(cluster);
		for (uint32_t i = 0; i < fs.sectors_per_cluster; ++i) {
			status = read_sector(sector + i, buf);
			if (MT_FAILURE(status)) return;

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

MTSTATUS fat32_read_file(const char* filename, uint32_t* file_size_out, void** buffer_out) {
	tracelast_func("fat32_read_file");
	MTSTATUS status;
	// We still need a temporary buffer for reading sectors
	void* sblk = MtAllocateVirtualMemory(512, 512);
	if (!sblk) return MT_NO_MEMORY;

	// Get the cluster of the directory filename points to (e.g "tmp/folder/myfile.txt", we need the "folder" cluster.)
	uint32_t cluster = 0;

	if (is_filename_in_dir(filename)) {
		cluster = extract_dir_cluster(filename);
		if (!cluster) {
			MtFreeVirtualMemory(sblk);
			return MT_FAT32_INVALID_CLUSTER;
		}
	}
	else {
		cluster = fs.root_cluster;
	}

	do {
		uint32_t sector = first_sector_of_cluster(cluster);
		for (uint32_t i = 0; i < fs.sectors_per_cluster; ++i) {
			status = read_sector(sector + i, sblk);
			if (MT_FAILURE(status)) {
				// Free sblk before returning
				MtFreeVirtualMemory(sblk);
				return status;
			}

			FAT32_DIR_ENTRY* dir_entries = (FAT32_DIR_ENTRY*)sblk;
			uint32_t entries_per_sector = fs.bytes_per_sector / sizeof(FAT32_DIR_ENTRY);

			for (uint32_t j = 0; j < entries_per_sector; ) {
				FAT32_DIR_ENTRY* current_entry = &dir_entries[j];

				if (current_entry->name[0] == END_OF_DIRECTORY) {
					// Free sblk
					MtFreeVirtualMemory(sblk);
					return MT_FAT32_FILE_NOT_FOUND; // End of directory, file not found
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
							return MT_NO_MEMORY;
						}

						uint32_t file_cluster = (uint32_t)((sfn_entry->fst_clus_hi << 16) | sfn_entry->fst_clus_lo);
						uint32_t remaining_bytes = file_size;
						uint8_t* dst = (uint8_t*)file_buffer;

						while (file_cluster < FAT32_EOC_MIN && remaining_bytes > 0) {
							uint32_t current_sector = first_sector_of_cluster(file_cluster);
							for (uint32_t sc = 0; sc < fs.sectors_per_cluster && remaining_bytes > 0; ++sc) {
								status = read_sector(current_sector + sc, sblk);
								if (MT_FAILURE(status)) {
									// Free both buffers
									MtFreeVirtualMemory(file_buffer);
									MtFreeVirtualMemory(sblk);
									return status;
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
						*buffer_out = file_buffer;
						return MT_SUCCESS;
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
	return MT_FAT32_FILE_NOT_FOUND; // File not found after searching the entire directory
}

MTSTATUS fat32_create_directory(const char* path) {
	tracelast_func("fat32_create_directory_full");
	// 1. Check if an entry already exists at this path
	if (fat32_find_entry(path, NULL, NULL)) {
#ifdef DEBUG
		gop_printf(0xFFFF0000, "Error: Path '%s' already exists.\n", path);
#endif
		return MT_FAT32_DIRECTORY_ALREADY_EXISTS;
	}
	MTSTATUS status;
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
		return MT_FAT32_PARENT_PATH_NOT_FOUND;
	}
	if (!(parent_entry.attr & ATTR_DIRECTORY)) {
#ifdef DEBUG
		gop_printf(0xFFFF0000, "Error: Parent path is not a directory.\n", parent_path);
#endif
		return MT_FAT32_PARENT_PATH_NOT_DIR;
	}
	parent_cluster = (parent_entry.fst_clus_hi << 16) | parent_entry.fst_clus_lo;

	// 4. Allocate a new cluster for this directory's contents
	uint32_t new_cluster = fat32_find_free_cluster();
	if (new_cluster == 0) return MT_FAT32_CLUSTERS_FULL;

	fat32_write_fat(new_cluster, FAT32_EOC_MAX);
	zero_cluster(new_cluster);

	// 5. Create '.' and '..' entries in the new cluster
	void* sector_buf = MtAllocateVirtualMemory(512, 512);
	if (!sector_buf) { /* handle error */ return MT_MEMORY_LIMIT; }
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
		return MT_FAT32_DIR_FULL;
	}

	// Read the target sector, modify it, write it back
	status = read_sector(entry_sector, sector_buf);
	if (MT_FAILURE(status)) { MtFreeVirtualMemory(sector_buf); return status; }

	FAT32_DIR_ENTRY* new_entry = &((FAT32_DIR_ENTRY*)sector_buf)[entry_index];
	kmemset(new_entry, 0, sizeof(FAT32_DIR_ENTRY));
	kmemcpy(new_entry->name, sfn, 11);
	new_entry->attr = ATTR_DIRECTORY;
	new_entry->fst_clus_lo = (uint16_t)new_cluster;
	new_entry->fst_clus_hi = (uint16_t)(new_cluster >> 16);

	status = write_sector(entry_sector, sector_buf);

	// free sector_buf
	MtFreeVirtualMemory(sector_buf);
	return status;
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

MTSTATUS fat32_write_file(const char* path, const void* data, uint32_t size, uint32_t mode) {
	tracelast_func("fat32_write_file_full");
	// Safety check.
	if (mode != WRITE_MODE_CREATE_OR_REPLACE && mode != WRITE_MODE_APPEND_EXISTING) {
		return MT_FAT32_INVALID_WRITE_MODE;
	}
	MTSTATUS status;
	uint32_t first_cluster = 0;
	// parse parent + filename.
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
	if (!fat32_find_entry(parent_path, &parent_entry, NULL) || !(parent_entry.attr & ATTR_DIRECTORY)) { return MT_FAT32_CLUSTER_NOT_FOUND; }
	uint32_t parent_cluster = (parent_entry.fst_clus_hi << 16) | parent_entry.fst_clus_lo;

	// locate existing entry if any
	FAT32_DIR_ENTRY existing_entry;
	bool exists = fat32_find_entry(path, &existing_entry, NULL);

	// Helper: locate on-disk sector + index for the entry (so we can update SFN/LFN in-place)
	uint32_t located_sector = 0;
	uint32_t located_index = 0;
	uint32_t located_consumed = 0;
	bool located = false;

	{
		void* buf = MtAllocateVirtualMemory(512, 512);
		if (!buf) return MT_NO_MEMORY;
		uint32_t cluster = parent_cluster;
		do {
			uint32_t sector_lba = first_sector_of_cluster(cluster);
			for (uint32_t s = 0; s < fs.sectors_per_cluster; ++s) {
				status = read_sector(sector_lba + s, buf);
				if (MT_FAILURE(status)) { MtFreeVirtualMemory(buf); return status; }
				FAT32_DIR_ENTRY* entries = (FAT32_DIR_ENTRY*)buf;
				uint32_t entries_per_sector = fs.bytes_per_sector / sizeof(FAT32_DIR_ENTRY);

				for (uint32_t j = 0; j < entries_per_sector; ) {
					uint8_t first = (uint8_t)entries[j].name[0];
					if (first == END_OF_DIRECTORY) { MtFreeVirtualMemory(buf); goto locate_done; }
					if (first == DELETED_DIR_ENTRY) { j++; continue; }

					char lfn_buf[MAX_LFN_LEN];
					uint32_t consumed = 0;
					FAT32_DIR_ENTRY* sfn = read_lfn(&entries[j], entries_per_sector - j, lfn_buf, &consumed);
					if (sfn) {
						bool match = false;
						// exact LFN
						if (kstrcmp(lfn_buf, filename) == 0) match = true;
						// case-insensitive LFN
						if (!match && ci_equal(lfn_buf, filename)) match = true;
						// SFN compare
						if (!match) {
							char token_sfn[11];
							format_short_name(filename, token_sfn);
							if (cmp_short_name(sfn->name, token_sfn)) match = true;
						}
						if (match) {
							located_sector = sector_lba + s;
							located_index = j;
							located_consumed = consumed;
							located = true;
							MtFreeVirtualMemory(buf);
							goto locate_done;
						}
						j += consumed;
						continue;
					}
					else {
						j++;
					}
				}
			}
			cluster = fat32_read_fat(cluster);
		} while (cluster < FAT32_EOC_MIN);
		MtFreeVirtualMemory(buf);
	}
locate_done:

	// 4) Allocate clusters for file data 
	if (exists) first_cluster = (existing_entry.fst_clus_hi << 16) | existing_entry.fst_clus_lo;

	// Behavior:
	// - CREATE_OR_OVERWRITE: if file exists -> free its clusters; treat as new file
	// - APPEND: if file exists -> append to its chain; otherwise behave like create
	if (mode == WRITE_MODE_CREATE_OR_REPLACE) {
		// If exists, free existing cluster chain and reset first_cluster
		if (exists && first_cluster >= 2) {
			if (!fat32_free_cluster_chain(first_cluster)) {
				return MT_FAT32_INVALID_CLUSTER;
			}
		}
		first_cluster = 0;
	}
	// For append, we'll compute writing start later.

	/* Step 5: Write data to the allocated clusters */
	// If size > 0 allocate clusters and write data. Handle append partial-last-cluster case.
	if (size > 0) {
		uint32_t cluster_size = fs.sectors_per_cluster * fs.bytes_per_sector;
		uint32_t clusters_needed = 0;
		uint32_t prev_cluster = 0;
		uint32_t last_cluster = 0;
		uint32_t append_offset = 0;

		if (mode == WRITE_MODE_APPEND_EXISTING && exists && first_cluster != 0) {
			// find last cluster of existing file and offset
			uint32_t cur = first_cluster;
			uint32_t next = cur;
			uint32_t file_size = existing_entry.file_size;
			if (file_size == 0) {
				last_cluster = 0;
				append_offset = 0;
			}
			else {
				// reach last cluster
				while (cur < FAT32_EOC_MIN) {
					next = fat32_read_fat(cur);
					if (next >= FAT32_EOC_MIN) { last_cluster = cur; break; }
					cur = next;
				}
				append_offset = existing_entry.file_size % cluster_size;
			}
		}

		// compute clusters needed (account for partial last cluster when appending)
		if (mode == WRITE_MODE_APPEND_EXISTING && exists && append_offset > 0) {
			uint32_t bytes_fit = cluster_size - append_offset;
			if (size <= bytes_fit) clusters_needed = 0;
			else clusters_needed = (size - bytes_fit + cluster_size - 1) / cluster_size;
		}
		else {
			clusters_needed = (size + cluster_size - 1) / cluster_size;
			// if appending into empty file that had no clusters, clusters_needed is as above
		}

		// allocate required clusters
		uint32_t first_new = 0;
		prev_cluster = 0;
		for (uint32_t i = 0; i < clusters_needed; ++i) {
			uint32_t nc = fat32_find_free_cluster();
			if (nc == 0) {
				// cleanup on failure
				if (first_new) fat32_free_cluster_chain(first_new);
				return MT_FAT32_CLUSTERS_FULL;
			}
			if (!zero_cluster(nc)) {
				// free and abort
				fat32_write_fat(nc, FAT32_FREE_CLUSTER);
				if (first_new) fat32_free_cluster_chain(first_new);
				return MT_FAT32_CLUSTER_GENERAL_FAILURE;
			}
			if (first_new == 0) first_new = nc;
			if (prev_cluster != 0) {
				if (!fat32_write_fat(prev_cluster, nc)) {
					// cleanup
					if (first_new) fat32_free_cluster_chain(first_new);
					return MT_FAT32_CLUSTER_GENERAL_FAILURE;
				}
			}
			prev_cluster = nc;
		}
		if (prev_cluster != 0) fat32_write_fat(prev_cluster, FAT32_EOC_MAX);

		// Link newly allocated clusters into file chain
		if (mode == WRITE_MODE_APPEND_EXISTING && exists) {
			if (first_new != 0) {
				if (last_cluster == 0) {
					// file had no cluster, new allocation becomes first
					first_cluster = first_new;
				}
				else {
					// link last_cluster -> first_new
					if (!fat32_write_fat(last_cluster, first_new)) {
						if (first_new) fat32_free_cluster_chain(first_new);
						return MT_FAT32_CLUSTER_GENERAL_FAILURE;
					}
				}
			}
			// else nothing newly allocated, file continues as-is
		}
		else {
			// create/overwrite or new file -> new chain starts at first_new
			if (first_new != 0) first_cluster = first_new;
		}

		// Now write bytes to cluster chain
		void* sector_buf = MtAllocateVirtualMemory(512, 512);
		if (!sector_buf) {
			// cleanup allocated clusters on error
			if (mode != WRITE_MODE_APPEND_EXISTING || !exists) {
				if (first_cluster) fat32_free_cluster_chain(first_cluster);
			}
			return MT_NO_MEMORY;
		}

		const uint8_t* src = (const uint8_t*)data;
		uint32_t bytes_left = size;

		// If appending into an existing partial last cluster, fill its tail first
		uint32_t cur_cluster = 0;
		uint32_t write_cluster = 0;
		uint32_t write_offset = 0;

		if (mode == WRITE_MODE_APPEND_EXISTING && exists && append_offset > 0) {
			write_cluster = last_cluster;
			write_offset = append_offset;
		}
		else {
			write_cluster = first_cluster;
			write_offset = 0;
		}

		// If write_cluster == 0 here (shouldn't happen when size>0) treat as error
		if (write_cluster == 0) {
			// may happen for append when file had no clusters but we didn't allocate - but in that case we did allocate above.
			MtFreeVirtualMemory(sector_buf);
			return MT_FAT32_CLUSTER_GENERAL_FAILURE;
		}

		// loop writing clusters one by one
		cur_cluster = write_cluster;
		while (bytes_left > 0 && cur_cluster < FAT32_EOC_MIN) {
			uint32_t sector_lba = first_sector_of_cluster(cur_cluster);
			// write sectors in cluster
			for (uint32_t sc = 0; sc < fs.sectors_per_cluster && bytes_left > 0; ++sc) {
				// if starting in the middle of cluster, handle first sector specially
				if (sc == 0 && write_offset > 0) {
					// read sector, patch from write_offset, write back
					status = read_sector(sector_lba + sc, sector_buf);
					if (MT_FAILURE(status)) { MtFreeVirtualMemory(sector_buf); return status; }
					uint32_t off_in_sector = write_offset % fs.bytes_per_sector;
					uint32_t to_copy = fs.bytes_per_sector - off_in_sector;
					if (to_copy > bytes_left) to_copy = bytes_left;

					// Copy new data
					kmemcpy((uint8_t*)sector_buf + off_in_sector, src, to_copy);

					// Zero out remaining bytes in the sector
					if (to_copy < fs.bytes_per_sector - off_in_sector)
						kmemset((uint8_t*)sector_buf + off_in_sector + to_copy, 0,
							fs.bytes_per_sector - off_in_sector - to_copy);

					// Patch the sector.
					status = write_sector(sector_lba + sc, sector_buf);
					if (MT_FAILURE(status)) { MtFreeVirtualMemory(sector_buf); return status; }

					src += to_copy;
					bytes_left -= to_copy;
					write_offset = 0;
					continue;
				}

				// normal full-sector write
				uint32_t to_write = (bytes_left > fs.bytes_per_sector) ? fs.bytes_per_sector : bytes_left;
				if (to_write < fs.bytes_per_sector) {
					// partial sector: read-modify-write to preserve rest
					status = read_sector(sector_lba + sc, sector_buf);
					if (MT_FAILURE(status)) { MtFreeVirtualMemory(sector_buf); return status; }
					kmemcpy(sector_buf, src, to_write);
					kmemset((uint8_t*)sector_buf + to_write, 0, fs.bytes_per_sector - to_write);
					status = write_sector(sector_lba + sc, sector_buf);
					if (MT_FAILURE(status)) { MtFreeVirtualMemory(sector_buf); return status; }
				}
				else {
					// full sector
					kmemcpy(sector_buf, src, fs.bytes_per_sector);
					status = write_sector(sector_lba + sc, sector_buf);
					if (MT_FAILURE(status)) { MtFreeVirtualMemory(sector_buf); return status; }
				}
				src += to_write;
				bytes_left -= to_write;
			}
			// advance to next cluster in chain
			uint32_t next = fat32_read_fat(cur_cluster);
			if (bytes_left == 0) break;
			if (next >= FAT32_EOC_MIN) {
				// Shouldn't happen because we allocated enough clusters, but guard anyway
				MtFreeVirtualMemory(sector_buf);
				return MT_FAT32_CLUSTERS_FULL;
			}
			cur_cluster = next;
		}

		MtFreeVirtualMemory(sector_buf);
	}

	// Step 6: Create and write directory entries (LFN + SFN)
	// When file already existed -> update its SFN entry in-place (file_size + fst_clus)
	// When file didn't exist -> create new LFN+SFN entries (as original impl)
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
	// set file size & first_cluster (first_cluster may be zero for size==0)
	uint32_t final_size = size;
	if (mode == WRITE_MODE_APPEND_EXISTING && exists) final_size = existing_entry.file_size + size;
	sfn_entry->file_size = final_size;
	sfn_entry->fst_clus_lo = (uint16_t)first_cluster;
	sfn_entry->fst_clus_hi = (uint16_t)(first_cluster >> 16);

	if (exists && located) {
		// Update existing on-disk SFN+LFN in-place: overwrite SFN fields + file_size + fst_clus (but preserve other fields).
		void* write_buf = MtAllocateVirtualMemory(512, 512);
		if (!write_buf) return MT_NO_MEMORY;
		status = read_sector(located_sector, write_buf);
		if (MT_FAILURE(status)) { MtFreeVirtualMemory(write_buf); return status; }

		FAT32_DIR_ENTRY* entries = (FAT32_DIR_ENTRY*)write_buf;
		// The located_index is the start (LFN or SFN). We assumed read_lfn returned consumed entries for that start.
		// If the located entry was an LFN chain, the SFN is at located_index + (located_consumed - 1)
		uint32_t sfn_pos = located_index + (located_consumed ? (located_consumed - 1) : 0);
		// Update SFN's cluster and filesize
		entries[sfn_pos].fst_clus_lo = (uint16_t)first_cluster;
		entries[sfn_pos].fst_clus_hi = (uint16_t)(first_cluster >> 16);
		entries[sfn_pos].file_size = final_size;

		// If original file had no LFN and we're writing a long-name (lfn_count>0), we cannot expand in-place safely.
		// In that case we fallback to find free slots and write a new LFN+SFN pair and mark old entries deleted.
		bool can_update_inplace = (lfn_count == 0) || (located_consumed == total_entries);
		if (!can_update_inplace) {
			// mark old consumed entries deleted (0xE5), then write new entries in free slots
			for (uint32_t k = 0; k < located_consumed; ++k) {
				((uint8_t*)entries[located_index + k].name)[0] = DELETED_DIR_ENTRY;
			}
			status = write_sector(located_sector, write_buf);
			if (MT_FAILURE(status)) { MtFreeVirtualMemory(write_buf); return status; }
			MtFreeVirtualMemory(write_buf);

			// find new free slots and write new entries (reuse original creation path)
			uint32_t entry_sector, entry_index;
			if (!fat32_find_free_dir_slots(parent_cluster, total_entries, &entry_sector, &entry_index)) {
				// cleanup clusters if we've allocated them
				return MT_FAT32_DIR_FULL;
			}
			void* write_buf2 = MtAllocateVirtualMemory(512, 512);
			if (!write_buf2) { return MT_NO_MEMORY; }
			status = read_sector(entry_sector, write_buf2);
			if (MT_FAILURE(status)) { MtFreeVirtualMemory(write_buf2); return status; }
			kmemcpy(&((FAT32_DIR_ENTRY*)write_buf2)[entry_index], entry_buf, total_entries * sizeof(FAT32_DIR_ENTRY));
			status = write_sector(entry_sector, write_buf2);
			MtFreeVirtualMemory(write_buf2);
			return status;
		}
		else {
			// safe to update in-place: write sector
			status = write_sector(located_sector, write_buf);
			MtFreeVirtualMemory(write_buf);
			return status;
		}
	}
	else {
		// File did not exist: find space and write the entries (original flow)
		uint32_t entry_sector, entry_index;
		if (!fat32_find_free_dir_slots(parent_cluster, total_entries, &entry_sector, &entry_index)) {
			// If we allocated new clusters earlier, free them (best effort)
			if (first_cluster) fat32_free_cluster_chain(first_cluster);
			return MT_FAT32_DIR_FULL;
		}

		void* write_buf = MtAllocateVirtualMemory(512, 512);
		if (!write_buf) return MT_NO_MEMORY;
		status = read_sector(entry_sector, write_buf);
		if (MT_FAILURE(status)) { MtFreeVirtualMemory(write_buf); return status; }
		kmemcpy(&((FAT32_DIR_ENTRY*)write_buf)[entry_index], entry_buf, total_entries * sizeof(FAT32_DIR_ENTRY));
		status = write_sector(entry_sector, write_buf);

		// free write_buf
		MtFreeVirtualMemory(write_buf);
		return status;
	}

	// Should not reach here
	return MT_GENERAL_FAILURE;
}

MTSTATUS fat32_list_directory(const char* path, char* listings, size_t max_len) {
	tracelast_func("fat32_list_directory");
	MTSTATUS status;
	// 1. Find the directory entry for the given path to get its starting cluster.
	FAT32_DIR_ENTRY dir_entry;
	if (!fat32_find_entry(path, &dir_entry, NULL) || !(dir_entry.attr & ATTR_DIRECTORY)) {
		gop_printf(0xFFFF0000, "Error: Directory not found or path is not a directory: %s\n", path);
		return MT_FAT32_DIRECTORY_NOT_FOUND;
	}

	uint32_t cluster = (uint32_t)((dir_entry.fst_clus_hi << 16) | dir_entry.fst_clus_lo);
	if (cluster == 0) { // Root directory special case on some FAT16/12, but for FAT32 it should be root_cluster.
		cluster = fs.root_cluster;
	}

	void* buf = MtAllocateVirtualMemory(512, 512);
	if (!buf) return MT_NO_MEMORY;

	// 2. The rest of the logic is the same as fat32_list_root, just starting from `cluster`.
	do {
		uint32_t sector = first_sector_of_cluster(cluster);
		for (uint32_t i = 0; i < fs.sectors_per_cluster; ++i) {
			status = read_sector(sector + i, buf);
			if (MT_FAILURE(status)) { MtFreeVirtualMemory(buf); return status; }

			FAT32_DIR_ENTRY* dir = (FAT32_DIR_ENTRY*)buf;
			uint32_t entries = fs.bytes_per_sector / sizeof(*dir);

			for (uint32_t j = 0; j < entries; ) {
				FAT32_DIR_ENTRY* current_entry = &dir[j];

				if (current_entry->name[0] == END_OF_DIRECTORY) {
					MtFreeVirtualMemory(buf);
					return MT_FAT32_DIR_FULL;
				}
				// Skip deleted, '.', and '..' entries from the listing
				if ((uint8_t)current_entry->name[0] == DELETED_DIR_ENTRY ||
					(current_entry->name[0] == '.' && (current_entry->name[1] == '\0' || current_entry->name[1] == '.'))) {
					j++;
					continue;
				}

				char lfn_name[MAX_LFN_LEN];
				uint32_t consumed = 0;
				FAT32_DIR_ENTRY* sfn_entry = read_lfn(current_entry, entries - j, lfn_name, &consumed);
				char line_buf[256];
				if (sfn_entry) {
					if (sfn_entry->attr & ATTR_DIRECTORY) {
						ksnprintf(line_buf, sizeof(line_buf), "  <DIR>  %s\n", lfn_name);
						kstrncat(listings, line_buf, max_len);
					}
					else {
						ksnprintf(line_buf, sizeof(line_buf), "         %s   (%u bytes)\n", lfn_name, sfn_entry->file_size);
						kstrncat(listings, line_buf, max_len);
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
	return MT_SUCCESS;
}

// Check that a directory cluster contains only '.' and '..' (and deleted entries).
// Returns true if empty ,false if non-empty or error.
bool fat32_directory_is_empty(const char* path) {
	tracelast_func("fat32_directory_is_empty");

	FAT32_DIR_ENTRY entry;
	uint32_t parent_cluster = 0;
	fat32_find_entry(path, &entry, &parent_cluster);

	uint32_t dir_cluster = get_dir_cluster(&entry);
	if (dir_cluster == 0) return false;

	void* buf = MtAllocateVirtualMemory(512, 512);
	if (!buf) return false;

	uint32_t cluster = dir_cluster;
	MTSTATUS status;
	do {
		uint32_t sector_lba = first_sector_of_cluster(cluster);
		for (uint32_t s = 0; s < fs.sectors_per_cluster; ++s) {
			status = read_sector(sector_lba + s, buf);
			if (MT_FAILURE(status)) { MtFreeVirtualMemory(buf); return false; }

			FAT32_DIR_ENTRY* entries = (FAT32_DIR_ENTRY*)buf;
			uint32_t entries_per_sector = fs.bytes_per_sector / sizeof(FAT32_DIR_ENTRY);

			for (uint32_t j = 0; j < entries_per_sector; ) {
				uint8_t first = (uint8_t)entries[j].name[0];

				if (first == END_OF_DIRECTORY) { MtFreeVirtualMemory(buf); return true; } // no more entries
				if (first == DELETED_DIR_ENTRY) { j++; continue; }

				// Build full name (LFN or SFN)
				char lfn_buf[MAX_LFN_LEN];
				uint32_t consumed = 0;
				FAT32_DIR_ENTRY* sfn = read_lfn(&entries[j], entries_per_sector - j, lfn_buf, &consumed);
				if (!sfn) { j++; continue; }

				// skip '.' and '..'
				if ((unsigned char)sfn->name[0] == '.') {
					j += consumed;
					continue;
				}

				// There is a non-deleted entry that is not '.'/'..' -> directory not empty
				MtFreeVirtualMemory(buf);
				return false;
			}
		}
		cluster = fat32_read_fat(cluster);
	} while (cluster < FAT32_EOC_MIN);

	MtFreeVirtualMemory(buf);
	return true;
}

// Mark the SFN and all preceding LFN entries for `filename` in parent_cluster as deleted.
// `path` is the full path. parent_cluster is cluster of parent directory.
// Returns true on success (sector written), false otherwise.
static bool mark_entry_and_lfns_deleted(const char* path, uint32_t parent_cluster) {
	tracelast_func("mark_entry_and_lfns_deleted");
	// extract filename (last component)
	char path_copy[260];
	kstrcpy(path_copy, path);
	int len = (int)kstrlen(path_copy);
	// strip trailing slashes
	while (len > 1 && path_copy[len - 1] == '/') { path_copy[--len] = '\0'; }

	int last_slash = -1;
	for (int i = len - 1; i >= 0; --i) {
		if (path_copy[i] == '/') { last_slash = i; break; }
	}

	const char* filename = (last_slash == -1) ? path_copy : &path_copy[last_slash + 1];

	// Prepare SFN format for short-name comparison
	char sfn_formatted[11];
	format_short_name(filename, sfn_formatted);

	void* buf = MtAllocateVirtualMemory(512, 512);
	if (!buf) return false;

	uint32_t cluster = parent_cluster;
	MTSTATUS status;
	do {
		uint32_t sector_lba = first_sector_of_cluster(cluster);
		for (uint32_t s = 0; s < fs.sectors_per_cluster; ++s) {
			status = read_sector(sector_lba + s, buf);
			if (MT_FAILURE(status)) { MtFreeVirtualMemory(buf); return false; }

			FAT32_DIR_ENTRY* entries = (FAT32_DIR_ENTRY*)buf;
			uint32_t entries_per_sector = fs.bytes_per_sector / sizeof(FAT32_DIR_ENTRY);

			for (uint32_t j = 0; j < entries_per_sector; ) {
				uint8_t first = (uint8_t)entries[j].name[0];

				if (first == END_OF_DIRECTORY) { MtFreeVirtualMemory(buf); return false; } // not found in parent
				if (first == DELETED_DIR_ENTRY) { j++; continue; }

				char lfn_buf[MAX_LFN_LEN];
				uint32_t consumed = 0;
				FAT32_DIR_ENTRY* sfn = read_lfn(&entries[j], entries_per_sector - j, lfn_buf, &consumed);

				if (sfn) {
					// Match by LFN (exact), LFN (case-insensitive), or SFN bytes
					bool match = false;

					// 1) exact LFN match
					if (kstrcmp(lfn_buf, filename) == 0) {
						match = true;
					}

					// 2) case-insensitive LFN match
					if (!match && ci_equal(lfn_buf, filename)) {
						match = true;
					}

					// 3) SFN byte-wise compare (token formatted)
					if (!match && cmp_short_name(sfn->name, sfn_formatted)) {
						match = true;
					}

					if (match) {
						// Mark all consumed entries (LFN...SFN) as deleted (0xE5)
						for (uint32_t k = 0; k < consumed; ++k) {
							((uint8_t*)entries[j + k].name)[0] = DELETED_DIR_ENTRY;
						}

						// Write sector back to disk
						bool ok = write_sector(sector_lba + s, buf);
						MtFreeVirtualMemory(buf);
						return ok;
					}

					j += consumed;
					continue;
				}
				else {
					// read_lfn failed (corrupted LFN chain?), skip this entry
					j++;
				}
			}
		}
		cluster = fat32_read_fat(cluster);
	} while (cluster < FAT32_EOC_MIN);

	MtFreeVirtualMemory(buf);
	return false; // not found
}


// Recursively delete directory contents and free the directory's cluster chain.
// This function deletes all children (files & subdirs) found inside dir_cluster,
// marks their directory entries as DELETED on disk, and finally frees dir_cluster itself.
// Returns true on success, false on any error.
static bool fat32_rm_rf_dir(uint32_t dir_cluster) {
	tracelast_func("fat32_rm_rf_dir");

	if (dir_cluster == 0 || dir_cluster == fs.root_cluster) return false; // never delete root here

	void* buf = MtAllocateVirtualMemory(512, 512);
	if (!buf) return false;

	uint32_t cluster = dir_cluster;
	// Iterate cluster chain
	MTSTATUS status;
	while (cluster < FAT32_EOC_MIN) {
		uint32_t sector_lba = first_sector_of_cluster(cluster);

		for (uint32_t s = 0; s < fs.sectors_per_cluster; ++s) {
			status = read_sector(sector_lba + s, buf);
			if (MT_FAILURE(status)) { MtFreeVirtualMemory(buf); return false; }

			FAT32_DIR_ENTRY* entries = (FAT32_DIR_ENTRY*)buf;
			uint32_t entries_per_sector = fs.bytes_per_sector / sizeof(FAT32_DIR_ENTRY);

			for (uint32_t j = 0; j < entries_per_sector; ) {
				uint8_t first = (uint8_t)entries[j].name[0];

				// End of directory: nothing after this in this directory
				if (first == END_OF_DIRECTORY) {
					// we can stop scanning this directory entirely
					// free buffer and break out to free cluster chain
					MtFreeVirtualMemory(buf);
					goto free_and_return;
				}

				// Deleted entry: skip
				if (first == DELETED_DIR_ENTRY) { j++; continue; }

				// Attempt to read LFN + SFN at this position
				char lfn_name[MAX_LFN_LEN];
				uint32_t consumed = 0;
				FAT32_DIR_ENTRY* sfn = read_lfn(&entries[j], entries_per_sector - j, lfn_name, &consumed);

				if (!sfn) {
					// corrupted chain or not an entry we can parse: skip single entry
					j++;
					continue;
				}

				// Skip '.' and '..' entries
				if ((unsigned char)sfn->name[0] == '.') {
					j += consumed;
					continue;
				}

				// If directory -> recurse
				if (sfn->attr & ATTR_DIRECTORY) {
					uint32_t child_cluster = get_dir_cluster(sfn);
					if (child_cluster != 0 && child_cluster != 1 && child_cluster != dir_cluster) {
						// Recursively delete child directory contents and free its clusters.
						if (!fat32_rm_rf_dir(child_cluster)) {
							// recursion failed — return false
							MtFreeVirtualMemory(buf);
							return false;
						}
						// At this point child's clusters are freed by the recursive call.
					}
					// After child deleted, mark child's LFN+SFN entries as deleted in this parent sector
					for (uint32_t k = 0; k < consumed; ++k) {
						((uint8_t*)entries[j + k].name)[0] = DELETED_DIR_ENTRY;
					}
					// write this sector back
					status = write_sector(sector_lba + s, buf);
					if (MT_FAILURE(status)) { MtFreeVirtualMemory(buf); return false; }
					// advance past consumed entries
					j += consumed;
					continue;
				}
				else {
					// It's a file: free its cluster chain (if any) then mark entries deleted
					uint32_t file_cluster = get_dir_cluster(sfn);
					if (file_cluster >= 2) {
						if (!fat32_free_cluster_chain(file_cluster)) {
							MtFreeVirtualMemory(buf);
							return false;
						}
					}
					// mark the LFN+SFN entries as deleted
					for (uint32_t k = 0; k < consumed; ++k) {
						((uint8_t*)entries[j + k].name)[0] = DELETED_DIR_ENTRY;
					}
					// write sector back
					status = write_sector(sector_lba + s, buf);
					if (MT_FAILURE(status)) { MtFreeVirtualMemory(buf); return false; }
					j += consumed;
					continue;
				}
			} // for each entry in sector
		} // for each sector in cluster

		cluster = fat32_read_fat(cluster);
	} // while cluster chain

free_and_return:
	// Free this directory's own cluster chain (we deleted contents)
	if (!fat32_free_cluster_chain(dir_cluster)) {
		// if freeing fails, we still consider it an error
		return false;
	}
	return true;
}

MTSTATUS fat32_delete_directory(const char* path) {
	tracelast_func("fat32_delete_directory");

	// Find entry & its parent cluster
	FAT32_DIR_ENTRY entry;
	uint32_t parent_cluster;
	if (!fat32_find_entry(path, &entry, &parent_cluster)) return MT_FAT32_DIRECTORY_NOT_FOUND;

	// Must be a directory
	if (!(entry.attr & ATTR_DIRECTORY)) return MT_FAT32_INVALID_FILENAME;

	uint32_t dir_cluster = get_dir_cluster(&entry);
	if (dir_cluster == 0) dir_cluster = fs.root_cluster;

	// Don't allow removing root via this function
	if (dir_cluster == fs.root_cluster) return MT_GENERAL_FAILURE;

	// Recursively delete children and free the directory's clusters.
	if (!fat32_rm_rf_dir(dir_cluster)) return MT_GENERAL_FAILURE;

	// Now mark this directory's entry (LFN+SFN) in parent as deleted.
	if (!mark_entry_and_lfns_deleted(path, parent_cluster)) return MT_GENERAL_FAILURE;

	return MT_SUCCESS;
}

static inline bool is_file(FAT32_DIR_ENTRY* entry) {
	uint8_t attr = entry->attr;
	if ((attr & ATTR_LONG_NAME) == ATTR_LONG_NAME) return false; // skip LFN
	if (attr & ATTR_DIRECTORY) return false; // skip directories
	return true; // it's a regular file
}

MTSTATUS fat32_delete_file(const char* path) {
	tracelast_func("fat32_delete_file");

	// Find the file entry and its parent cluster
	FAT32_DIR_ENTRY entry;
	uint32_t parent_cluster;
	if (!fat32_find_entry(path, &entry, &parent_cluster)) {
		return MT_FAT32_DIRECTORY_NOT_FOUND; // File not found
	}

	// Must be a file (not a directory)
	if (!is_file(&entry)) {
		return MT_FAT32_INVALID_FILENAME; // Not a file
	}

	// Get the file's first cluster
	uint32_t file_cluster = get_dir_cluster(&entry);

	// Free the file's cluster chain (if it has any clusters allocated)
	if (file_cluster >= 2 && file_cluster < FAT32_EOC_MIN) {
		if (!fat32_free_cluster_chain(file_cluster)) {
			return MT_GENERAL_FAILURE; // Failed to free cluster chain
		}
	}

	// Mark the file's directory entry (LFN + SFN) as deleted in the parent directory
	if (!mark_entry_and_lfns_deleted(path, parent_cluster)) {
		return MT_GENERAL_FAILURE; // Failed to mark directory entries as deleted
	}

	return MT_SUCCESS; // Success
}