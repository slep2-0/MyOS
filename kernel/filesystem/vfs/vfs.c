/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Virtual File System (VFS) Implementation.
 */

#include "../../includes/fs.h"
#include "../../includes/ob.h"

#include "../../drivers/ahci/ahci.h"
#include "../fat32/fat32.h"
#include "../../includes/macros.h"

typedef struct MOUNTED_FS {
	FS_DRIVER* driver;
	uint8_t device_id;
	const char* mount_point;  // e.g., "/", "/ext2"
} MOUNTED_FS;

#define MAX_MOUNTS 4
static MOUNTED_FS mounted_fs[MAX_MOUNTS];
static uint8_t mount_count = 0;

#define MAIN_FS_DEVICE 0

POBJECT_TYPE FsFileType = NULL;

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

// Adapter for VFS FS_DRIVER
static MTSTATUS fat32_fs_init(uint8_t device_id) {
	return fat32_init(device_id);
}

FS_DRIVER fat32_driver = {
	.init = fat32_fs_init,
	.ReadFile = fat32_read_file,
	.WriteFile = fat32_write_file,
	.CreateFile = fat32_create_file,
	.DeleteObjectProcedure = fat32_deletion_routine,
};

static
void FsDeleteObject(
	IN void* Object
)

{
	PFILE_OBJECT FileObject = (PFILE_OBJECT)Object;
	
	// Get FS driver for object, if the driver has an object deletion procedure, we use, if not the object will just be deleted (no additional deletions)
	MOUNTED_FS* fs = vfs_find_fs_for_path(FileObject->FileName);
	if (fs && fs->driver && fs->driver->DeleteObjectProcedure) {
		fs->driver->DeleteObjectProcedure(Object);
	}
}

MTSTATUS FsInitialize(void) {
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
	mounted_fs[mount_count++] = (MOUNTED_FS){ .driver = &fat32_driver, .device_id = MAIN_FS_DEVICE, .mount_point = "/"};

	// Create type initializer for FILE_OBJECT.
	OBJECT_TYPE_INITIALIZER ObjectTypeInitializer;
	kmemset(&ObjectTypeInitializer, 0, sizeof(OBJECT_TYPE_INITIALIZER));

	char* Name = "File";
	ObjectTypeInitializer.PoolType = NonPagedPool;
#ifdef DEBUG
	ObjectTypeInitializer.DumpProcedure = NULL; // TODO DUMP PROC!
#else
	ObjectTypeInitializer.DumpProcedure = NULL;
#endif
	ObjectTypeInitializer.DeleteProcedure = &FsDeleteObject;
	ObjectTypeInitializer.ValidAccessRights = MT_FILE_ALL_ACCESS;
	status = ObCreateObjectType(Name, &ObjectTypeInitializer, &FsFileType);
	if (MT_FAILURE(status)) return status;

	return MT_SUCCESS;
}

MTSTATUS FsReadFile(
	IN PFILE_OBJECT FileObject,
	IN uint64_t FileOffset,
	OUT void* Buffer,
	IN size_t BufferSize,
	_Out_Opt size_t* BytesRead
) 

{	
	MOUNTED_FS* fs = vfs_find_fs_for_path(FileObject->FileName);
	if (!fs || !fs->driver || !fs->driver->ReadFile) return MT_NOT_IMPLEMENTED;

	return fs->driver->ReadFile(FileObject, FileOffset, Buffer, BufferSize, BytesRead);
}

MTSTATUS FsWriteFile(
	IN PFILE_OBJECT FileObject,
	IN uint64_t FileOffset,
	IN void* Buffer,
	IN size_t BufferSize,
	_Out_Opt size_t* BytesWritten
)

{
	MOUNTED_FS* fs = vfs_find_fs_for_path(FileObject->FileName);
	if (!fs || !fs->driver || !fs->driver->WriteFile) return MT_NOT_IMPLEMENTED;

	return fs->driver->WriteFile(FileObject, FileOffset, Buffer, BufferSize, BytesWritten);
}

MTSTATUS FsDeleteFile(
	IN PFILE_OBJECT FileObject
)

{
	MOUNTED_FS* fs = vfs_find_fs_for_path(FileObject->FileName);
	if (!fs || !fs->driver || !fs->driver->DeleteFile) return MT_NOT_IMPLEMENTED;

	return fs->driver->DeleteFile(FileObject);
}

MTSTATUS FsListDirectory(
	IN PFILE_OBJECT DirectoryObject,
	OUT char* listings,
	IN size_t max_len
)

{
	MOUNTED_FS* fs = vfs_find_fs_for_path(DirectoryObject->FileName);
	if (!fs || !fs->driver || !fs->driver->ListDirectory) return MT_NOT_IMPLEMENTED;

	return fs->driver->ListDirectory(DirectoryObject, listings, max_len);
}

MTSTATUS FsCreateDirectory(
	IN  const char* path,
	OUT PHANDLE OutDirectoryObject
)

{
	MOUNTED_FS* fs = vfs_find_fs_for_path(path);

	if (!fs || !fs->driver || !fs->driver->CreateDirectory) return MT_NOT_IMPLEMENTED;

	PFILE_OBJECT OutDir;
	fs->driver->CreateDirectory(path, &OutDir);
	
	// Create a handle for the object.
	MTSTATUS Status = ObCreateHandleForObject(OutDir, MT_FILE_ALL_ACCESS, OutDirectoryObject);
	if (MT_FAILURE(Status)) {
		ObDereferenceObject(OutDir);
	}

	return Status;
}

MTSTATUS FsRemoveDirectoryRecursive(
	IN PFILE_OBJECT DirectoryObject
)

{
	MOUNTED_FS* fs = vfs_find_fs_for_path(DirectoryObject->FileName);
	if (!fs || !fs->driver || !fs->driver->RemoveDirectoryRecursive) return MT_NOT_IMPLEMENTED;

	return fs->driver->RemoveDirectoryRecursive(DirectoryObject);
}

MTSTATUS FsCreateFile(
	IN const char* path,
	IN ACCESS_MASK DesiredAccess,
	OUT PHANDLE FileHandleOut
)

{
	MOUNTED_FS* fs = vfs_find_fs_for_path(path);
	if (!fs || !fs->driver || !fs->driver->CreateFile) return MT_NOT_IMPLEMENTED;
	PFILE_OBJECT FileObject = NULL;
	MTSTATUS Status = fs->driver->CreateFile(path, &FileObject);
	if (MT_FAILURE(Status)) return Status;

	// File opened.
	// Create handle.
	Status = ObCreateHandleForObject(FileObject, DesiredAccess, FileHandleOut);

	// The dereference should make the pointer count be 1 handle creation succeeded, or destroy the object if it failed.
	ObDereferenceObject(FileObject);
	return Status;
}