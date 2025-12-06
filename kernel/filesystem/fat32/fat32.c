/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     FAT32 FileSystem Implementation.
 */

#include "fat32.h"
#include "../../drivers/blk/block.h"
#include "../../assert.h"
#include "../../time.h"
#include "../../intrinsics/atomic.h"
#include "../../includes/mg.h"
#include "../../includes/mm.h"

#define WRITE_MODE_APPEND_EXISTING 0
#define WRITE_MODE_CREATE_OR_REPLACE 1

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define le32toh(x) __builtin_bswap32(x)
#else
#define le32toh(x) (x)
#endif

static FAT32_BPB bpb;
static FAT32_FSINFO fs;
static BLOCK_DEVICE* disk;
extern GOP_PARAMS gop_local;

static SPINLOCK fat32_read_fat_lock = { 0 };
static SPINLOCK fat32_write_fat_lock = { 0 };
static void* fat_cache_buf = NULL;
void* fat_cache_buf2 = NULL;
static uint32_t fat_cache_sector = UINT32_MAX;

#define MAX_LFN_ENTRIES 20       // Allows up to 260 chars (20*13)
#define MAX_LFN_LEN 260

volatile int32_t fat32_called_from_scanner = 0;

// Internal Error Constants
#define FAT32_READ_ERROR   0xFFFFFFFFu
typedef struct {
	uint16_t name_chars[13];     // UTF-16 characters from one LFN entry
} LFN_ENTRY_BUFFER;

// Read sector into the buffer.
static MTSTATUS read_sector(uint32_t lba, void* buf) {

	size_t NumberOfBytes = fs.bytes_per_sector;
	if (!NumberOfBytes) NumberOfBytes = 512;

	if (NumberOfBytes % 512 != 0) {
		// NumberOfBytes must be in multiples of 512.
		return MT_INVALID_PARAM;
	}

	return disk->read_sector(disk, lba, buf, NumberOfBytes);
}

// Write to sector from buffer
static MTSTATUS write_sector(uint32_t lba, const void* buf) {

	size_t NumberOfBytes = fs.bytes_per_sector;
	if (!NumberOfBytes) NumberOfBytes = 512;

	if (NumberOfBytes % 512 != 0) {
		// NumberOfBytes must be in multiples of 512.
		return MT_INVALID_PARAM;
	}

	return disk->write_sector(disk, lba, buf, NumberOfBytes);
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
			if (pos >= MAX_LFN_LEN - 1) { // Check BEFORE writing
				goto done;
			}
			if (ch <= 0x7F) out_name[pos++] = (char)ch; else out_name[pos++] = '?';
		}

		// Name2 at offset 14, 6 UTF-16 chars
		uint16_t* name2 = (uint16_t*)(ebytes + 14);
		for (int c = 0; c < 6; ++c) {
			uint16_t ch = name2[c];
			if (ch == 0x0000) { out_name[pos] = '\0'; goto done; }
			if (pos >= MAX_LFN_LEN - 1) {
				goto done;
			}
			if (ch <= 0x7F) out_name[pos++] = (char)ch; else out_name[pos++] = '?';
		}

		// Name3 at offset 28, 2 UTF-16 chars
		uint16_t* name3 = (uint16_t*)(ebytes + 28);
		for (int c = 0; c < 2; ++c) {
			uint16_t ch = name3[c];
			if (ch == 0x0000) { out_name[pos] = '\0'; goto done; }
			if (pos >= MAX_LFN_LEN - 1) {
				goto done;
			}
			if (ch <= 0x7F) out_name[pos++] = (char)ch; else out_name[pos++] = '?';
		}
	}

done:
	out_name[pos] = '\0';
	// consumed entries = number of LFN entries + the 8.3 entry
	*out_consumed = (uint32_t)lfn_count + 1;
	return short_entry;
}

static inline uint32_t fat32_total_clusters(void) {
	return (bpb.total_sectors_32 - fs.first_data_sector) / fs.sectors_per_cluster;
}

// Read the FAT for the given cluster, to inspect data about this specific cluster, like which sectors are free, used, what's the next sector, and which sector are EOF (end of file = 0x0FFFFFFF)
static uint32_t fat32_read_fat(uint32_t cluster) {
	bool isScanner = InterlockedCompareExchange32(&fat32_called_from_scanner, 0, 0);

	// Do not treat reserved clusters as "free" returned to callers that iterate the chain.
	if (cluster < 2) {
		if (isScanner) {
			return FAT32_READ_ERROR;
		}
		return FAT32_EOC_MIN;
	}

	IRQL oldIrql;
	MsAcquireSpinlock(&fat32_read_fat_lock, &oldIrql);

	// allocate cache buffer onceW
	if (!fat_cache_buf) {
		fat_cache_buf = MmAllocatePoolWithTag(NonPagedPool, fs.bytes_per_sector, '1TAF');
		if (!fat_cache_buf) {
			gop_printf(0xFFFF0000, "fat32_read_fat: couldn't alloc cache buf\n");
			MsReleaseSpinlock(&fat32_read_fat_lock, oldIrql);
			if (isScanner) {
				return FAT32_READ_ERROR;
			}
			return FAT32_EOC_MIN;
		}
	}

	uint32_t fat_offset = cluster * 4;
	uint32_t fat_sector = fs.fat_start + (fat_offset / fs.bytes_per_sector);
	uint32_t ent_offset = fat_offset % fs.bytes_per_sector;
	uint32_t bps = fs.bytes_per_sector;

	// read sector only if different from cached one
	if (fat_cache_sector != fat_sector) {
		MTSTATUS st = read_sector(fat_sector, fat_cache_buf);
		if (MT_FAILURE(st)) {
			gop_printf(0xFFFF0000, "fat32_read_fat: read_sector fail for sector %u\n", fat_sector);
			MsReleaseSpinlock(&fat32_read_fat_lock, oldIrql);
			if (isScanner) {
				return FAT32_READ_ERROR;
			}
			return FAT32_EOC_MIN;
		}
		fat_cache_sector = fat_sector;
	}

	uint32_t raw = 0;
	uint32_t val = 0;

	if (ent_offset <= bps - 4) {
		/* entirely inside cached sector */
		kmemcpy(&raw, (uint8_t*)fat_cache_buf + ent_offset, sizeof(raw));
		raw = le32toh(raw);
		val = raw & 0x0FFFFFFF;
	}
	else {
		/* entry spans to next sector */
		if (!fat_cache_buf2) {
			fat_cache_buf2 = MmAllocatePoolWithTag(NonPagedPool, bps, '2TAF');
			if (!fat_cache_buf2) {
				gop_printf(0xFFFF0000, "fat32_read_fat: couldn't alloc secondary cache buf\n");
				MsReleaseSpinlock(&fat32_read_fat_lock, oldIrql);
				if (isScanner) {
					return FAT32_READ_ERROR;
				}
				return FAT32_EOC_MIN;
			}
		}

		MTSTATUS st2 = read_sector(fat_sector + 1, fat_cache_buf2);
		if (MT_FAILURE(st2)) {
			gop_printf(0xFFFF0000, "fat32_read_fat: read_sector fail for next sector %u\n", fat_sector + 1);
			MsReleaseSpinlock(&fat32_read_fat_lock, oldIrql);
			if (isScanner) {
				return FAT32_READ_ERROR;
			}
			return FAT32_EOC_MIN;
		}

		uint8_t tmp[4];
		size_t first = bps - ent_offset;             // bytes available in current sector
		kmemcpy(tmp, (uint8_t*)fat_cache_buf + ent_offset, first);
		kmemcpy(tmp + first, (uint8_t*)fat_cache_buf2, 4 - first);
		kmemcpy(&raw, tmp, sizeof(raw));
		raw = le32toh(raw);
		val = raw & 0x0FFFFFFF;
	}

	/* diagnostic: use the computed raw (not a fresh read from cache which might be wrong if entry spanned) */
	if (val == cluster) {
		if (raw == 0) {
			gop_printf(0xFFFF0000, "FAT suspicious: cluster=%u -> raw=0x%08x (ent_off=%u, fat_sector=%u, total=%u)\n",
				cluster, raw, ent_offset, fat_sector, fat32_total_clusters());
			MsReleaseSpinlock(&fat32_read_fat_lock, oldIrql);
			if (isScanner) {
				return FAT32_READ_ERROR;
			}
			return FAT32_EOC_MIN;
		}
	}

	MsReleaseSpinlock(&fat32_read_fat_lock, oldIrql);
	return val;
}

