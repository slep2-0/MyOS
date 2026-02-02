/*++

Module Name:

    fs.h

Purpose:

    This module contains the header files & prototypes required for filesystem operation in the kernel.

Author:

    slep (Matanel) 2025.

Revision History:

--*/

#ifndef X86_MATANEL_FS_H
#define X86_MATANEL_FS_H

#include "../mtstatus.h"
#include "ob.h"

#define MAX_PATH 256

#define MT_FILE_READ_DATA            0x0001  // file & pipe
#define MT_FILE_LIST_DIRECTORY       0x0001  // directory

#define MT_FILE_WRITE_DATA           0x0002  // file & pipe
#define MT_FILE_ADD_FILE             0x0002  // directory

#define MT_FILE_APPEND_DATA          0x0004  // file
#define MT_FILE_ADD_SUBDIRECTORY     0x0004  // directory
#define MT_FILE_CREATE_PIPE_INSTANCE 0x0004  // named pipe

#define MT_FILE_READ_EA              0x0008  // file & directory
#define MT_FILE_WRITE_EA             0x0010  // file & directory

#define MT_FILE_EXECUTE              0x0020  // file
#define MT_FILE_TRAVERSE             0x0020  // directory

#define MT_FILE_DELETE_CHILD         0x0040  // directory

#define MT_FILE_READ_ATTRIBUTES      0x0080  // all
#define MT_FILE_WRITE_ATTRIBUTES     0x0100  // all
#define MT_FILE_ALL_ACCESS           0x01FF  // everything above

#define MT_FILE_GENERIC_READ  ( MT_FILE_READ_DATA    | MT_FILE_READ_ATTRIBUTES | MT_FILE_READ_EA )
#define MT_FILE_GENERIC_WRITE ( MT_FILE_WRITE_DATA   | MT_FILE_WRITE_ATTRIBUTES | MT_FILE_WRITE_EA | MT_FILE_APPEND_DATA )
#define MT_FILE_GENERIC_EXECUTE ( MT_FILE_READ_ATTRIBUTES | MT_FILE_EXECUTE )

// PFILE_OBJECT->Flags
typedef enum _MT_FILE_OBJECT_FLAGS {
    MT_FOF_NONE = 0x00000000, // no flags

    // Basic object types, if MT_FOF_DIRECTORY bit is set, it is a directory, otherwise, it is a file.
    MT_FOF_DIRECTORY = 0x00000001, // object is a directory
    MT_FOF_READ_ONLY = 0x00000002, // DOS readonly attribute (persisted)
    MT_FOF_HIDDEN = 0x00000004, // DOS hidden (persisted)
    MT_FOF_SYSTEM = 0x00000008, // DOS system (persisted)
    MT_FOF_VOLUME_LABEL = 0x00000010, // volume label entry (rare)
    MT_FOF_ARCHIVE = 0x00000020, // DOS archive bit (persisted)

    // Storage attributes
    MT_FOF_COMPRESSED = 0x00000040, // file is compressed (on-disk)
    MT_FOF_ENCRYPTED = 0x00000080, // file is encrypted (on-disk)
    MT_FOF_SPARSE = 0x00000100, // sparse file support
    MT_FOF_TEMPORARY = 0x00000200, // temporary file (do not write to disk eagerly)
    MT_FOF_OFFLINE = 0x00000400, // data moved offline / recall needed

    // Lifecycle object.
    MT_FOF_APPEND_ONLY = 0x00000800, // all writes forced to EOF
    MT_FOF_IMMUTABLE = 0x00001000, // contents cannot be changed (readonly at FS level)
    MT_FOF_DELETE_ON_CLOSE = 0x00002000, // mark to delete when last handle closes
    MT_FOF_DELETE_PENDING = 0x00004000, // already unlinked; still open by handles

    // IO
    MT_FOF_NO_CACHE = 0x00008000, // do not cache in page cache
    MT_FOF_WRITE_THROUGH = 0x00010000, // writes bypass cache (write-through)
    MT_FOF_REPARSE_POINT = 0x00020000, // entry is a reparse point / junction / symlink

    // State markers
    MT_FOF_LOCKED = 0x00040000, // someone holds exclusive FS-level lock (useful for mkdir/rmdir)
    MT_FOF_DIRTY_METADATA = 0x00080000, // metadata changed and needs flush.

    // Reserved.
    MT_FOF_RESERVED_1 = 0x00100000,
    MT_FOF_RESERVED_2 = 0x00200000,
    MT_FOF_RESERVED_3 = 0x00400000,

    // Reserved.
    MT_FOF_FS_RESERVED_START = 0x01000000,
    MT_FOF_FS_RESERVED_MASK = 0xFF000000u
} MT_FILE_OBJECT_FLAGS;

