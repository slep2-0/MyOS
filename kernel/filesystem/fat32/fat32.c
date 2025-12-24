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
#include "../../includes/fs.h"
#include "../../includes/ob.h"

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

	void* sector_buf = MmAllocatePoolWithTag(NonPagedPool, fs.bytes_per_sector, 'tecs');
	if (!sector_buf) return false;

	while (token != NULL) {
		bool found_this_token = false;
		parent_cluster_of_last_found = current_cluster;

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

static MTSTATUS fat32_update_file_start_cluster(const char* path, uint32_t new_cluster) {
	char path_copy[260];
	kstrncpy(path_copy, path, sizeof(path_copy));

	if (kstrcmp(path_copy, "/") == 0 || path_copy[0] == '\0') return MT_INVALID_PARAM;

	uint32_t current_cluster = fs.root_cluster;
	char* save_ptr = NULL;
	char* token = kstrtok_r(path_copy, "/", &save_ptr);

	// Buffer for 2 sectors to handle boundary spanning
	uint32_t buf_size = fs.bytes_per_sector * 2;
	void* big_buf = MmAllocatePoolWithTag(NonPagedPool, buf_size, 'UPDT');
	if (!big_buf) return MT_NO_MEMORY;

	while (token != NULL) {
		char* next_token = kstrtok_r(NULL, "/", &save_ptr);
		bool is_target_file = (next_token == NULL);
		bool found_component = false;

		uint32_t search_cluster = current_cluster;

		do {
			uint32_t sector_lba = first_sector_of_cluster(search_cluster);

			// Loop through sectors, but read 2 at a time (sliding window could be better, but this is simpler)
			// We iterate i += 1, effectively overlapping reads or just reading pairs.
			// Simpler approach: Read 2 sectors, process, advance by 1 sector.
			for (uint32_t i = 0; i < fs.sectors_per_cluster; i++) {

				// Read Current Sector
				MTSTATUS st = read_sector(sector_lba + i, big_buf);
				if (MT_FAILURE(st)) { MmFreePool(big_buf); return st; }

				// Read Next Sector (if available in cluster) to capture spanning LFNs
				// If we are at the last sector of a cluster, we can't easily read the next one 
				// without traversing the FAT. For simplicity, we zero the second half if end of cluster.
				if (i < fs.sectors_per_cluster - 1) {
					read_sector(sector_lba + i + 1, (uint8_t*)big_buf + fs.bytes_per_sector);
				}
				else {
					kmemset((uint8_t*)big_buf + fs.bytes_per_sector, 0, fs.bytes_per_sector);
				}

				// Scan the buffer (now essentially treating it as one large sector)
				FAT32_DIR_ENTRY* entries = (FAT32_DIR_ENTRY*)big_buf;
				// We only scan the FIRST sector's worth of entries as "start points"
				// but read_lfn is allowed to look into the second sector part.
				uint32_t num_entries_to_scan = fs.bytes_per_sector / sizeof(FAT32_DIR_ENTRY);
				// The buffer actually holds 2x entries
				uint32_t total_entries_in_buf = buf_size / sizeof(FAT32_DIR_ENTRY);

				for (uint32_t j = 0; j < num_entries_to_scan; ) {
					if (entries[j].name[0] == END_OF_DIRECTORY) goto cluster_done;
					if ((uint8_t)entries[j].name[0] == DELETED_DIR_ENTRY) { j++; continue; }

					char lfn_buf[MAX_LFN_LEN];
					uint32_t consumed = 0;

					// Pass the total available entries so read_lfn can peek into the 2nd sector part
					FAT32_DIR_ENTRY* sfn = read_lfn(&entries[j], total_entries_in_buf - j, lfn_buf, &consumed);

					if (sfn) {
						if (ci_equal(lfn_buf, token)) {
							if (is_target_file) {
								// Calculate where SFN is relative to the start of big_buf
								uintptr_t offset = (uintptr_t)sfn - (uintptr_t)big_buf;

								// Update SFN in memory
								sfn->fst_clus_hi = (uint16_t)((new_cluster >> 16) & 0xFFFF);
								sfn->fst_clus_lo = (uint16_t)(new_cluster & 0xFFFF);

								// Determine which sector the SFN actually lives in
								uint32_t target_sector = sector_lba + i;
								void* write_source = big_buf;

								if (offset >= fs.bytes_per_sector) {
									// The SFN was actually in the second sector (i+1)
									target_sector = sector_lba + i + 1;
									write_source = (uint8_t*)big_buf + fs.bytes_per_sector;
								}

								// Write ONLY the sector containing the SFN
								MTSTATUS wr_st = write_sector(target_sector, write_source);
								MmFreePool(big_buf);
								return wr_st;
							}
							else {
								current_cluster = ((uint32_t)sfn->fst_clus_hi << 16) | sfn->fst_clus_lo;
								found_component = true;
								goto token_next;
							}
						}
					}
					j += (consumed > 0) ? consumed : 1;
				}
			}
			search_cluster = fat32_read_fat(search_cluster);
		} while (search_cluster < FAT32_EOC_MIN);

	cluster_done:
		if (!found_component) {
			MmFreePool(big_buf);
			return MT_NOT_FOUND;
		}

	token_next:
		token = next_token;
	}

	MmFreePool(big_buf);
	return MT_NOT_FOUND;
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

MTSTATUS fat32_read_file(
	IN PFILE_OBJECT FileObject,
	IN uint64_t FileOffset,
	OUT void* Buffer,
	IN size_t BufferSize,
	_Out_Opt size_t* BytesRead
)
{
	if (BytesRead) *BytesRead = 0;
	if (BufferSize == 0) return MT_SUCCESS;

	uint32_t bytes_per_sector = fs.bytes_per_sector;
	uint32_t sectors_per_cluster = fs.sectors_per_cluster;
	uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;

	// Walk the FAT chain until we reach thee desired cluster of the file offset.
	uint32_t current_cluster = (uint32_t)(uintptr_t)FileObject->FsContext;
	uint32_t clusters_to_skip = FileOffset / cluster_size;

	for (uint32_t i = 0; i < clusters_to_skip; i++) {
		current_cluster = fat32_read_fat(current_cluster);
		if (current_cluster >= FAT32_EOC_MIN) {
			return MT_FAT32_EOF;
		}
	}

	// FileOffset is good, we can set it in the file object.
	FileObject->CurrentOffset = FileOffset;

	// We will need an intermediate buffer ONLY IF the buffer given is less than the sector size (which is what DMA reads, we have to make it sector aligned in DMA reads)
	// to avoid buffer overflows.
	void* IntermediateBuffer = MmAllocatePoolWithTag(NonPagedPool, bytes_per_sector, 'BTAF');
	if (!IntermediateBuffer) {
		return MT_NO_MEMORY;
	}

	size_t total_bytes_read = 0;
	size_t bytes_left = BufferSize;
	uint32_t current_file_offset = FileOffset;
	uint8_t* current_buffer_ptr = (uint8_t*)Buffer;
	MTSTATUS status = MT_SUCCESS;

	while (bytes_left > 0) {
		// Calculate the LBA for the current cluster position.
		uint32_t offset_in_cluster = current_file_offset % cluster_size;
		uint32_t sector_index_in_cluster = offset_in_cluster / bytes_per_sector;

		uint32_t lba = fs.first_data_sector + ((current_cluster - 2) * sectors_per_cluster) + sector_index_in_cluster;

		// Determine offsets within this specific sector
		uint32_t offset_in_sector = current_file_offset % bytes_per_sector;
		uint32_t bytes_available_in_sector = bytes_per_sector - offset_in_sector;

		// We can only read as much as fits in the sector OR as much as the caller asked for
		size_t bytes_to_copy = (bytes_left < bytes_available_in_sector) ? bytes_left : bytes_available_in_sector;

		// If its an unaligned read (more bytes than we can fit), we use the intermediate buffer for this.
		bool direct_read = (offset_in_sector == 0) && (bytes_left >= bytes_per_sector);

		void* target_buf = direct_read ? current_buffer_ptr : IntermediateBuffer;

		status = read_sector(lba, target_buf);

		if (MT_FAILURE(status)) {
			// Read failed
			break;
		}

		// Copy data if this was to the intermediate buffer. (not a direct read to caller buffer)
		if (!direct_read) {
			kmemcpy(current_buffer_ptr, (uint8_t*)IntermediateBuffer + offset_in_sector, bytes_to_copy);
		}

		// Advance Pointers.
		total_bytes_read += bytes_to_copy;
		bytes_left -= bytes_to_copy;
		current_buffer_ptr += bytes_to_copy;
		current_file_offset += bytes_to_copy;

		// if the new offset is directly at a cluster boundary (end of cluster) we cannot read it since it would go to a different cluster..
		// We need to read the next cluster and use it.
		if (bytes_left > 0 && (current_file_offset % cluster_size) == 0) {
			current_cluster = fat32_read_fat(current_cluster);

			// Check for EOF (End of Chain)
			if (current_cluster >= FAT32_EOC_MIN) {
				// Technically an error if we expected more data but hit EOF
				status = MT_FAT32_EOF;
				break;
			}
		}
	}

	// 4. Cleanup and Return
	if (IntermediateBuffer) {
		MmFreePool(IntermediateBuffer);
	}

	if (BytesRead) {
		*BytesRead = total_bytes_read;
	}

	// If we read some bytes but hit EOF/Error later, we usually return success with partial count,
	// or the specific error. Here we return the last status.
	return status;
}

MTSTATUS fat32_create_directory(const char* path) {
	// Check if an entry already exists at this path
	if (fat32_find_entry(path, NULL, NULL)) {
#ifdef DEBUG
		gop_printf(0xFFFF0000, "Error: Path '%s' already exists.\n", path);
#endif
		return MT_FAT32_DIRECTORY_ALREADY_EXISTS;
	}
	MTSTATUS status = MT_GENERAL_FAILURE;
	// Separate parent path and new directory name
	char path_copy[260];
	kstrncpy(path_copy, path, sizeof(path_copy));

	char* new_dir_name = NULL;
	char* parent_path = "/";

	// Remove trailing slashes (except if path is just "/")
	int len = kstrlen(path_copy);
	while (len > 1 && path_copy[len - 1] == '/') {
		path_copy[len - 1] = '\0';
		len--;
	}

	// Find last slash
	int last_slash = -1;
	for (int i = 0; path_copy[i] != '\0'; i++) {
		if (path_copy[i] == '/') last_slash = i;
	}

	// Split parent path and new directory name
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


	// Find the parent directory cluster
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
		gop_printf(0xFFFF0000, "Error: Parent path is not a directory. PATH: %s\n", parent_path);
#endif
		return MT_FAT32_PARENT_PATH_NOT_DIR;
	}
	parent_cluster = (parent_entry.fst_clus_hi << 16) | parent_entry.fst_clus_lo;

	// Allocate a new cluster for this directory's contents
	uint32_t new_cluster = fat32_find_free_cluster();
	if (new_cluster == 0) return MT_FAT32_CLUSTERS_FULL;

	fat32_write_fat(new_cluster, FAT32_EOC_MAX);
	zero_cluster(new_cluster);

	// Create '.' and '..' entries in the new cluster
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

	// Create the entry in the parent directory
	// For simplicity, we'll use a simple SFN.
	char sfn[11];
	format_short_name(new_dir_name, sfn);

	// Decide whether we need LFN entries:
	int name_len = kstrlen(new_dir_name);
	int need_lfn = 0;
	if (name_len > 11) need_lfn = 1;
	else {
		for (int i = 0; i < name_len; i++) {
			char c = new_dir_name[i];
			if (c >= 'a' && c <= 'z') { need_lfn = 1; break; }
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

MTSTATUS fat32_write_file(
	IN PFILE_OBJECT FileObject,
	IN uint64_t FileOffset,
	IN void* Buffer,
	IN size_t BufferSize,
	_Out_Opt size_t* BytesWritten
)
{
	if (BytesWritten) *BytesWritten = 0;
	if (BufferSize == 0) return MT_SUCCESS;

	uint32_t bytes_per_sector = fs.bytes_per_sector;
	uint32_t sectors_per_cluster = fs.sectors_per_cluster;
	uint32_t cluster_size = bytes_per_sector * sectors_per_cluster;
	MTSTATUS status = MT_SUCCESS;

	// If we do not have a cluster for the file, we allocate it.
	if (FileObject->FsContext == 0) {
		// Allocate a new cluster
		uint32_t first_cluster = fat32_find_free_cluster();
		if (first_cluster == 0) {
			return MT_FAT32_CLUSTERS_FULL;
		}

		// Mark it as End-of-Chain (EOC) immediately
		fat32_write_fat(first_cluster, FAT32_EOC_MAX);

		// Zero the cluster to avoid leaking old data
		zero_cluster(first_cluster);

		// Update entry on disk so it points to new cluster.
		MTSTATUS update_st = fat32_update_file_start_cluster(FileObject->FileName, first_cluster);

		if (MT_FAILURE(update_st)) {
			// Cleanup: If we couldn't update the directory, we should free the allocated cluster
			fat32_write_fat(first_cluster, FAT32_FREE_CLUSTER);
			return update_st;
		}

		// 5. Update the File Object in memory
		FileObject->FsContext = (void*)(uintptr_t)first_cluster;
	}

	// Seek to the correct cluster based on FileOffset
	uint32_t current_cluster = (uint32_t)(uintptr_t)FileObject->FsContext;
	uint32_t clusters_to_skip = FileOffset / cluster_size;

	for (uint32_t i = 0; i < clusters_to_skip; i++) {
		current_cluster = fat32_read_fat(current_cluster);

		// Looks like we hit EOF.
		if (current_cluster >= FAT32_EOC_MIN) {
			return MT_FAT32_EOF;
		}
	}

	// Intermediate buffer use exactly like in fat32_read_file
	void* IntermediateBuffer = MmAllocatePoolWithTag(NonPagedPool, bytes_per_sector, 'BTAF');
	if (!IntermediateBuffer) {
		return MT_NO_MEMORY;
	}

	size_t total_bytes_written = 0;
	size_t bytes_left = BufferSize;
	uint32_t current_file_offset = FileOffset;
	const uint8_t* src_buffer_ptr = (const uint8_t*)Buffer;

	// Write loop
	while (bytes_left > 0) {
		// Calculate LBA for current position
		uint32_t offset_in_cluster = current_file_offset % cluster_size;
		uint32_t sector_index_in_cluster = offset_in_cluster / bytes_per_sector;
		uint32_t lba = fs.first_data_sector + ((current_cluster - 2) * sectors_per_cluster) + sector_index_in_cluster;

		// Determine offsets within this specific sector
		uint32_t offset_in_sector = current_file_offset % bytes_per_sector;
		uint32_t bytes_available_in_sector = bytes_per_sector - offset_in_sector;
		size_t bytes_to_write = (bytes_left < bytes_available_in_sector) ? bytes_left : bytes_available_in_sector;

		// If we are overwriting the ENTIRE sector, we don't need to read it first.
		// Otherwise (partial write), we must read the existing sector data to preserve surrounding bytes.
		bool full_sector_overwrite = (offset_in_sector == 0) && (bytes_to_write == bytes_per_sector);

		if (full_sector_overwrite) {
			// Looks like we can write directly from the user buffer!
			status = write_sector(lba, (void*)src_buffer_ptr);
		}
		else {
			// We have to read the sector and then modify it and write it back, since it is smaller than the user buffer.
			status = read_sector(lba, IntermediateBuffer);
			if (MT_FAILURE(status)) break;

			// Modify buffer
			kmemcpy((uint8_t*)IntermediateBuffer + offset_in_sector, src_buffer_ptr, bytes_to_write);

			// Write back
			status = write_sector(lba, IntermediateBuffer);
		}

		if (MT_FAILURE(status)) break;

		// Advance pointers
		total_bytes_written += bytes_to_write;
		bytes_left -= bytes_to_write;
		src_buffer_ptr += bytes_to_write;
		current_file_offset += bytes_to_write;

		// If we finished a cluster and still have data, we move to the next one.
		if (bytes_left > 0 && (current_file_offset % cluster_size) == 0) {
			uint32_t next_cluster = fat32_read_fat(current_cluster);

			if (next_cluster >= FAT32_EOC_MIN) {
				// We reached the end of the chain but still have data to write.
				// Allocate a new cluster.
				uint32_t new_c = fat32_find_free_cluster();
				if (new_c == 0) {
					status = MT_FAT32_CLUSTERS_FULL;
					break;
				}

				zero_cluster(new_c);
				fat32_write_fat(current_cluster, new_c); // Current = new
				fat32_write_fat(new_c, FAT32_EOC_MAX);   // Mark new as EOC

				current_cluster = new_c;
			}
			else {
				// Just follow the existing chain (overwriting existing file data)
				current_cluster = next_cluster;
			}
		}
	}

	// Update the file object state before return
	FileObject->CurrentOffset = current_file_offset;

	// If we extended the file we update the size in the object
	if (current_file_offset > FileObject->FileSize) {
		FileObject->FileSize = current_file_offset;
	}

	if (IntermediateBuffer) {
		MmFreePool(IntermediateBuffer);
	}

	if (BytesWritten) {
		*BytesWritten = total_bytes_written;
	}

	return status;
}

// forward
static MTSTATUS fat32_open_file(
	IN const char* path,
	OUT PFILE_OBJECT* FileObjectOut
);

MTSTATUS fat32_create_file(
	IN const char* path,
	OUT PFILE_OBJECT* FileObjectOut
)
{
	// First of all, we check if the file already exists
	FAT32_DIR_ENTRY existing_entry;
	uint32_t parent_cluster_check;

	if (fat32_find_entry(path, &existing_entry, &parent_cluster_check)) {
		// If its a directory we return.
		if (existing_entry.attr & ATTR_DIRECTORY) {
			return MT_FAT32_INVALID_FILENAME;
		}

		// It already exists, return fat32_open_file.
		return fat32_open_file(path, FileObjectOut);
	}

	// Split the path into directory and filename
	char parent_path[260];
	char filename[260];
	int len = kstrlen(path);
	int last_slash = -1;

	// Manual split logic
	for (int i = len - 1; i >= 0; i--) {
		if (path[i] == '/') {
			last_slash = i;
			break;
		}
	}

	if (last_slash == -1) {
		// No slash? It's in the root
		kstrncpy(filename, path, 260);
		parent_path[0] = '/';
		parent_path[1] = '\0';
	}
	else {
		// Copy parent path
		int p_len = (last_slash == 0) ? 1 : last_slash; // Handle "/file.txt" vs "/A/file.txt"
		for (int i = 0; i < p_len; i++) parent_path[i] = path[i];
		parent_path[p_len] = '\0';

		// Copy filename
		kstrncpy(filename, &path[last_slash + 1], 260);
	}

	// Find parent directory cluster
	FAT32_DIR_ENTRY parent_entry;
	uint32_t parent_dir_cluster;

	// Check if root
	if (kstrcmp(parent_path, "/") == 0) {
		parent_dir_cluster = fs.root_cluster;
	}
	else {
		if (!fat32_find_entry(parent_path, &parent_entry, &parent_dir_cluster)) {
			return MT_NOT_FOUND; // Parent folder doesn't exist
		}
		parent_dir_cluster = get_dir_cluster(&parent_entry);
	}

	// Prepare SFN for the file.
	char sfn[11];
	format_short_name(filename, sfn);

	// Append ~ to file.
	if (sfn[6] == ' ') { sfn[6] = '~'; sfn[7] = '1'; }
	else { sfn[6] = '~'; sfn[7] = '1'; } // Force overwrite for safety if name was long

	uint8_t checksum = lfn_checksum((uint8_t*)sfn);

	// Prepare LFN entries for file.
	uint32_t lfn_count = (kstrlen(filename) + 12) / 13;
	uint32_t total_entries = lfn_count + 1; // LFNs + 1 SFN

	FAT32_LFN_ENTRY* entry_buf = (FAT32_LFN_ENTRY*)MmAllocatePoolWithTag(NonPagedPool, total_entries * sizeof(FAT32_LFN_ENTRY), 'LFN');
	if (!entry_buf) return MT_NO_MEMORY;

	fat32_create_lfn_entries(entry_buf, filename, checksum);

	// Configure the SFN entry (the last in the entry buffer)
	FAT32_DIR_ENTRY* sfn_entry = (FAT32_DIR_ENTRY*)&entry_buf[lfn_count];
	kmemset(sfn_entry, 0, sizeof(FAT32_DIR_ENTRY));

	kmemcpy(sfn_entry->name, sfn, 11);
	sfn_entry->attr = ATTR_ARCHIVE;
	sfn_entry->file_size = 0; // New file
	sfn_entry->fst_clus_hi = 0;
	sfn_entry->fst_clus_lo = 0;
	// TODO Timestamps

	// Allocate slots in parent directory to place the file there.
	uint32_t sector_lba;
	uint32_t entry_index;

	if (!fat32_find_free_dir_slots(parent_dir_cluster, total_entries, &sector_lba, &entry_index)) {
		// Directory is full.
		MmFreePool(entry_buf);
		return MT_FAT32_DIR_FULL;
	}

	// Write the entries to disk now.
	void* sector_buf = MmAllocatePoolWithTag(NonPagedPool, fs.bytes_per_sector, 'wrte');
	if (!sector_buf) {
		MmFreePool(entry_buf);
		return MT_NO_MEMORY;
	}

	// FIX: Read the first sector where entries begin
	MTSTATUS status = read_sector(sector_lba, sector_buf);
	if (MT_FAILURE(status)) {
		MmFreePool(sector_buf);
		MmFreePool(entry_buf);
		return status;
	}

	// FIX: Calculate offsets and check for sector boundary crossing
	uint32_t entry_size = sizeof(FAT32_DIR_ENTRY);
	uint32_t total_bytes_to_write = total_entries * entry_size;
	uint32_t offset_in_sector = entry_index * entry_size;
	uint32_t bytes_available_in_sector = fs.bytes_per_sector - offset_in_sector;

	if (total_bytes_to_write <= bytes_available_in_sector) {
		// All entries fit in the current sector ---
		// Copy all entries to the correct offset
		kmemcpy((uint8_t*)sector_buf + offset_in_sector, entry_buf, total_bytes_to_write);

		// Write the single sector back
		status = write_sector(sector_lba, sector_buf);
	}
	else {
		// Write the first part to the current sector
		kmemcpy((uint8_t*)sector_buf + offset_in_sector, entry_buf, bytes_available_in_sector);
		status = write_sector(sector_lba, sector_buf);

		if (MT_SUCCEEDED(status)) {
			// We read the NEXT sector first to preserve any existing data in it
			// This assumes sectors are contiguous.

			uint32_t next_sector_lba = sector_lba + 1;

			status = read_sector(next_sector_lba, sector_buf);
			if (MT_SUCCEEDED(status)) {
				// Calculate remaining bytes
				uint32_t bytes_remaining = total_bytes_to_write - bytes_available_in_sector;

				// Copy the rest of the entries to the START of the new sector buffer
				kmemcpy(sector_buf, (uint8_t*)entry_buf + bytes_available_in_sector, bytes_remaining);

				// Write the second sector
				status = write_sector(next_sector_lba, sector_buf);
			}
		}
	}

	MmFreePool(sector_buf);
	MmFreePool(entry_buf);

	if (MT_FAILURE(status)) {
		return status;
	}

	// Open the file now.
	return fat32_open_file(path, FileObjectOut);
}

MTSTATUS fat32_list_directory(const char* path, char* listings, size_t max_len) {
	MTSTATUS status;
	// Find the directory entry for the given path to get its starting cluster.
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

static MTSTATUS fat32_open_file(
	IN const char* path,
	OUT PFILE_OBJECT* FileObjectOut
)

{
	// Find the file entry and its parent cluster
	FAT32_DIR_ENTRY entry;
	uint32_t parent_cluster;
	if (!fat32_find_entry(path, &entry, &parent_cluster)) {
		return MT_FAT32_FILE_NOT_FOUND; // File not found
	}

	// Must be a file (not a directory)
	if (!is_file(&entry)) {
		return MT_FAT32_INVALID_FILENAME; // Not a file
	}

	// Get the file's first cluster
	uint32_t file_cluster = get_dir_cluster(&entry);

	// All passed, create the object.
	PFILE_OBJECT FileObject = NULL;
	MTSTATUS Status = ObCreateObject(FsFileType, sizeof(FILE_OBJECT), (void**)&FileObject);
	if (MT_FAILURE(Status)) return Status;

	// Fill in fields.
	// File name is the path.

	size_t length = kstrlen(path) + 1;
	FileObject->FileName = MmAllocatePoolWithTag(PagedPool, length, 'eman');
	kstrncpy(FileObject->FileName, path, length);
	// Offset starts at 0.
	FileObject->CurrentOffset = 0;
	// File size given from the entry.
	FileObject->FileSize = entry.file_size;
	// The initial cluster of the file.
	FileObject->FsContext = (void*)(uintptr_t)file_cluster;
	// Flags describing what the hell is this!
	// Currently, none, this also means its a file since the dir bit isnt set.
	FileObject->Flags = MT_FOF_NONE;
	*FileObjectOut = FileObject;
	return MT_SUCCESS;
}

void fat32_deletion_routine(void* Object)

{
	// We just delete the filename allocated.
	PFILE_OBJECT FileObject = (PFILE_OBJECT)Object;
	MmFreePool((void*)FileObject->FileName);
}