static inline uint32_t first_sector_of_cluster(uint32_t cluster) {
	return fs.first_data_sector + (cluster - 2) * fs.sectors_per_cluster;
}


static bool fat32_write_fat(uint32_t cluster, uint32_t value) {
	IRQL oldIrql;
	MsAcquireSpinlock(&fat32_write_fat_lock, &oldIrql);
	uint32_t fat_offset = cluster * 4;
	uint32_t sec_index = fat_offset / fs.bytes_per_sector;
	uint32_t ent_offset = fat_offset % fs.bytes_per_sector;
	uint32_t bps = fs.bytes_per_sector;
	if (bps == 0) { gop_printf(0xFFFF0000, "fat32_write_fat: bps==0!\n"); MsReleaseSpinlock(&fat32_write_fat_lock, oldIrql); return false; }
	// We may need up to two buffers if the entry spans sectors.
	void* buf1 = MmAllocatePoolWithTag(NonPagedPool, bps, '1FUB');
	if (!buf1) {
		MsReleaseSpinlock(&fat32_write_fat_lock, oldIrql);
		return false;
	}
	gop_printf(0x00FF00FF, "fat32_write_fat: alloc buf1=%p bps=%u ent_off=%u sec=%u\n", buf1, bps, ent_offset, sec_index);
	void* buf2 = NULL; // Allocate only if needed

	bool spans = (ent_offset > bps - 4);
	if (spans) {
		buf2 = MmAllocatePoolWithTag(NonPagedPool, bps, 'fat');
		if (!buf2) {
			MmFreePool(buf1);
			MsReleaseSpinlock(&fat32_write_fat_lock, oldIrql);
			return false;
		}
	}

	bool ok = true;
	for (uint32_t fat_i = 0; fat_i < bpb.num_fats; ++fat_i) {
		uint32_t current_fat_base = fs.fat_start + (fat_i * fs.sectors_per_fat);
		uint32_t sector1_lba = current_fat_base + sec_index;
		uint32_t sector2_lba = sector1_lba + 1;

		if (spans) {
			// Read both affected sectors
			if (MT_FAILURE(read_sector(sector1_lba, buf1)) || MT_FAILURE(read_sector(sector2_lba, buf2))) {
				ok = false;
				break;
			}

			// Modify the two buffers
			uint8_t value_bytes[4];
			kmemcpy(value_bytes, &value, 4);

			size_t first_part_size = bps - ent_offset;
			size_t second_part_size = 4 - first_part_size;

			kmemcpy((uint8_t*)buf1 + ent_offset, value_bytes, first_part_size);
			kmemcpy(buf2, value_bytes + first_part_size, second_part_size);

			// Write both sectors back
			if (MT_FAILURE(write_sector(sector1_lba, buf1)) || MT_FAILURE(write_sector(sector2_lba, buf2))) {
				ok = false;
				break;
			}
		}
		else {
			// Entry is fully contained in one sector
			if (MT_FAILURE(read_sector(sector1_lba, buf1))) { ok = false; break; }

			// Read existing 4-byte raw entry safely (avoid unaligned direct deref)
			uint32_t raw_le = 0;
			kmemcpy(&raw_le, (uint8_t*)buf1 + ent_offset, sizeof(raw_le));
			uint32_t raw = le32toh(raw_le);

			// Modify only the low 28 bits per FAT32
			raw = (raw & 0xF0000000) | (value & 0x0FFFFFFF);

			// Write back in little-endian form
			uint32_t new_le = le32toh(raw); // on little-endian this is a no-op; or define htole32 properly
			kmemcpy((uint8_t*)buf1 + ent_offset, &new_le, sizeof(new_le));

			if (MT_FAILURE(write_sector(sector1_lba, buf1))) { ok = false; break; }
		}
	}

	MmFreePool(buf1);
	if (buf2) {
		MmFreePool(buf2);
	}
	MsReleaseSpinlock(&fat32_write_fat_lock, oldIrql);
	return ok;
}


static inline uint32_t get_dir_cluster(FAT32_DIR_ENTRY* entry) {
	return ((uint32_t)entry->fst_clus_hi << 16) | entry->fst_clus_lo;
}

// Free a cluster chain starting at start_cluster (set each entry to FREE)
static bool fat32_free_cluster_chain(uint32_t start_cluster) {
	if (start_cluster < 2 || start_cluster >= FAT32_EOC_MIN) return false;

	uint32_t cur = start_cluster;
	while (cur < FAT32_EOC_MIN) {
		uint32_t next = fat32_read_fat(cur);
		if (next == cur || next == 0) {
			gop_printf(0xFFFF0000, "Detected FAT self-loop/zero at %u -> %u | %s\n", cur, next, __func__);
			break; // fail gracefully
		}
		// mark current as free
		if (!fat32_write_fat(cur, FAT32_FREE_CLUSTER)) return false;
		// protect against pathological loops
		if (next == cur) break;
		cur = next;
	}
	return true;
}

