/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      Virtual File System (VFS) Header Functions & Structures.
 */

#ifndef X86_VFS_H
#define X86_VFS_H

// Include the FAT32 Header.
#include "../fat32/fat32.h"

typedef enum _FS_WRITE_MODES {
	WRITE_MODE_APPEND_EXISTING,
	WRITE_MODE_CREATE_OR_REPLACE,
} FS_WRITE_MODES;

typedef struct FS_DRIVER {
    MTSTATUS(*init)(uint8_t device_id);
    MTSTATUS(*read)(const char* filename, uint32_t* file_size_out, void** buffer_out);
    MTSTATUS(*write)(const char* path, const void* data, uint32_t size, uint32_t mode);
    MTSTATUS(*delete)(const char* path);
    MTSTATUS(*mkdir)(const char* path);
    MTSTATUS(*rmdir)(const char* path);
    bool(*is_dir_empty)(const char* path);
    MTSTATUS(*listdir)(const char* path, char* listings, size_t max_len);
    void(*listrootdir)(void);
} FS_DRIVER;

/// <summary>
/// Initialize the Virtual File System (initializes other filesystem needed services as well)
/// </summary>
/// <returns>MTSTATUS Status Code</returns>
MTSTATUS vfs_init(void);

/// <summary>
/// Reads the file into a buffer.
/// </summary>
/// <param name="filename">The Filename to read, e.g "file.txt" or "tmp/folder/myfile.txt"</param>
/// <param name="file_size_out">A pointer to put the file size in bytes</param>
/// <param name="bufferOut">A pointer to put the file buffer in (doesn't need to be dynamically allocated)</param>
/// <returns>MTSTATUS Status Code.</returns>
MTSTATUS vfs_read(const char* filename, uint32_t* file_size_out, void** buffer_out);

/// <summary>
/// Creates a new file (or opens existing) and writes data to it.
/// </summary>
/// <param name="path">The full path of the file to create</param>
/// <param name="data">A pointer to the data to write.</param>
/// <param name="size">The number of bytes to write</param>
/// <param name="write_mode">Whether to APPEND or CREATE/REPLACE the file. (in FS_WRITE_MODES enum)</param>
/// <returns>MTSTATUS Status Code</returns>
MTSTATUS vfs_write(const char* path, const void* data, uint32_t size, FS_WRITE_MODES write_mode);

/// <summary>
/// This function deletes the file given to the function from the system.
/// </summary>
/// <param name="path">Full path to delete file.</param>
/// <returns>MTSTATUS Status code.</returns>
MTSTATUS vfs_delete(const char* path);

/// <summary>
/// Lists the directory given.
/// </summary>
/// <param name="path">Path to directory, e.g "mydir/" </param>
/// <param name="listings">[OUT] Pointer to directory listing. (each seperated with a newline character)</param>
/// <param name="max_len">[IN] Max size of listings buffer.</param>
/// <returns>MTSTATUS Status code.</returns>
MTSTATUS vfs_listdir(const char* path, char* listings, size_t max_len);

/// <summary>
/// Creates a new directory
/// </summary>
/// <param name="path">The full path to the new directory</param>
/// <returns>MTSTATUS Status code.</returns>
MTSTATUS vfs_mkdir(const char* path);

/// <summary>
/// This function deletes the directory given to the function from the system.
/// </summary>
/// <param name="path">Full path to delete directory.</param>
/// <returns>MTSTATUS Status code.</returns>
MTSTATUS vfs_rmdir(const char* path);

/// <summary>
/// This function returns if the directory given to the function is empty (e.g, has only '.' and '..' entries)
/// </summary>
/// <param name="path">Full path to dir</param>
/// <returns>True or false based if empty or not.</returns>
bool vfs_is_dir_empty(const char* path);

/// <summary>
/// This function will list the root directory of the main mount device.
/// </summary>
void vfs_listrootdir(void);

#endif