// ------------------ STRUCTURES ------------------

typedef struct _FILE_OBJECT {
    // Name of the file. (full path)
    char* FileName;

    // Filesystem-specific context (e. first cluster number of file/dir in our FAT32)
    void* FsContext;

    // Size of the file in bytes
    uint64_t FileSize;

    // Current read/write position in file
    uint64_t CurrentOffset;

    // Flags: directory, read-only, etc.
    uint32_t Flags;
} FILE_OBJECT, * PFILE_OBJECT;

typedef struct FS_DRIVER {
    // Initialize driver for a device
    MTSTATUS(*init)(uint8_t device_id);
    MTSTATUS(*ReadFile)(IN PFILE_OBJECT FileObject,
        IN uint64_t FileOffset,
        OUT void* Buffer,
        IN size_t BufferSize,
        _Out_Opt size_t* BytesRead);
    MTSTATUS(*WriteFile)(IN PFILE_OBJECT FileObject,
        IN uint64_t FileOffset,
        IN void* Buffer,
        IN size_t BufferSize,
        _Out_Opt size_t* BytesWritten);
    MTSTATUS(*DeleteFile)(IN PFILE_OBJECT FileObject);
    MTSTATUS(*ListDirectory)(IN PFILE_OBJECT DirectoryObject,
        OUT char* listings,
        IN size_t max_len);
    MTSTATUS(*RemoveDirectoryRecursive)(IN PFILE_OBJECT DirectoryObject);
    MTSTATUS(*CreateDirectory)(
        IN  const char* path,
        OUT PFILE_OBJECT* OutDirectoryObject
        );
    MTSTATUS(*CreateFile)(IN const char* path,
        OUT PFILE_OBJECT* FileObjectOut);
    void(*DeleteObjectProcedure)(IN void* Object);

} FS_DRIVER;

// ------------------ FUNCTIONS ------------------
extern POBJECT_TYPE FsFileType;
typedef int32_t HANDLE, * PHANDLE;
typedef uint32_t ACCESS_MASK;

MTSTATUS FsInitialize(void);

MTSTATUS FsCreateFile(
    IN const char* path,
    IN ACCESS_MASK DesiredAccess,
    OUT PHANDLE FileHandleOut
);

MTSTATUS FsReadFile(
    IN PFILE_OBJECT FileObject,
    IN uint64_t FileOffset,
    OUT void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesRead
);

MTSTATUS FsWriteFile(
    IN PFILE_OBJECT FileObject,
    IN uint64_t FileOffset,
    IN void* Buffer,
    IN size_t BufferSize,
    _Out_Opt size_t* BytesWritten
);

MTSTATUS FsDeleteFile(
    IN PFILE_OBJECT FileObject
);

MTSTATUS FsListDirectory(
    IN PFILE_OBJECT DirectoryObject,
    OUT char* listings,
    IN size_t max_len
);

MTSTATUS FsCreateDirectory(
    IN  const char* path,
    OUT PHANDLE OutDirectoryObject
);

MTSTATUS FsRemoveDirectoryRecursive(
    IN PFILE_OBJECT DirectoryObject
);

#endif