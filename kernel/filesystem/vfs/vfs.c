/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Virtual File System (VFS) Implementation.
 */

#include "vfs.h"

#include "../../drivers/ahci/ahci.h"
#include "../fat32/fat32.h"

typedef struct MOUNTED_FS {
	FS_DRIVER* driver;
	uint8_t device_id;
	const char* mount_point;  // e.g., "/", "/ext2"
} MOUNTED_FS;

#define MAX_MOUNTS 4
static MOUNTED_FS mounted_fs[MAX_MOUNTS];
static uint8_t mount_count = 0;

#define MAIN_FS_DEVICE 0

// Adapter for VFS FS_DRIVER
static MTSTATUS fat32_fs_init(uint8_t device_id) {
	return fat32_init(device_id);
}

FS_DRIVER fat32_driver = {
	.init = fat32_fs_init,
	.read = fat32_read_file,
	.write = fat32_write_file,
	.delete = fat32_delete_file,
	.listdir = fat32_list_directory,
	.mkdir = fat32_create_directory,
	.rmdir = fat32_delete_directory,
	.is_dir_empty = fat32_directory_is_empty,
	.listrootdir = fat32_list_root,
};

MTSTATUS vfs_init(void) {
	// First initialize other FS Related stuff (FAT32, AHCI, etc..)
	MTSTATUS status = ahci_init();
	if (MT_FAILURE(status)) {
		gop_printf(COLOR_RED, "AHCI | Status failure: %x", status);
		FREEZE();
		return status;
	}
	// Mount FAT32 on MAIN_FS_DEVICE
	status = fat32_driver.init(MAIN_FS_DEVICE);
	if (MT_FAILURE(status)) {
		gop_printf(COLOR_RED, "FAT32 | Status failure: %x", status);
		FREEZE();
		return status;
	}
	mounted_fs[mount_count++] = (MOUNTED_FS){ .driver = &fat32_driver, .device_id = MAIN_FS_DEVICE, .mount_point = "/" };

	return MT_SUCCESS;
}

static MOUNTED_FS* vfs_find_fs_for_path(const char* path) {
	if (!path) return NULL;
	for (uint8_t i = 0; i < mount_count; i++) {
		const char* mount = mounted_fs[i].mount_point;
		// root mount should match anything
		if (mount[0] == '/' && mount[1] == '\0') return &mounted_fs[i];

		// compute mount_len
		size_t mount_len = 0;
		while (mount[mount_len]) mount_len++;

		// path must be at least mount_len
		size_t path_len = 0;
		while (path[path_len]) path_len++;
		if (path_len < mount_len) continue;

		bool match = true;
		for (size_t j = 0; j < mount_len; j++) {
			if (path[j] != mount[j]) { match = false; break; }
		}
		if (match) return &mounted_fs[i];
	}
	return NULL;
}

MTSTATUS vfs_read(const char* filename, uint32_t* file_size_out, void** buffer_out) {
	MOUNTED_FS* fs = vfs_find_fs_for_path(filename);
	if (!fs || !fs->driver || !fs->driver->read) return MT_NOT_IMPLEMENTED;

	return fs->driver->read(filename, file_size_out, buffer_out);
}

MTSTATUS vfs_write(const char* path, const void* data, uint32_t size, FS_WRITE_MODES write_mode) {
	MOUNTED_FS* fs = vfs_find_fs_for_path(path);
	if (!fs || !fs->driver || !fs->driver->write) return MT_NOT_IMPLEMENTED;

	return fs->driver->write(path, data, size, (uint32_t)write_mode);
}

MTSTATUS vfs_delete(const char* path) {
	MOUNTED_FS* fs = vfs_find_fs_for_path(path);
	if (!fs || !fs->driver || !fs->driver->delete) return MT_NOT_IMPLEMENTED;

	return fs->driver->delete(path);
}

MTSTATUS vfs_listdir(const char* path, char* listings, size_t max_len) {
	MOUNTED_FS* fs = vfs_find_fs_for_path(path);
	if (!fs || !fs->driver || !fs->driver->listdir) return MT_NOT_IMPLEMENTED;

	return fs->driver->listdir(path, listings, max_len);
}

MTSTATUS vfs_mkdir(const char* path) {
	MOUNTED_FS* fs = vfs_find_fs_for_path(path);
	if (!fs || !fs->driver || !fs->driver->mkdir) return MT_NOT_IMPLEMENTED;

	return fs->driver->mkdir(path);
}

MTSTATUS vfs_rmdir(const char* path) {
	MOUNTED_FS* fs = vfs_find_fs_for_path(path);
	if (!fs || !fs->driver || !fs->driver->rmdir) return MT_NOT_IMPLEMENTED;

	return fs->driver->rmdir(path);
}

bool vfs_is_dir_empty(const char* path) {
	MOUNTED_FS* fs = vfs_find_fs_for_path(path);
	if (!fs || !fs->driver || !fs->driver->is_dir_empty) return false;

	return fs->driver->is_dir_empty(path);
}

void vfs_listrootdir(void) {
	MOUNTED_FS* fs = vfs_find_fs_for_path("/");
	if (!fs || !fs->driver || !fs->driver->listrootdir) return;

	fs->driver->listrootdir();
	return;
}