static uint32_t fat32_find_free_cluster(void) {
	// Atomically update.
	InterlockedExchange32(&fat32_called_from_scanner, 1);
	// Start searching from cluster 2 (the first usable cluster)
	// In a more advanced implementation, we would use the FSInfo sector to find a hint. But even then that hint can be misleading (read osdev on FAT)
	uint32_t total_clusters = fat32_total_clusters();
	uint32_t retval = 0;
	for (uint32_t i = 2; i < total_clusters; i++) {
		retval = fat32_read_fat(i);
		if (retval == FAT32_FREE_CLUSTER) {
			///gop_printf(COLOR_CYAN, "Returning with %u RETVAL.\n", i);
			InterlockedExchange32(&fat32_called_from_scanner, 0);
			return i;
		}
		else if (retval == FAT32_READ_ERROR) {
			///gop_printf(COLOR_BROWN, "Hit a read error, continuing.\n");
			continue;
		}
	}
	InterlockedExchange32(&fat32_called_from_scanner, 0);
	///gop_printf(COLOR_CYAN, "Returning with 0 RETVAL.\n");
	return 0; // no free clusters found..
}

static bool zero_cluster(uint32_t cluster) {
	void* buf = MmAllocatePoolWithTag(NonPagedPool, fs.bytes_per_sector, 'FUBF');
	bool success = true;
	if (!buf) return false;
	kmemset(buf, 0, fs.bytes_per_sector);
	MTSTATUS status;
	uint32_t sector = first_sector_of_cluster(cluster);
	for (uint32_t i = 0; i < fs.sectors_per_cluster; i++) {
		status = write_sector(sector + i, buf);
		if (MT_FAILURE(status)) {
			success = false;
			break;
		}
	}

	MmFreePool(buf);
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

/// Returns the number of LFN entries created.
static uint32_t fat32_create_lfn_entries(FAT32_LFN_ENTRY* entry_buffer, const char* long_name, uint8_t sfn_checksum) {
	uint32_t len = kstrlen(long_name);
	uint32_t num_lfn_entries = (len + 12) / 13;  // 13 chars per entry
	uint32_t char_idx = 0;

	for (int i = (int)num_lfn_entries - 1; i >= 0; --i) {
		FAT32_LFN_ENTRY* lfn = &entry_buffer[i];

		// Clear the entry
		kmemset(lfn, 0, sizeof(*lfn));

		uint8_t seq = (uint8_t)(num_lfn_entries - i);
		if (i == (int)num_lfn_entries - 1)
			seq |= 0x40; // last entry marker

		lfn->LDIR_Ord = seq;
		lfn->LDIR_Attr = 0x0F;
		lfn->LDIR_Type = 0;
		lfn->LDIR_Chksum = sfn_checksum;
		lfn->LDIR_FstClusLO = 0;

		// Fill name fields safely
		for (int k = 0; k < 13; ++k) {
			uint16_t uch = 0xFFFF;
			if (char_idx < len)
				uch = (uint8_t)long_name[char_idx];
			else if (char_idx == len)
				uch = 0x0000; // null terminator

			if (k < 5)
				lfn->LDIR_Name1[k] = uch;
			else if (k < 11)
				lfn->LDIR_Name2[k - 5] = uch;
			else
				lfn->LDIR_Name3[k - 11] = uch;

			if (char_idx <= len)
				++char_idx;
		}
	}

	return num_lfn_entries;
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
	kstrncpy(path_copy, path, sizeof(path_copy));
	
	uint32_t current_cluster = fs.root_cluster;
	uint32_t parent_cluster_of_last_found = fs.root_cluster;

	if (kstrcmp(path_copy, "/") == 0 || path_copy[0] == '\0') {
		if (out_entry) {
			kmemset(out_entry, 0, sizeof(FAT32_DIR_ENTRY));
			out_entry->attr = ATTR_DIRECTORY;
			out_entry->fst_clus_lo = (uint16_t)(fs.root_cluster & 0xFFFF);
			out_entry->fst_clus_hi = (uint16_t)(fs.root_cluster >> 16);
		}
		if (out_parent_cluster) *out_parent_cluster = fs.root_cluster;
		return true;
	}

	FAT32_DIR_ENTRY last_found_entry;
	kmemset(&last_found_entry, 0, sizeof(FAT32_DIR_ENTRY));
	bool any_token_found = false;

	char* save_ptr = NULL;
	char* token = kstrtok_r(path_copy, "/", &save_ptr);

	while (token != NULL) {
		bool found_this_token = false;
		parent_cluster_of_last_found = current_cluster;

		void* sector_buf = MmAllocatePoolWithTag(NonPagedPool, fs.bytes_per_sector, 'tecs');
		if (!sector_buf) return false;

		uint32_t search_cluster = current_cluster;
		do {
			uint32_t sector = first_sector_of_cluster(search_cluster);
			for (uint32_t i = 0; i < fs.sectors_per_cluster; i++) {
				MTSTATUS status = read_sector(sector + i, sector_buf);
				if (MT_FAILURE(status)) { MmFreePool(sector_buf); return false; }

				FAT32_DIR_ENTRY* entries = (FAT32_DIR_ENTRY*)sector_buf;
				uint32_t num_entries = fs.bytes_per_sector / sizeof(FAT32_DIR_ENTRY);

				for (uint32_t j = 0; j < num_entries; ) {
					if (entries[j].name[0] == END_OF_DIRECTORY) {
						goto next_cluster; // Break inner loop, continue with next cluster
					}
					if ((uint8_t)entries[j].name[0] == DELETED_DIR_ENTRY) { j++; continue; }

					char lfn_buf[MAX_LFN_LEN];
					uint32_t consumed = 0;
					FAT32_DIR_ENTRY* sfn = read_lfn(&entries[j], num_entries - j, lfn_buf, &consumed);

					if (sfn) {
						// Using ci_equal for simplicity, assuming it does case-insensitive compare
						if (ci_equal(lfn_buf, token)) {
							kmemcpy(&last_found_entry, sfn, sizeof(FAT32_DIR_ENTRY));
							found_this_token = true;
							current_cluster = (sfn->fst_clus_hi << 16) | sfn->fst_clus_lo;
							goto token_search_done; // Break all search loops for this token
						}
					}
					j += (consumed > 0) ? consumed : 1;
				}
			}
		next_cluster:
			search_cluster = fat32_read_fat(search_cluster);
		} while (search_cluster < FAT32_EOC_MIN);

	token_search_done:
		MmFreePool(sector_buf); // free buffer

		if (!found_this_token) {
			return false; // Path component not found
		}
		any_token_found = true;
		token = kstrtok_r(NULL, "/", &save_ptr);

		if (token != NULL && !(last_found_entry.attr & ATTR_DIRECTORY)) {
			return false; // Trying to traverse into a file
		}
	}

	if (any_token_found) {
		if (out_entry) kmemcpy(out_entry, &last_found_entry, sizeof(FAT32_DIR_ENTRY));
		if (out_parent_cluster) *out_parent_cluster = parent_cluster_of_last_found;
		return true;
	}

	return false;
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
	void* sector_buf = MmAllocatePoolWithTag(NonPagedPool, fs.bytes_per_sector, 'tecs');
	if (!sector_buf) return false;
	MTSTATUS status;

	do {
		uint32_t sector_lba = first_sector_of_cluster(current_cluster);
		for (uint32_t i = 0; i < fs.sectors_per_cluster; i++) {
			status = read_sector(sector_lba + i, sector_buf);
			if (MT_FAILURE(status)) { MmFreePool(sector_buf); return false; }

			FAT32_DIR_ENTRY* entries = (FAT32_DIR_ENTRY*)sector_buf;
			uint32_t num_entries = fs.bytes_per_sector / sizeof(FAT32_DIR_ENTRY);

			// --- CRITICAL FIX: Reset counter for each new sector ---
			uint32_t consecutive_free = 0;

			for (uint32_t j = 0; j < num_entries; j++) {
				uint8_t first_byte = (uint8_t)entries[j].name[0];
				if (first_byte == END_OF_DIRECTORY || first_byte == DELETED_DIR_ENTRY) {
					if (consecutive_free == 0) {
						// Mark the start of a potential block
						*out_sector = sector_lba + i;
						*out_entry_index = j;
					}
					consecutive_free++;
					if (consecutive_free == count) {
						MmFreePool(sector_buf);
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
			MmFreePool(sector_buf);
			if (fat32_extend_directory(dir_cluster)) {
				return fat32_find_free_dir_slots(dir_cluster, count, out_sector, out_entry_index);
			}
			else {
				return false;
			}
		}
		current_cluster = next_cluster;
	} while (true);

	MmFreePool(sector_buf);
	return false;
}

#define BPB_SECTOR_START 2048

// Read BPB (Bios Parameter Block) and initialize.
MTSTATUS fat32_init(int disk_index) {
	MTSTATUS status;
	disk = get_block_device(disk_index);
	if (!disk) { return MT_GENERAL_FAILURE; }

	void* buf = MmAllocatePoolWithTag(NonPagedPool, 512, 'TAF');
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
	MmFreePool(buf);
	return MT_SUCCESS;
}

// Walk cluster chain and read directory entries.
void fat32_list_root(void) {
	uint32_t cluster = fs.root_cluster;

	void* buf = MmAllocatePoolWithTag(NonPagedPool, fs.bytes_per_sector, 'fatb');
	if (!buf) return;

	// Temp buffer to accumulate LFN entries (and eventually the 8.3 entry).
	FAT32_DIR_ENTRY temp_entries[MAX_LFN_ENTRIES + 1];
	uint32_t lfn_accum = 0;
	MTSTATUS status;
	do {
		uint32_t sector = first_sector_of_cluster(cluster);
		for (uint32_t i = 0; i < fs.sectors_per_cluster; ++i) {
			status = read_sector(sector + i, buf);
			if (MT_FAILURE(status)) {
				MmFreePool(buf);
				return;
			}
			FAT32_DIR_ENTRY* dir = (FAT32_DIR_ENTRY*)buf;
			uint32_t entries = fs.bytes_per_sector / sizeof(*dir);

			for (uint32_t j = 0; j < entries; ++j, ++dir) {
				uint8_t first = (uint8_t)dir->name[0];

				// End of directory: stop everything
				if (first == 0x00) {
					MmFreePool(buf);
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
	} while (cluster < FAT32_EOC_MIN);
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

	if (!filename || filename[0] == '\0') return fs.root_cluster;

	// Make a mutable copy
	char path_copy[260];
	kstrncpy(path_copy, filename, sizeof(path_copy));

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
	MTSTATUS status;
	// We still need a temporary buffer for reading sectors
	void* sblk = MmAllocatePoolWithTag(NonPagedPool, fs.bytes_per_sector, 'sblk');
	if (!sblk) return MT_NO_MEMORY;

	// Get the cluster of the directory filename points to (e.g "tmp/folder/myfile.txt", we need the "folder" cluster.)
	uint32_t cluster = 0;

	if (is_filename_in_dir(filename)) {
		cluster = extract_dir_cluster(filename);
		if (!cluster) {
			MmFreePool(sblk);
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
				MmFreePool(sblk);
				return status;
			}

			FAT32_DIR_ENTRY* dir_entries = (FAT32_DIR_ENTRY*)sblk;
			uint32_t entries_per_sector = fs.bytes_per_sector / sizeof(FAT32_DIR_ENTRY);

			for (uint32_t j = 0; j < entries_per_sector; ) {
				FAT32_DIR_ENTRY* current_entry = &dir_entries[j];

				if (current_entry->name[0] == END_OF_DIRECTORY) {
					// Free sblk
					MmFreePool(sblk);
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
						void* file_buffer = MmAllocatePoolWithTag(NonPagedPool, file_size, 'file');
						if (!file_buffer) {
							// Free sblk
							MmFreePool(sblk);
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
									MmFreePool(file_buffer);
									MmFreePool(sblk);
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
						MmFreePool(sblk);
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
	MmFreePool(sblk);
	return MT_FAT32_FILE_NOT_FOUND; // File not found after searching the entire directory
}

MTSTATUS fat32_create_directory(const char* path) {
	// 1. Check if an entry already exists at this path
	if (fat32_find_entry(path, NULL, NULL)) {
#ifdef DEBUG
		gop_printf(0xFFFF0000, "Error: Path '%s' already exists.\n", path);
#endif
		return MT_FAT32_DIRECTORY_ALREADY_EXISTS;
	}
	MTSTATUS status = MT_GENERAL_FAILURE;
	// 2. Separate parent path and new directory name
	char path_copy[260];
	kstrncpy(path_copy, path, sizeof(path_copy));

	char* new_dir_name = NULL;
	char* parent_path = "/";

	// 1. Remove trailing slashes (except if path is just "/")
	int len = kstrlen(path_copy);
	while (len > 1 && path_copy[len - 1] == '/') {
		path_copy[len - 1] = '\0';
		len--;
	}

	// 2. Find last slash
	int last_slash = -1;
	for (int i = 0; path_copy[i] != '\0'; i++) {
		if (path_copy[i] == '/') last_slash = i;
	}

	// 3. Split parent path and new directory name
	if (last_slash != -1) {
		new_dir_name = &path_copy[last_slash + 1];   // name after last slash
		if (last_slash > 0) {
			path_copy[last_slash] = '\0';           // terminate parent path
			parent_path = path_copy;
		}
		// If last_slash == 0, parent_path stays "/"
	}
	else {
		// No slashes at all: directory is in root
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
	void* sector_buf = MmAllocatePoolWithTag(NonPagedPool, fs.bytes_per_sector, 'fat');
	if (!sector_buf) { /* handle error */ return MT_MEMORY_LIMIT; }
	kmemset(sector_buf, 0, fs.bytes_per_sector);
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

	// Decide whether we need LFN entries:
	int name_len = kstrlen(new_dir_name);
	int need_lfn = 0;
	if (name_len > 11) need_lfn = 1;
	else {
		for (int i = 0; i < name_len; i++) {
			char c = new_dir_name[i];
			if (c >= 'a' && c <= 'z') { need_lfn = 1; break; }
			/* optionally: detect characters not representable in SFN and set need_lfn = 1 */
		}
	}

	uint32_t entry_sector = 0, entry_index = 0;

	if (need_lfn) {
		// Create LFN + SFN (allocate contiguous slots)
		uint8_t checksum = lfn_checksum((uint8_t*)sfn);
		uint32_t num_lfn_entries = (name_len + 12) / 13;
		uint32_t total_slots = num_lfn_entries + 1; // LFN entries + SFN

		if (!fat32_find_free_dir_slots(parent_cluster, total_slots, &entry_sector, &entry_index)) {
			// free sector_buf, free cluster...
			MmFreePool(sector_buf);
			fat32_write_fat(new_cluster, FAT32_FREE_CLUSTER);
			return MT_FAT32_DIR_FULL;
		}

		// Prepare temporary buffer with LFN entries followed by SFN
		FAT32_LFN_ENTRY* temp_entries = (FAT32_LFN_ENTRY*)MmAllocatePoolWithTag(NonPagedPool, total_slots * sizeof(FAT32_LFN_ENTRY), 'fat');
		if (!temp_entries) {
			MmFreePool(sector_buf);
			fat32_write_fat(new_cluster, FAT32_FREE_CLUSTER);
			return MT_MEMORY_LIMIT;
		}

		kmemset(temp_entries, 0, total_slots * sizeof(FAT32_LFN_ENTRY));

		// Fill LFN entries into temp_entries[0 .. num_lfn_entries-1]
		fat32_create_lfn_entries(temp_entries, new_dir_name, checksum);

		// Fill SFN at the end
		FAT32_DIR_ENTRY* sfn_entry = (FAT32_DIR_ENTRY*)&temp_entries[num_lfn_entries];
		kmemset(sfn_entry, 0, sizeof(FAT32_DIR_ENTRY));
		kmemcpy(sfn_entry->name, sfn, 11);
		sfn_entry->attr = ATTR_DIRECTORY;
		sfn_entry->fst_clus_lo = (uint16_t)new_cluster;
		sfn_entry->fst_clus_hi = (uint16_t)(new_cluster >> 16);

		// Write temp_entries sequentially into parent directory slots (may span sectors)
		const int entries_per_sector = fs.bytes_per_sector / sizeof(FAT32_DIR_ENTRY);
		uint32_t cur_sector = entry_sector;
		int cur_index = (int)entry_index;
		uint32_t remaining = total_slots;
		uint32_t temp_idx = 0;

		while (remaining > 0) {
			status = read_sector(cur_sector, sector_buf);
			if (MT_FAILURE(status)) {
				// cleanup: free temp, free sector_buf, free cluster
				MmFreePool(temp_entries);
				MmFreePool(sector_buf);
				fat32_write_fat(new_cluster, FAT32_FREE_CLUSTER);
				return status;
			}

			int can = entries_per_sector - cur_index;
			int to_write = (remaining < (uint32_t)can) ? remaining : (uint32_t)can;

			for (int j = 0; j < to_write; j++) {
				FAT32_DIR_ENTRY* dst = &((FAT32_DIR_ENTRY*)sector_buf)[cur_index + j];
				FAT32_DIR_ENTRY* src = (FAT32_DIR_ENTRY*)&temp_entries[temp_idx + j];
				kmemcpy(dst, src, sizeof(FAT32_DIR_ENTRY));
			}

			status = write_sector(cur_sector, sector_buf);
			if (MT_FAILURE(status)) {
				MmFreePool(temp_entries);
				MmFreePool(sector_buf);
				fat32_write_fat(new_cluster, FAT32_FREE_CLUSTER);
				return status;
			}

			remaining -= to_write;
			temp_idx += to_write;
			cur_sector++;
			cur_index = 0;
		}

		// cleanup temp buffer
		MmFreePool(temp_entries);
		// free sector_buf and return last write status
		MmFreePool(sector_buf);
		return status;
	}
	else {
		// No LFN needed: simple single-slot SFN write (original behaviour)
		if (!fat32_find_free_dir_slots(parent_cluster, 1, &entry_sector, &entry_index)) {
			// free sector_buf, free cluster...
			MmFreePool(sector_buf);
			fat32_write_fat(new_cluster, FAT32_FREE_CLUSTER);
			return MT_FAT32_DIR_FULL;
		}

		// Read the target sector, modify it, write it back
		status = read_sector(entry_sector, sector_buf);
		if (MT_FAILURE(status)) { MmFreePool(sector_buf); return status; }

		FAT32_DIR_ENTRY* new_entry = &((FAT32_DIR_ENTRY*)sector_buf)[entry_index];
		kmemset(new_entry, 0, sizeof(FAT32_DIR_ENTRY));
		kmemcpy(new_entry->name, sfn, 11);
		new_entry->attr = ATTR_DIRECTORY;
		new_entry->fst_clus_lo = (uint16_t)new_cluster;
		new_entry->fst_clus_hi = (uint16_t)(new_cluster >> 16);

		status = write_sector(entry_sector, sector_buf);

		// free sector_buf
		MmFreePool(sector_buf);
		return status;
	}
}

static TIME_ENTRY convertFat32ToRealtime(uint16_t fat32Time, uint16_t fat32Date) {
	TIME_ENTRY time;
	uint8_t h, m, s;
	uint8_t mon, day;
	uint16_t y;
	fat32_decode_date(fat32Date, &y, &mon, &day);
	fat32_decode_time(fat32Time, &h, &m, &s);
	time.hour = h;
	time.minute = m;
	time.second = s;
	time.month = mon;
	time.day = day;
	time.year = y;
	return time;
}

MTSTATUS fat32_write_file(const char* path, const void* data, uint32_t size, uint32_t mode) {
	// Safety check.
	if (mode != WRITE_MODE_CREATE_OR_REPLACE && mode != WRITE_MODE_APPEND_EXISTING) {
		return MT_FAT32_INVALID_WRITE_MODE;
	}
	MTSTATUS status = MT_GENERAL_FAILURE;
	uint32_t first_cluster = 0;

	// --- Step 1: Safely parse parent path and filename ---
	char parent_path_buf[260];
	char filename_buf[260];
	int last_slash = -1;
	for (int len = 0; path[len] != '\0'; len++) {
		if (path[len] == '/') {
			last_slash = len;
		}
	}

	if (last_slash == -1) {
		// No slash, e.g., "file.txt". Parent is root.
		kstrcpy(parent_path_buf, "/");
		kstrncpy(filename_buf, path, sizeof(filename_buf) - 1);
		filename_buf[sizeof(filename_buf) - 1] = '\0';
	}
	else {
		// Slash found.
		kstrncpy(filename_buf, &path[last_slash + 1], sizeof(filename_buf) - 1);
		filename_buf[sizeof(filename_buf) - 1] = '\0';
		if (last_slash == 0) {
			// Path is "/file.txt". Parent is root.
			kstrcpy(parent_path_buf, "/");
		}
		else {
			// Path is "/testdir/file.txt". Copy the parent part.
			size_t parent_len = last_slash;
			if (parent_len >= sizeof(parent_path_buf)) {
				parent_len = sizeof(parent_path_buf) - 1; // Truncate
			}
			kmemcpy(parent_path_buf, path, parent_len);
			parent_path_buf[parent_len] = '\0';
		}
	}

	char* filename = filename_buf;
	char* parent_path = parent_path_buf;

	// --- Step 2: Find parent directory and check for existing file ---
	FAT32_DIR_ENTRY parent_entry;
	if (!fat32_find_entry(parent_path, &parent_entry, NULL) || !(parent_entry.attr & ATTR_DIRECTORY)) {
		return MT_FAT32_CLUSTER_NOT_FOUND;
	}
	uint32_t parent_cluster = (parent_entry.fst_clus_hi << 16) | parent_entry.fst_clus_lo;

	FAT32_DIR_ENTRY existing_entry;
	bool exists = fat32_find_entry(path, &existing_entry, NULL);

	// Helper: locate on-disk sector + index for the entry (so we can update SFN/LFN in-place)
	uint32_t located_sector = 0;
	uint32_t located_index = 0;
	uint32_t located_consumed = 0;
	bool located = false;
	if (exists) {
		void* buf = MmAllocatePoolWithTag(NonPagedPool, fs.bytes_per_sector, 'fat');
		if (!buf) return MT_NO_MEMORY;
		uint32_t cluster = parent_cluster;
		do {
			uint32_t sector_lba = first_sector_of_cluster(cluster);
			for (uint32_t s = 0; s < fs.sectors_per_cluster; ++s) {
				status = read_sector(sector_lba + s, buf);
				if (MT_FAILURE(status)) { MmFreePool(buf); return status; }
				FAT32_DIR_ENTRY* entries = (FAT32_DIR_ENTRY*)buf;
				uint32_t entries_per_sector = fs.bytes_per_sector / sizeof(FAT32_DIR_ENTRY);

				for (uint32_t j = 0; j < entries_per_sector; ) {
					uint8_t first = (uint8_t)entries[j].name[0];
					if (first == END_OF_DIRECTORY) { MmFreePool(buf); goto locate_done; }
					if (first == DELETED_DIR_ENTRY) { j++; continue; }

					char lfn_buf[MAX_LFN_LEN];
					uint32_t consumed = 0;
					FAT32_DIR_ENTRY* sfn = read_lfn(&entries[j], entries_per_sector - j, lfn_buf, &consumed);
					if (sfn) {
						if (ci_equal(lfn_buf, filename)) {
							located_sector = sector_lba + s;
							located_index = j;
							located_consumed = consumed;
							located = true;
							MmFreePool(buf);
							goto locate_done;
						}
						j += consumed;
					}
					else {
						j++;
					}
				}
			}
			cluster = fat32_read_fat(cluster);
		} while (cluster < FAT32_EOC_MIN);
		MmFreePool(buf);
	}
locate_done:

	// Step 3: Handle existing file based on write mode
	if (exists) first_cluster = (existing_entry.fst_clus_hi << 16) | existing_entry.fst_clus_lo;

	if (mode == WRITE_MODE_CREATE_OR_REPLACE) {
		if (exists && first_cluster >= 2) {
			if (!fat32_free_cluster_chain(first_cluster)) {
				return MT_FAT32_INVALID_CLUSTER;
			}
		}
		first_cluster = 0;
	}

	// Step 4: Allocate clusters and write file data
	if (size > 0) {
		uint32_t cluster_size = fs.sectors_per_cluster * fs.bytes_per_sector;
		uint32_t clusters_needed = 0;
		uint32_t last_cluster = 0;
		uint32_t append_offset = 0;

		if (mode == WRITE_MODE_APPEND_EXISTING && exists && first_cluster != 0) {
			uint32_t cur = first_cluster;
			if (existing_entry.file_size > 0) {
				while (cur < FAT32_EOC_MIN) {
					uint32_t next = fat32_read_fat(cur);
					if (next >= FAT32_EOC_MIN) { last_cluster = cur; break; }
					cur = next;
				}
				append_offset = existing_entry.file_size % cluster_size;
			}
		}

		if (mode == WRITE_MODE_APPEND_EXISTING && exists && append_offset > 0) {
			uint32_t bytes_fit = cluster_size - append_offset;
			if (size > bytes_fit) {
				clusters_needed = (size - bytes_fit + cluster_size - 1) / cluster_size;
			}
		}
		else {
			clusters_needed = (size + cluster_size - 1) / cluster_size;
		}

		uint32_t first_new = 0;
		uint32_t prev_cluster = 0;
		for (uint32_t i = 0; i < clusters_needed; ++i) {
			uint32_t nc = fat32_find_free_cluster();
			if (nc == 0) {
				if (first_new) fat32_free_cluster_chain(first_new);
				return MT_FAT32_CLUSTERS_FULL;
			}
			zero_cluster(nc);
			if (first_new == 0) first_new = nc;
			if (prev_cluster != 0) fat32_write_fat(prev_cluster, nc);
			prev_cluster = nc;
		}
		if (prev_cluster != 0) fat32_write_fat(prev_cluster, FAT32_EOC_MAX);

		if (mode == WRITE_MODE_APPEND_EXISTING && exists && first_new != 0) {
			if (last_cluster == 0) {
				first_cluster = first_new;
			}
			else {
				fat32_write_fat(last_cluster, first_new);
			}
		}
		else if (mode != WRITE_MODE_APPEND_EXISTING || !exists) {
			if (first_new != 0) first_cluster = first_new;
		}

		void* sector_buf = MmAllocatePoolWithTag(NonPagedPool, fs.bytes_per_sector, 'fat');
		if (!sector_buf) {
			if (first_new) fat32_free_cluster_chain(first_new);
			return MT_NO_MEMORY;
		}

		const uint8_t* src = (const uint8_t*)data;
		uint32_t bytes_left = size;
		uint32_t write_cluster = (mode == WRITE_MODE_APPEND_EXISTING && exists && append_offset > 0) ? last_cluster : first_cluster;

		while (bytes_left > 0 && write_cluster < FAT32_EOC_MIN) {
			uint32_t sector_lba = first_sector_of_cluster(write_cluster);
			uint32_t start_offset_in_cluster = (write_cluster == last_cluster && append_offset > 0) ? append_offset : 0;

			for (uint32_t s = start_offset_in_cluster / fs.bytes_per_sector; s < fs.sectors_per_cluster && bytes_left > 0; ++s) {
				uint32_t off_in_sector = (s == start_offset_in_cluster / fs.bytes_per_sector) ? start_offset_in_cluster % fs.bytes_per_sector : 0;
				uint32_t to_write = fs.bytes_per_sector - off_in_sector;
				if (to_write > bytes_left) to_write = bytes_left;

				if (off_in_sector > 0 || to_write < fs.bytes_per_sector) {
					read_sector(sector_lba + s, sector_buf);
				}
				kmemcpy((uint8_t*)sector_buf + off_in_sector, src, to_write);
				write_sector(sector_lba + s, sector_buf);

				src += to_write;
				bytes_left -= to_write;
			}
			append_offset = 0; // Only matters for the very first cluster write in an append op
			write_cluster = fat32_read_fat(write_cluster);
		}
		MmFreePool(sector_buf);
	}

	// --- Step 5: Prepare directory entry data (LFN + SFN) ---
	char sfn[11];
	format_short_name(filename, sfn);
	uint8_t checksum = lfn_checksum((uint8_t*)sfn);

	uint32_t lfn_count = (kstrlen(filename) + 12) / 13;
	uint32_t total_entries = lfn_count + 1;
	FAT32_LFN_ENTRY* entry_buf = (FAT32_LFN_ENTRY*)MmAllocatePoolWithTag(NonPagedPool, total_entries * sizeof(FAT32_LFN_ENTRY), 'fat');
	if (!entry_buf) {
		if (mode != WRITE_MODE_APPEND_EXISTING || !exists) {
			if (first_cluster) fat32_free_cluster_chain(first_cluster);
		}
		return MT_NO_MEMORY;
	}

	fat32_create_lfn_entries(entry_buf, filename, checksum);

	FAT32_DIR_ENTRY* sfn_entry = (FAT32_DIR_ENTRY*)&entry_buf[lfn_count];
	kmemset(sfn_entry, 0, sizeof(FAT32_DIR_ENTRY));
	kmemcpy(sfn_entry->name, sfn, 11);
	sfn_entry->attr = 0; // File attribute
	uint32_t final_size = size;
	if (mode == WRITE_MODE_APPEND_EXISTING && exists) final_size = existing_entry.file_size + size;
	sfn_entry->file_size = final_size;
	sfn_entry->fst_clus_lo = (uint16_t)first_cluster;
	sfn_entry->fst_clus_hi = (uint16_t)(first_cluster >> 16);

	// --- Step 6: Safely find space and write directory entries ---

	// If the file existed before, we must mark its old directory entries as deleted.
	if (exists && located) {
		void* delete_buf = MmAllocatePoolWithTag(NonPagedPool, fs.bytes_per_sector, 'fat');
		if (delete_buf) {
			// This logic assumes old entries are in one sector. A more complex implementation
			// would loop across sectors if located_consumed + located_index > entries_per_sector.
			status = read_sector(located_sector, delete_buf);
			if (MT_SUCCEEDED(status)) {
				FAT32_DIR_ENTRY* entries = (FAT32_DIR_ENTRY*)delete_buf;
				for (uint32_t k = 0; k < located_consumed; ++k) {
					if ((located_index + k) < (fs.bytes_per_sector / 32)) {
						entries[located_index + k].name[0] = DELETED_DIR_ENTRY;
					}
				}
				write_sector(located_sector, delete_buf);
			}
			MmFreePool(delete_buf);
		}
	}

	uint32_t entry_sector, entry_index;
	if (!fat32_find_free_dir_slots(parent_cluster, total_entries, &entry_sector, &entry_index)) {
		if (mode != WRITE_MODE_APPEND_EXISTING || !exists) {
			if (first_cluster) fat32_free_cluster_chain(first_cluster);
		}
		MmFreePool(entry_buf);
		return MT_FAT32_DIR_FULL;
	}

	void* write_buf = MmAllocatePoolWithTag(NonPagedPool, fs.bytes_per_sector, 'fat');
	if (!write_buf) { MmFreePool(entry_buf); return MT_NO_MEMORY; }

	uint32_t current_sector = entry_sector;
	uint32_t current_index_in_sector = entry_index;
	uint32_t entries_remaining = total_entries;
	uint8_t* source_entry = (uint8_t*)entry_buf;
	const uint32_t entries_per_sector = fs.bytes_per_sector / 32;

	while (entries_remaining > 0) {
		status = read_sector(current_sector, write_buf);
		if (MT_FAILURE(status)) {
			MmFreePool(write_buf);
			MmFreePool(entry_buf);
			return status;
		}

		uint32_t space_in_sector = entries_per_sector - current_index_in_sector;
		uint32_t entries_to_write = (entries_remaining < space_in_sector) ? entries_remaining : space_in_sector;

		kmemcpy((uint8_t*)write_buf + current_index_in_sector * 32, source_entry, entries_to_write * 32);

		status = write_sector(current_sector, write_buf);
		if (MT_FAILURE(status)) {
			MmFreePool(write_buf);
			MmFreePool(entry_buf);
			return status;
		}

		entries_remaining -= entries_to_write;
		source_entry += entries_to_write * 32;

		current_sector++; // Assumes contiguous sectors within a cluster
		current_index_in_sector = 0;
	}

	MmFreePool(write_buf);
	MmFreePool(entry_buf);
	return status;
}

MTSTATUS fat32_list_directory(const char* path, char* listings, size_t max_len) {
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

	void* buf = MmAllocatePoolWithTag(NonPagedPool, fs.bytes_per_sector, 'fat');
	if (!buf) return MT_NO_MEMORY;

	if (max_len > 0) listings[0] = '\0';
	size_t used = 0;

	do {
		uint32_t sector = first_sector_of_cluster(cluster);
		bool end_of_dir = false;

		for (uint32_t i = 0; i < fs.sectors_per_cluster; ++i) {
			status = read_sector(sector + i, buf);
			if (MT_FAILURE(status)) { MmFreePool(buf); return status; }

			FAT32_DIR_ENTRY* dir = (FAT32_DIR_ENTRY*)buf;
			uint32_t entries = fs.bytes_per_sector / sizeof(*dir);

			for (uint32_t j = 0; j < entries; ) {
				FAT32_DIR_ENTRY* current_entry = &dir[j];

				if (current_entry->name[0] == END_OF_DIRECTORY) {
					end_of_dir = true;
					break; // stop scanning entries in this sector -> will break outer loops below
				}

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
						ksnprintf(line_buf, sizeof(line_buf), "<DIR>  %s\n", lfn_name);
					}
					else {
						ksnprintf(line_buf, sizeof(line_buf), "%s   (%u bytes)\n", lfn_name, sfn_entry->file_size);
					}
					// safe append: compute remaining and append up to remaining-1
					size_t avail = (used < max_len) ? (max_len - used) : 0;
					if (avail > 1) {
						// write directly into listings+used
						ksnprintf(listings + used, avail, "%s", line_buf);
						used = kstrlen(listings);
					}
					j += consumed;
				}
				else {
					j++;
				}
			}

			if (end_of_dir) break;
		}

		if (end_of_dir) break;

		cluster = fat32_read_fat(cluster);
	} while (cluster < FAT32_EOC_MIN);

	MmFreePool(buf);
	return MT_SUCCESS;
}

// Check that a directory cluster contains only '.' and '..' (and deleted entries).
// Returns true if empty ,false if non-empty or error.
bool fat32_directory_is_empty(const char* path) {

	FAT32_DIR_ENTRY entry;
	uint32_t parent_cluster = 0;
	fat32_find_entry(path, &entry, &parent_cluster);

	uint32_t dir_cluster = get_dir_cluster(&entry);
	if (dir_cluster == 0) return false;

	void* buf = MmAllocatePoolWithTag(NonPagedPool, fs.bytes_per_sector, 'fat');
	if (!buf) return false;

	uint32_t cluster = dir_cluster;
	MTSTATUS status;
	do {
		uint32_t sector_lba = first_sector_of_cluster(cluster);
		for (uint32_t s = 0; s < fs.sectors_per_cluster; ++s) {
			status = read_sector(sector_lba + s, buf);
			if (MT_FAILURE(status)) { MmFreePool(buf); return false; }

			FAT32_DIR_ENTRY* entries = (FAT32_DIR_ENTRY*)buf;
			uint32_t entries_per_sector = fs.bytes_per_sector / sizeof(FAT32_DIR_ENTRY);

			for (uint32_t j = 0; j < entries_per_sector; ) {
				uint8_t first = (uint8_t)entries[j].name[0];

				if (first == END_OF_DIRECTORY) { MmFreePool(buf); return true; } // no more entries
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
				MmFreePool(buf);
				return false;
			}
		}
		cluster = fat32_read_fat(cluster);
	} while (cluster < FAT32_EOC_MIN);

	MmFreePool(buf);
	return true;
}

// Mark the SFN and all preceding LFN entries for `filename` in parent_cluster as deleted.
// `path` is the full path. parent_cluster is cluster of parent directory.
// Returns true on success (sector written), false otherwise.
static bool mark_entry_and_lfns_deleted(const char* path, uint32_t parent_cluster) {
	// extract filename (last component)
	char path_copy[260];
	kstrncpy(path_copy, path, sizeof(path_copy));
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

	void* buf = MmAllocatePoolWithTag(NonPagedPool, fs.bytes_per_sector, 'fat');
	if (!buf) return false;

	uint32_t cluster = parent_cluster;
	MTSTATUS status;
	do {
		uint32_t sector_lba = first_sector_of_cluster(cluster);
		for (uint32_t s = 0; s < fs.sectors_per_cluster; ++s) {
			status = read_sector(sector_lba + s, buf);
			if (MT_FAILURE(status)) { MmFreePool(buf); return false; }

			FAT32_DIR_ENTRY* entries = (FAT32_DIR_ENTRY*)buf;
			uint32_t entries_per_sector = fs.bytes_per_sector / sizeof(FAT32_DIR_ENTRY);

			for (uint32_t j = 0; j < entries_per_sector; ) {
				uint8_t first = (uint8_t)entries[j].name[0];

				if (first == END_OF_DIRECTORY) { MmFreePool(buf); return false; } // not found in parent
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
						MmFreePool(buf);
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

	MmFreePool(buf);
	return false; // not found
}


// Recursively delete directory contents and free the directory's cluster chain.
// This function deletes all children (files & subdirs) found inside dir_cluster,
// marks their directory entries as DELETED on disk, and finally frees dir_cluster itself.
// Returns true on success, false on any error.
static bool fat32_rm_rf_dir(uint32_t dir_cluster) {

	if (dir_cluster == 0 || dir_cluster == fs.root_cluster) return false; // never delete root here

	void* buf = MmAllocatePoolWithTag(NonPagedPool, fs.bytes_per_sector, 'fat');
	if (!buf) return false;

	uint32_t cluster = dir_cluster;
	// Iterate cluster chain
	MTSTATUS status;
	while (cluster < FAT32_EOC_MIN) {
		uint32_t sector_lba = first_sector_of_cluster(cluster);

		for (uint32_t s = 0; s < fs.sectors_per_cluster; ++s) {
			status = read_sector(sector_lba + s, buf);
			if (MT_FAILURE(status)) { MmFreePool(buf); return false; }

			FAT32_DIR_ENTRY* entries = (FAT32_DIR_ENTRY*)buf;
			uint32_t entries_per_sector = fs.bytes_per_sector / sizeof(FAT32_DIR_ENTRY);

			for (uint32_t j = 0; j < entries_per_sector; ) {
				uint8_t first = (uint8_t)entries[j].name[0];

				// End of directory: nothing after this in this directory
				if (first == END_OF_DIRECTORY) {
					// we can stop scanning this directory entirely
					// free buffer and break out to free cluster chain
					MmFreePool(buf);
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
							// recursion failed � return false
							MmFreePool(buf);
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
					if (MT_FAILURE(status)) { MmFreePool(buf); return false; }
					// advance past consumed entries
					j += consumed;
					continue;
				}
				else {
					// It's a file: free its cluster chain (if any) then mark entries deleted
					uint32_t file_cluster = get_dir_cluster(sfn);
					if (file_cluster >= 2) {
						if (!fat32_free_cluster_chain(file_cluster)) {
							MmFreePool(buf);
							return false;
						}
					}
					// mark the LFN+SFN entries as deleted
					for (uint32_t k = 0; k < consumed; ++k) {
						((uint8_t*)entries[j + k].name)[0] = DELETED_DIR_ENTRY;
					}
					// write sector back
					status = write_sector(sector_lba + s, buf);
					if (MT_FAILURE(status)) { MmFreePool(buf); return false; }
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