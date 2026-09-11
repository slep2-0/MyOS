/*++

Module Name:

	directory.c

Purpose:

	This translation unit contains the implementation of the object manager namespace.

Author:

	slep (Matanel) 2026.

Revision History:

--*/

#include "../../includes/ob.h"
#include "../../assert.h"
#include "../../../shared/include/accessrights.h"

POBJECT_TYPE ObDirectoryType = NULL;
POBJECT_DIRECTORY ObRootDirectoryObject = NULL;
POBJECT_DIRECTORY ObAchtungNamedObjectsDirectory = NULL;
POBJECT_DIRECTORY ObDeviceDirectory = NULL;

// Convert an ASCII character to uppercase without using the C runtime.
FORCEINLINE
char 
ObpUppercaseAscii(
	char Character
)

{
	if (Character >= 'a' && Character <= 'z')
		return Character - ('a' - 'A');

	return Character;
}

static
uint32_t
ObpHashName(
	IN const char* Name,
	IN size_t NameLength
)

{
	if (!Name || !NameLength) return 0;

	uint32_t ResultedHash = 0;

	// Loop over the name, we treat each character as its uppercase equivalent
	for (size_t i = 0; i < NameLength; i++) {
		// Convert the character in the string to an uppercase one first.
		char Character = ObpUppercaseAscii(Name[i]);

		// Mix the accumulated hash, then incorporate this character. The hash
		// selects one bucket, exact name comparison selects the entry in its chain.
		ResultedHash += (ResultedHash << 1) + (ResultedHash >> 1);
		ResultedHash += (uint8_t)Character;
	}

	// Return the hash.
	return ResultedHash;
}

// requires shared push lock to be held
static
POBJECT_DIRECTORY_ENTRY
ObpFindDirectoryEntryLocked(
	IN POBJECT_DIRECTORY Directory,
	IN const char* Name,
	IN size_t NameLength,
	IN bool CaseInsensitive
)

{
	if (!Directory || !Name || !NameLength) return NULL;

	// We select the correct hash buckets that correspond to the name using its hash
	uint32_t NameHash = ObpHashName(Name, NameLength);

	POBJECT_DIRECTORY_ENTRY Entry = Directory->HashBuckets[NameHash % MT_OB_DIRECTORY_HASH_BUCKET_SIZE];
	POBJECT_DIRECTORY_ENTRY FoundEntry = NULL;

	while (Entry != NULL) {
		// Walk every entries in the chain until we find the correct one corresponding to the name.
		// Fast lookup first using cache values
		if (Entry->HashValue != NameHash) {
			goto Advance;
		}

		assert(Entry->Object,
			"A published directory entry must reference an object");

		// Hash matches, check if this is the correct entry.
		POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Entry->Object);

		POBJECT_HEADER_NAME_INFO NameInfo = Header->NameInfo;
		assert(NameInfo, "Object name info must be valid if a directory entry exists for the object");
		assert(NameInfo->Name,
			"A published directory entry must have an object name");
		assert(NameInfo->NameLength != 0,
			"A published directory entry must have a non-empty object name");
		assert(NameInfo->ParentDirectory == Directory,
			"Object name info must point back to the containing directory");

		// Validate the length of the strings matches
		if (NameInfo->NameLength != NameLength) {
			goto Advance;
		}

		// Name matches, validate this is the correct entry
		// by comparing the name, if the caller wants to compare case-insensitively then lets do so.
		for (size_t i = 0; i < NameLength; i++) {
			char CharacterGiven = Name[i];
			char CharacterEntry = NameInfo->Name[i];

			if (CaseInsensitive) {
				CharacterGiven = ObpUppercaseAscii(CharacterGiven);
				CharacterEntry = ObpUppercaseAscii(CharacterEntry);
			}

			// If they do not match this is not the entry
			if (CharacterEntry != CharacterGiven) {
				goto Advance;
			}
		}

		// Name matches, this IS the entry, return it.
		FoundEntry = Entry;
		break;

	Advance:
		Entry = Entry->Next;
		continue;
	}

	return FoundEntry;
}

static
bool
ObpInsertDirectoryEntryLocked(
	IN POBJECT_DIRECTORY Directory,
	IN POBJECT_DIRECTORY_ENTRY NewEntry,
	IN bool CaseInsensitive
)

{
	if (!Directory || !NewEntry) return false;
	// holy assertions
	assert(Directory, "A directory is required for insertion");
	assert(NewEntry, "A directory entry is required for insertion");
	assert(Directory->Lock.ExclusiveOwned && Directory->Lock.SharedOwners == 0,
		"The directory must be locked exclusively during insertion");
	assert(NewEntry->Next == NULL,
		"A new directory entry must not already belong to a bucket chain");
	assert(NewEntry->Object,
		"A new directory entry must reference an object");

	POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(NewEntry->Object);
	POBJECT_HEADER_NAME_INFO NameInfo = Header->NameInfo;
	assert(NameInfo,
		"The inserted object must have name information");
	assert(NameInfo->Name,
		"The inserted object must have a name buffer");
	assert(NameInfo->NameLength != 0,
		"The inserted object must have a non-empty name");
	assert(NameInfo->QueryReferences != 0,
		"The inserted object's name information must own a lifetime reference");
	assert(NameInfo->ParentDirectory == NULL,
		"An object being inserted must not already belong to a directory");
	assert(NewEntry->HashValue == ObpHashName(NameInfo->Name, NameInfo->NameLength),
		"The directory entry must cache the inserted object's name hash");

	// bad developer band aid is done, now we implement the function
	// first before inserting, we must verify the entry isnt already inserted

	if (ObpFindDirectoryEntryLocked(Directory, NameInfo->Name, NameInfo->NameLength, CaseInsensitive) != NULL) {
		// Entry is already in directory...
		return false;
	}

	// Alright, just insert into the bucket
	POBJECT_DIRECTORY_ENTRY Head = Directory->HashBuckets[NewEntry->HashValue % MT_OB_DIRECTORY_HASH_BUCKET_SIZE];

	// Insert the new entry to be the new head of the list
	NewEntry->Next = Head;

	// Set parent directory
	NameInfo->ParentDirectory = Directory;

	// Replace head
	Directory->HashBuckets[NewEntry->HashValue % MT_OB_DIRECTORY_HASH_BUCKET_SIZE] = NewEntry;

	// Good
	return true;
} 

static
POBJECT_DIRECTORY_ENTRY
ObpAllocateDirectoryEntry(
	IN void* Object
)

{
	if (!Object) return NULL;

	// The object must have an already prepared NameInfo struct ptr.
	POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
	assert(Header->NameInfo != NULL, "Caller violated term of not filling in NameInfo when allocating entry");
	assert(Header->NameInfo->Name);
	assert(Header->NameInfo->NameLength);

	// Allocate the entry now
	POBJECT_DIRECTORY_ENTRY Entry = MmAllocatePoolWithTag(PagedPool, sizeof(OBJECT_DIRECTORY_ENTRY), 'DIRE'); // Directory Object Entry
	if (!Entry) return NULL;

	// Initiaize the entry
	Entry->Next = NULL;
	Entry->Object = Object;
	Entry->HashValue = ObpHashName(Header->NameInfo->Name, Header->NameInfo->NameLength);
	
	// Return the allocated initialized entry
	return Entry;
}

static
MTSTATUS
ObpInsertDirectoryEntry(
	IN POBJECT_DIRECTORY Directory,
	IN void* Object,
	IN bool CaseInsensitive
)

{
	if (!Directory || !Object) return MT_INVALID_PARAM;

	// Allocate the entry before locking
	POBJECT_DIRECTORY_ENTRY Entry = ObpAllocateDirectoryEntry(Object);
	if (!Entry) return MT_NO_MEMORY;

	// Acquire the lock now
	MsAcquirePushLockExclusive(&Directory->Lock);

	// Reference the object
	if (!ObReferenceObject(Object)) {
		// Dead object
		MsReleasePushLockExclusive(&Directory->Lock);
		MmFreePool(Entry);
		return MT_OBJECT_DELETED;
	}

	// Insert the object into the list
	if (!ObpInsertDirectoryEntryLocked(Directory, Entry, CaseInsensitive)) {
		// Object is already inserted...
		MsReleasePushLockExclusive(&Directory->Lock);
		MmFreePool(Entry);
		// Dereference after releasing push lock so deletion routine wont acquire it.
		ObDereferenceObject(Object);
		return MT_ALREADY_EXISTS;
	}

	MsReleasePushLockExclusive(&Directory->Lock);
	return MT_SUCCESS;
}

// returns the object with a reference from the function
// you must dereference after manually, the remove function does it.
static
MTSTATUS
ObpLookupDirectoryEntry(
	IN POBJECT_DIRECTORY Directory,
	IN const char* Name,
	IN size_t NameLength,
	IN bool CaseInsensitive,
	OUT void** Object
)

{
	if (!Directory || !Name || !NameLength || !Object) return MT_INVALID_PARAM;

	// Initialize ptr before starting
	*Object = NULL;

	// Acquire directory shared lock to find the object
	MsAcquirePushLockShared(&Directory->Lock);

	POBJECT_DIRECTORY_ENTRY Entry = ObpFindDirectoryEntryLocked(Directory, Name, NameLength, CaseInsensitive);
	if (!Entry) {
		MsReleasePushLockShared(&Directory->Lock);
		return MT_NOT_FOUND;
	}

	// Reference its object
	if (!ObReferenceObject(Entry->Object)) {
		MsReleasePushLockShared(&Directory->Lock);
		return MT_OBJECT_DELETED;
	}

	// Return it
	*Object = Entry->Object;

	MsReleasePushLockShared(&Directory->Lock);
	return MT_SUCCESS;
}

static
POBJECT_DIRECTORY_ENTRY
ObpRemoveDirectoryEntryLocked(
	IN POBJECT_DIRECTORY Directory,
	IN void* Object
)

{
	if (!Directory || !Object) return NULL;
	assert(Directory->Lock.ExclusiveOwned && Directory->Lock.SharedOwners == 0,
		"The directory must be locked exclusively during removal");

	POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
	POBJECT_HEADER_NAME_INFO NameInfo = Header->NameInfo;
	assert(NameInfo,
		"A named object must have name information during directory removal");
	assert(NameInfo->Name,
		"A named object must have a name buffer during directory removal");
	assert(NameInfo->NameLength != 0,
		"A named object must have a non-empty name during directory removal");
	assert(NameInfo->ParentDirectory == Directory,
		"The removed object's name information must reference this directory");
	assert(NameInfo->QueryReferences != 0,
		"The removed object's name information must own a lifetime reference");

	// Loop through the list
	uint32_t HashValue = ObpHashName(NameInfo->Name, NameInfo->NameLength);
	uint32_t BucketIndex = HashValue % MT_OB_DIRECTORY_HASH_BUCKET_SIZE;
	POBJECT_DIRECTORY_ENTRY Entry = Directory->HashBuckets[BucketIndex];
	POBJECT_DIRECTORY_ENTRY FoundEntry = NULL;
	POBJECT_DIRECTORY_ENTRY* PrevNext = &Directory->HashBuckets[BucketIndex];

	while (Entry != NULL) {
		if (Entry->Object == Object) {
			// Unlink the entry from the list and set the previous pointer to point to the one after
			// basically RemoveEntryList...
			FoundEntry = Entry;
			*PrevNext = FoundEntry->Next;
			FoundEntry->Next = NULL;
			break;
		}

		PrevNext = &Entry->Next;
		Entry = Entry->Next;
	}
		
	return FoundEntry;
}

// Caller retains its reference; removal drops the directory-owned reference.
static
MTSTATUS
ObpRemoveDirectoryEntry(
	IN POBJECT_DIRECTORY Directory,
	IN void* Object
)

{
	if (!Directory || !Object) return MT_INVALID_PARAM;

	// Acquire exclusive lock, since we are actively writing stuff to the list
	MsAcquirePushLockExclusive(&Directory->Lock);

	POBJECT_DIRECTORY_ENTRY Entry = ObpRemoveDirectoryEntryLocked(Directory, Object);
	if (!Entry) {
		MsReleasePushLockExclusive(&Directory->Lock);
		return MT_NOT_FOUND;
	}

	// Set NameInfo->ParentDirectory to null as we removed the directory entry for this object
	// meaning it no longer has a connection to the namespace, so we null it out.
	OBJECT_TO_OBJECT_HEADER(Object)->NameInfo->ParentDirectory = NULL;

	// Release lock before freeing and returning.
	MsReleasePushLockExclusive(&Directory->Lock);

	// Free the entry
	MmFreePool(Entry);

	// Drop object reference
	ObDereferenceObject(Object);

	return MT_SUCCESS;
}

static
void
ObpDeleteDirectory(
	IN void* Object
)

{
	POBJECT_DIRECTORY Directory = Object;

	// Detach all entries in all of the 37 buckets the directory has.
	for (size_t i = 0; i < ARRAY_COUNTOF(Directory->HashBuckets); i++) {
		POBJECT_DIRECTORY_ENTRY Entry = Directory->HashBuckets[i];

		while (Entry != NULL) {
			// Save next pointer before removing the entry
			POBJECT_DIRECTORY_ENTRY SavedNext = Entry->Next;

			// Delete the entry
			MTSTATUS Status = ObpRemoveDirectoryEntry(Directory, Entry->Object);
			assert(MT_SUCCEEDED(Status));

			// Advance
			Entry = SavedNext;
		}
	}
}

POBJECT_HEADER_NAME_INFO
ObpReferenceNameInfo(
	IN POBJECT_HEADER Header
)

{
	if (!Header || !Header->NameInfo) return NULL;

	POBJECT_HEADER_NAME_INFO NameInfo = Header->NameInfo;

	// Use CAS loop
	// If the REMOVAL bit is set, NULL
	// If the count is zero, NULL
	// Otherwise, increment count
	volatile uint32_t References = InterlockedLoad(&NameInfo->QueryReferences);

	if (References == 0) {
		return NULL;
	}

	if ((InterlockedLoad(&NameInfo->QueryReferences) & MT_OB_NAME_INFO_REMOVING) != 0) {
		return NULL;
	}

	volatile uint32_t ExpectedValue = References;

	while (true) {
		// The CAS loop itself
		if ((ExpectedValue & MT_OB_NAME_INFO_REMOVING))
			return NULL;

		if (ExpectedValue == 0) {
			return NULL;
		}

		if ((ExpectedValue & MT_OB_NAME_INFO_REFERENCE_MASK) ==
			MT_OB_NAME_INFO_REFERENCE_MASK) {
			MeBugCheckEx(
				MEMORY_OVERFLOW_DETECTION,
				NameInfo,
				(void*)(uintptr_t)ExpectedValue,
				Header,
				RETADDR(0)
			);
		}

		uint32_t ObservedValue = InterlockedCompareExchangeU32(&NameInfo->QueryReferences, ExpectedValue + 1, ExpectedValue);

		// CAS Succeeded
		if (ExpectedValue == ObservedValue) {
			return NameInfo;
		}

		// CAS unsucessful, someone modified the references before us
		// Update with new value
		ExpectedValue = ObservedValue;
	}
}

void
ObpDereferenceNameInfo(
	IN POBJECT_HEADER_NAME_INFO NameInfo
)

{
	if (!NameInfo) {
		MeBugCheckEx(
			NULL_POINTER_DEREFERENCE,
			RETADDR(0),
			NULL,
			NULL,
			NULL
		);
	}

	volatile uint32_t References = InterlockedLoad(&NameInfo->QueryReferences);

	volatile uint32_t ExpectedValue = References;

	while (true) {
		// them cas loopz

		if ((ExpectedValue & MT_OB_NAME_INFO_REFERENCE_MASK) == 0) {
			// Dereference of an already zeroed reference count
			MeBugCheckEx(
				MEMORY_DOUBLE_FREE,
				NameInfo,
				(void*)(uintptr_t)ExpectedValue,
				RETADDR(0),
				NULL
			);
		}

		volatile uint32_t ObservedValue = InterlockedCompareExchangeU32(&NameInfo->QueryReferences, ExpectedValue - 1, ExpectedValue);

		uint32_t NewValue = ExpectedValue - 1;

		// Mask only the references, leave the removing bit behind.
		uint32_t NewActualRefCount = NewValue & MT_OB_NAME_INFO_REFERENCE_MASK;

		// NewActualRefCount - ONLY the UPDATED reference count of NameInfo
		// NewValue - the UPDATED reference count of NameInfo AND its removal bits

		if (ObservedValue == ExpectedValue && NewActualRefCount > 0) {
			// CAS Succeeded, and object isnt dead yet
			return;
		}

		// Why do we bugcheck at 0?
		// The OBJECT_HEADER itself holds the initial reference count to the NameInfo structure references
		// (i.e 1 at the start), if the count reaches 0 BUT the bit is not set, that means something decremented the initial
		// reference of the OBJECT_HEADER itself did to the nameinfo structure, i.e, double ObpDeref.
		// OR, wrong code in the OBJECT_HEADER did not set the bit even though it decremented the NameInfo structure reference
		if (ObservedValue == ExpectedValue && NewActualRefCount == 0 && (NewValue & MT_OB_NAME_INFO_REMOVING) == 0) {
			// CAS Succeeded, but object ownership disappeared incorrectly
			MeBugCheckEx(
				MEMORY_CORRUPT_HEADER,
				NameInfo,
				(void*)(uintptr_t)NewValue,
				RETADDR(0),
				NULL
			);
		}

		if (ObservedValue == ExpectedValue && NewActualRefCount == 0 && (NewValue & MT_OB_NAME_INFO_REMOVING) != 0) {
			// CAS Succeeded, object is dead, and ownership has successfuly set removing bit
			// free NameInfo->Name and NameInfo itself
			MmFreePool(NameInfo->Name);
			MmFreePool(NameInfo);
			return;
		}

		// CAS unsuccessful, set expected to the observed one and retry
		ExpectedValue = ObservedValue;
	}
}

void
ObpDeleteNameInfo(
	IN POBJECT_HEADER Header
)

{
	if (!Header) {
		MeBugCheckEx(
			NULL_POINTER_DEREFERENCE,
			RETADDR(0),
			NULL,
			NULL,
			NULL
		);
	}

	if (!Header->NameInfo) {
		return;
	}

	POBJECT_HEADER_NAME_INFO NameInfo = Header->NameInfo;

	// Verify the header is NO LONGER connected to its parent directory
	// it cannot be connected because when the header dies the connection to the directory (ptr) is nulled out.
	if (NameInfo->ParentDirectory) {
		MeBugCheckEx(
			MEMORY_CORRUPT_HEADER,
			Header,
			NameInfo,
			NameInfo->ParentDirectory,
			RETADDR(0)
		);
	}

	// SET the removing bit atomically.
	volatile uint32_t PreviousValue = InterlockedOrU32(&NameInfo->QueryReferences, MT_OB_NAME_INFO_REMOVING);

	if (PreviousValue & MT_OB_NAME_INFO_REMOVING) {
		// Someone set the bit before us
		// which should be impossible in normal runtime conditions
		// WE are the only code that sets it
		MeBugCheckEx(
			MEMORY_DOUBLE_FREE,
			NameInfo,
			(void*)(uintptr_t)PreviousValue,
			Header,
			RETADDR(0)
		);
	}

	if ((PreviousValue & MT_OB_NAME_INFO_REFERENCE_MASK) == 0) {
		MeBugCheckEx(
			MEMORY_CORRUPT_HEADER,
			NameInfo,
			(void*)(uintptr_t)PreviousValue,
			Header,
			RETADDR(0)
		);
	}

	// NULL Out NameInfo now.
	Header->NameInfo = NULL;

	// Do the final dereference.
	ObpDereferenceNameInfo(NameInfo);

	// OBJECT_HEADER is deleted at return.
}

MTSTATUS
ObpCreateNameInfo(
	IN POBJECT_HEADER Header,
	IN const char* Name,
	IN size_t NameLength
)

{
	// Reject SIZE_MAX because of NameLength + 1 below
	if (!Header || !Name || !NameLength || NameLength == SIZE_MAX) return MT_INVALID_PARAM;

	if (Header->NameInfo) {
		return MT_ALREADY_EXISTS;
	}

	// Allocate the name info structure
	// and a copy of the name from the pool.
	POBJECT_HEADER_NAME_INFO NameInfo = MmAllocatePoolWithTag(PagedPool, sizeof(OBJECT_HEADER_NAME_INFO), 'OBJN'); // Object name info, did i keep track of this?
	if (!NameInfo) return MT_NO_MEMORY;
	
	// NameLength + 1 because we want to include a null terminator for safety EVEN THOUGH all string checks for "Name" should depend on the NameLength member.
    char* NameAlloc = MmAllocatePoolWithTag(PagedPool, NameLength + 1, 'NAMA'); // Name Allocation
	if (!NameAlloc) {
		MmFreePool(NameInfo);
		return MT_NO_MEMORY;
	}

	// Copy the name string into the allocated one.
	// Use memcpy and not strcpy since we depend on NameLength
	kmemcpy(NameAlloc, Name, NameLength);

	// Manually NULL Terminate
	NameAlloc[NameLength] = '\0';

	// Set initial references.
	NameInfo->QueryReferences = MT_OB_NAME_INFO_INITIAL_REFERENCES;

	// Parent directory is initially NULL, the insertion sets the parent dir.
	NameInfo->ParentDirectory = NULL;

	// Set the name and name length in.
	NameInfo->Name = NameAlloc;
	NameInfo->NameLength = NameLength;

	// Plug into header and return
	Header->NameInfo = NameInfo;
	return MT_SUCCESS;
}

MTSTATUS
ObpInitializeDirectoryType(
	void
)

{
	if (ObDirectoryType != NULL) {
		// Directory Type already initialized, bugcheck.
		// This should bugcheck on return.
		return MT_ALREADY_EXISTS;
	}

	MTSTATUS Status;
	OBJECT_TYPE_INITIALIZER ObjectTypeInitializer;
	kmemset(&ObjectTypeInitializer, 0, sizeof(OBJECT_TYPE_INITIALIZER));

	// Directory
	const char* Name = "Directory";
	ObjectTypeInitializer.PoolType = PagedPool;
#ifdef DEBUG
	ObjectTypeInitializer.DumpProcedure = NULL; // TODO DUMP PROC!
#else
	ObjectTypeInitializer.DumpProcedure = NULL;
#endif
	ObjectTypeInitializer.DeleteProcedure = &ObpDeleteDirectory;
	ObjectTypeInitializer.ValidAccessRights = MT_DIRECTORY_ALL_ACCESS;
	Status = ObCreateObjectType(Name, &ObjectTypeInitializer, &ObDirectoryType);

	return Status;
}

MTSTATUS
ObpCreateRootDirectory(
	void
)

{
	assert(ObDirectoryType != NULL, "NULL Directory type, creation of root directory before initialization");
	assert(ObRootDirectoryObject == NULL, "Creation of root directory twice");

	POBJECT_DIRECTORY RootDirectory = NULL;

	// Create the object directory through ob.
	MTSTATUS Status = ObCreateObject(ObDirectoryType, sizeof(OBJECT_DIRECTORY), (void**) & RootDirectory);
	if (MT_FAILURE(Status)) return Status;

	// Initialize push lock
	// This is not mandatory as all push lock init does is zero stuff out
	// but, we do it as push locks could change in the future
	// For the object hash buckets itself, it is all zeroed out anyway.
	MsInitializePushLock(&RootDirectory->Lock);

	// Set header name info
	POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(RootDirectory);

	// Create it, root dir is always backslash.
	Status = ObpCreateNameInfo(Header, "\\", sizeof("\\") - 1);

	if (MT_FAILURE(Status)) {
		ObDereferenceObject(RootDirectory);
		return Status;
	}

	// ParentDirectory is NULL as it is the root directory, no parent there.
	// Set it as a permanent object
	Header->Flags |= MT_OB_FLAG_PERMANENT_OBJECT;

	// Publish the root directory
	ObRootDirectoryObject = RootDirectory;
	return MT_SUCCESS;
}

static MTSTATUS
ObpCreateDirectoryObject(
	IN POBJECT_DIRECTORY ParentDirectory,
	IN const char* Name,
	IN size_t NameLength,
	IN bool CaseInsensitive,
	OUT POBJECT_DIRECTORY* DirectoryObject
)

{
	if (!ParentDirectory || !Name || !NameLength || !DirectoryObject) return MT_INVALID_PARAM;

	// Initalize to NULL
	*DirectoryObject = NULL;

	// Create the Object Directory itself
	POBJECT_DIRECTORY Directory = NULL;

	MTSTATUS Status = ObCreateObject(ObDirectoryType, sizeof(OBJECT_DIRECTORY), (void**)&Directory);
	if (MT_FAILURE(Status)) return Status;

	// Initialize push lock
	MsInitializePushLock(&Directory->Lock);

	// Create its name info
	POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Directory);

	Status = ObpCreateNameInfo(Header, Name, NameLength);
	if (MT_FAILURE(Status)) {
		ObDereferenceObject(Directory);
		return Status;
	}

	// Insert into its parent directory
	Status = ObpInsertDirectoryEntry(ParentDirectory, Directory, CaseInsensitive);
	if (MT_FAILURE(Status)) {
		ObDereferenceObject(Directory);
		// DO NOT call MmFreePool for the NameInfo here as the dereference of the directory ultimately leads to the ObpDeleteNameInfo function.
		return Status;
	}

	// All good now, return.
	*DirectoryObject = Directory;
	return MT_SUCCESS;
}

MTSTATUS
ObpCreateAchtungNamedObjectsDirectory(
	void
)

{
	assert(ObAchtungNamedObjectsDirectory == NULL);
	assert(ObRootDirectoryObject != NULL);

	// Create local directory through the function
	POBJECT_DIRECTORY Directory = NULL;
	MTSTATUS Status = ObpCreateDirectoryObject(ObRootDirectoryObject, "AchtungNamedObjects", sizeof("AchtungNamedObjects") - 1, true, &Directory);
	if (MT_FAILURE(Status)) return Status;

	// Mark the header as permanent
	POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Directory);
	Header->Flags |= MT_OB_FLAG_PERMANENT_OBJECT;

	// Wire into global variable
	ObAchtungNamedObjectsDirectory = Directory;
	return MT_SUCCESS;
}

MTSTATUS
ObpCreateDeviceDirectory(
	void
)

{
	assert(ObDeviceDirectory == NULL);
	assert(ObRootDirectoryObject != NULL);

	// Create local directory through the function
	POBJECT_DIRECTORY Directory = NULL;
	MTSTATUS Status = ObpCreateDirectoryObject(ObRootDirectoryObject, "Device", sizeof("Device") - 1, true, &Directory);
	if (MT_FAILURE(Status)) return Status;

	// Mark the header as permanent
	POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Directory);
	Header->Flags |= MT_OB_FLAG_PERMANENT_OBJECT;

	// Wire into global variable
	ObDeviceDirectory = Directory;
	return MT_SUCCESS;
}

// Returns with "Object" referenced.
MTSTATUS
ObpLookupObjectPath(
	IN POBJECT_DIRECTORY StartingDirectory,
	IN const char* Path,
	IN size_t PathLength,
	IN uint32_t Attributes,
	IN void* ParseContext,
	OUT void** Object
)

{
	if (!Object || !Path || !PathLength ||
		(Attributes & ~MT_OBJ_VALID_ATTRIBUTES) != 0) {
		return MT_INVALID_PARAM;
	}

	*Object = NULL;
	bool CaseInsensitive = (Attributes & MT_OBJ_CASE_INSENSITIVE) != 0;

	// Determine if the path is relative from StartingDirectory
	// Or is an absolute path.
	bool IsAbsolutePath = (Path[0] == '\\');
	POBJECT_DIRECTORY CurrentDirectory = NULL;
	size_t PathIndex = 0;

	if (IsAbsolutePath && StartingDirectory != NULL) {
		// Absolute path given, even though StartingDirectory is supplied too.
		return MT_INVALID_PARAM;
	}

	if (!IsAbsolutePath && !StartingDirectory) {
		// Not an absolute path, and yet no starting directory is given for relative
		return MT_INVALID_PARAM;
	}

	// If the path given is only the root return the object as the root dir.
	if (PathLength == 1 && Path[0] == '\\') {
		bool Ok = ObReferenceObject(ObRootDirectoryObject);
		assert(Ok, "Reference of root directory failed at search");
		*Object = ObRootDirectoryObject;
		return MT_SUCCESS;
	}

	// If we are using an absolute path then start from the root
	// else, start from the directory given
	if (IsAbsolutePath) {
		assert(ObRootDirectoryObject, "Root directory not initialized when attempting directory search");
		CurrentDirectory = ObRootDirectoryObject;

		// Start parsing at index 1, skipping over the starting slash
		PathIndex = 1;
	}
	else {
		CurrentDirectory = StartingDirectory;
	}

	// Reference the directory so it will not die
	if (!ObReferenceObject(CurrentDirectory)) {
		// Directory is dead, its RIP bro
		return MT_OBJECT_DELETED;
	}

	// Add symbolic link reparse attempts above the label
	uint32_t ReparseAttempts = 0;
	char* OwnedReparsePath = NULL;

	// Start component loop now
	// All it does is scan until the end of the string or until '\' which signals that a component follows (and also that this should be a directory)
LoopOnceAgain:;
	bool HasMoreComponents = false;
	bool FinalComponent = false;
	size_t ComponentStart = PathIndex;
	size_t ComponentLength = 0;

	while (PathIndex < PathLength) {

		// If we have reached a backslash, aka the end of this directory component
		// then stop
		if (Path[PathIndex] == '\\') {

			// PathIndex points at the separator, so the distance from ComponentStart is the number of characters in this component.
			ComponentLength = PathIndex - ComponentStart;

			if (ComponentLength == 0) {
				ObDereferenceObject(CurrentDirectory);

				if (OwnedReparsePath) {
					// Free the reparse path from the symlink
					MmFreePool(OwnedReparsePath);
				}

				return MT_INVALID_PARAM;
			}

			// Before stopping, we must increment path index because if not
			// the next iteration will infinite loop saying its a component
			// well it wont infinite loop because ObpFind will return a bad status
			// but it will fail no matter what.
			PathIndex++;
			HasMoreComponents = true;

			// A final component and it being a directory is disallowed
			// Technically, the string is malformed too, since it ends with a slash, thus marking this a directory component (before check)
			if (PathIndex == PathLength) {
				ObDereferenceObject(CurrentDirectory);

				if (OwnedReparsePath) {
					// Free the reparse path from the symlink
					MmFreePool(OwnedReparsePath);
				}

				return MT_INVALID_PARAM;
			}

			break;
		}

		// Move on to next letter
		PathIndex++;
	}

	// Record if this is the final component or not
	FinalComponent = (PathIndex == PathLength);

	// If no separator was found, PathIndex reached PathLength, so the remaining distance is the final component's length.
	// Since the only other calculation is done in the separator (slash), so we compensate for it here
	if (!HasMoreComponents) {
		ComponentLength = PathIndex - ComponentStart;
	}

	// Lookup the compnent now
	void* ComponentObject = NULL;
	MTSTATUS Status = ObpLookupDirectoryEntry(CurrentDirectory, Path + ComponentStart, ComponentLength, CaseInsensitive, &ComponentObject);

	if (MT_FAILURE(Status)) {
		ObDereferenceObject(CurrentDirectory);

		if (OwnedReparsePath) {
			// Free the reparse path from the symlink
			MmFreePool(OwnedReparsePath);
		}

		return Status;
	}

	// Acquire the header of the object
	POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(ComponentObject);

	// This could be a symbolic link, if we ignored it, we would return a final component as a symbolic link
	// even though the object is still not found.
	// Also, did you know C: is a symbolic link? I did not until I began implementing this
	// I thought its just a "DOS" path so a different kind of path, technically, it is
	// but every DOS path will get reformatted into the NT path which is Device\HardDiskVolumeX
	// (considering its for the fs)
	bool OpenFinalSymbolicLink =
		FinalComponent &&
		Header->Type == ObSymbolicLinkType &&
		(Attributes & MT_OBJ_OPEN_LINK) != 0;

	if (Header->Type->TypeInfo.ParseProcedure && !OpenFinalSymbolicLink) {
		// Object is symbolic link (or another parsable object, like FS device), feed it to the parse procedure
		OB_PARSE_RESULT Result = { 0 };
		size_t RemainingStart = ComponentStart + ComponentLength;
		Status = Header->Type->TypeInfo.ParseProcedure(ComponentObject, Path + RemainingStart, PathLength - RemainingStart, ParseContext, &Result);

		if (MT_FAILURE(Status)) {
			if (Result.Object || Result.ReparsePath || Result.ReparsePathLength != 0) {
				MeBugCheckEx(
					OBJECT_PARSE_CONTRACT_VIOLATION,
					Header->Type,
					ComponentObject,
					(void*)(uintptr_t)Status,
					&Result
				);
			}

			// Symbolic link parse failure..
			ObDereferenceObject(ComponentObject);
			ObDereferenceObject(CurrentDirectory);

			if (OwnedReparsePath) {
				// Free the reparse path from the symlink
				MmFreePool(OwnedReparsePath);
			}

			return Status;
		}


		if (Status == MT_REPARSE) {
			// Validate callback results
			if (!Result.ReparsePath || Result.ReparsePathLength == 0 ||
				Result.Object != NULL || Result.ReparsePath[0] != '\\') {
				MeBugCheckEx(
					OBJECT_PARSE_CONTRACT_VIOLATION,
					Header->Type,
					ComponentObject,
					(void*)(uintptr_t)Status,
					&Result
				);
			}

			if (ReparseAttempts >= MT_OB_MAX_REPARSE_ATTEMPTS) {
				ObDereferenceObject(ComponentObject);
				ObDereferenceObject(CurrentDirectory);

				if (OwnedReparsePath) {
					// Free the reparse path from the symlink
					MmFreePool(OwnedReparsePath);
				}

				// Free the path the symbolic link parser gave
				MmFreePool(Result.ReparsePath);

				return MT_REPARSE_LIMIT_EXCEEDED;
			}

			ReparseAttempts++;

			// Set the component path as the new symbolic link path
			// First derefenrece both the directory and the object as we are outta there
			ObDereferenceObject(ComponentObject);
			ObDereferenceObject(CurrentDirectory);

			// Free the previous ReparsePath if any, since the reparse callback did an allocation
			if (OwnedReparsePath) {
				MmFreePool(OwnedReparsePath);
			}

			// Set the new reparse path to free for later
			OwnedReparsePath = Result.ReparsePath;

			// Now set the new path to search for next iteration
			Path = OwnedReparsePath;
			PathLength = Result.ReparsePathLength;

			bool Ok = ObReferenceObject(ObRootDirectoryObject);
			assert(Ok);

			// Before restarting the loop check if the caller just wants the root directory
			if (PathLength == 1 && Path[0] == '\\') {
				// They do
				MmFreePool(OwnedReparsePath);
				*Object = ObRootDirectoryObject;
				return MT_SUCCESS;
			}

			// Start from the root
			CurrentDirectory = ObRootDirectoryObject;

			// The new index is 1, as we are from the root
			PathIndex = 1;

			// JMP LoopOnceAgain
			goto LoopOnceAgain;
		}
		else if (Status == MT_SUCCESS) {
			if (!Result.Object || Result.ReparsePath || Result.ReparsePathLength != 0) {
				MeBugCheckEx(
					OBJECT_PARSE_CONTRACT_VIOLATION,
					Header->Type,
					ComponentObject,
					(void*)(uintptr_t)Status,
					&Result
				);
			}

			ObDereferenceObject(ComponentObject);
			ObDereferenceObject(CurrentDirectory);

			// Free the previous ReparsePath if any, since the reparse callback did an allocation
			if (OwnedReparsePath) {
				MmFreePool(OwnedReparsePath);
			}

			// Return the object the parser gave
			*Object = Result.Object;
			return MT_SUCCESS;
		}
		else {
			MeBugCheckEx(
				OBJECT_PARSE_CONTRACT_VIOLATION,
				Header->Type,
				ComponentObject,
				(void*)(uintptr_t)Status,
				&Result
			);
		}
	}
	
	if (FinalComponent) {
		// This is the component that the search wanted to find
		// return it
		// First dereference the directory
		ObDereferenceObject(CurrentDirectory);

		*Object = ComponentObject;

		if (OwnedReparsePath) {
			// Free the reparse path from the symlink
			MmFreePool(OwnedReparsePath);
		}

		// Ownership of reference transferred to caller from ObpLookup
		// we dont deref
		// This is the final return path of the function!!! (if all caller arguments are good)
		return MT_SUCCESS;
	}

	// More components remain
	// This should be a directory
	if (Header->Type != ObDirectoryType) {
		// This is not a directoy even though the path given has more components
		// that means the caller passed the wrong path, this is not a bugcheck
		// as this is user mode controlled too (or just bad coding, but it is not corruption)
		ObDereferenceObject(ComponentObject);
		ObDereferenceObject(CurrentDirectory);

		if (OwnedReparsePath) {
			// Free the reparse path from the symlink
			MmFreePool(OwnedReparsePath);
		}

		return MT_TYPE_MISMATCH;
	}

	// Dereference old directory, and set the current directory as the new one
	// then continue component search until we reach the actual object we want to find.
	ObDereferenceObject(CurrentDirectory);

	POBJECT_DIRECTORY NewDirectory = (POBJECT_DIRECTORY)ComponentObject;
	CurrentDirectory = NewDirectory;

	// All good now, continue search
	// I understand this is not readable, and that functions at the end are suposed to have a return and not a goto
	// But how my mind was coding the function it ended up being like this, if this will be major readability problem for me (or future readers who want to suggest)
	// Then I will rewrite the loop.
	// The final return of the function (if all parameters are correct) is return MT_SUCCESS at line 965, in if (FinalComponent)
	goto LoopOnceAgain;
}

// Returns SIZE_MAX when no slashes are found
static
size_t
ObpGetIteratorAtFinalSlash(
	IN const char* Path,
	IN size_t PathLength
)

{
	// Returns the index where the final slash parts
	// meaning Path[RetVal] == '\\' (final slash)
	size_t LastSlashFound = SIZE_MAX;

	for (size_t i = 0; i < PathLength; i++) {
		if (Path[i] == '\\') {
			LastSlashFound = i;
		}
	}

	return LastSlashFound;
}

MTSTATUS
ObpInsertNamedObject(
	IN void* Object,
	IN POBJECT_DIRECTORY StartingDirectory,
	IN const char* Path,
	IN size_t PathLength,
	IN uint32_t Attributes
)

{
	if (!Object || !Path || !PathLength) return MT_INVALID_PARAM;

	// Accept only valid attributes
	// OPEN_LINK is useless during insertion, so catch caller errors
	assert((Attributes & MT_OBJ_OPEN_LINK) == 0, "Caller passed MT_OBJ_OPEN_LINK to function which does not use it");
	if ((Attributes & ~MT_OBJ_CASE_INSENSITIVE) != 0) {
		return MT_INVALID_PARAM;
	}

	// Absolute paths cannot have a starting directory
	if (Path[0] == '\\' && StartingDirectory != NULL) {
		return MT_INVALID_PARAM;
	}

	// Relative paths must have a starting directory
	if (Path[0] != '\\' && StartingDirectory == NULL) {
		return MT_INVALID_PARAM;
	}

	POBJECT_DIRECTORY ParentDirectory; // Referenced

	// Leaf is the object, its name
	size_t LeafStart;
	size_t LeafLength;
	size_t FinalSlashIndex = ObpGetIteratorAtFinalSlash(Path, PathLength);

	if (FinalSlashIndex == SIZE_MAX) {
		// Path has no slashes, meaning it is the leaf directly
		// Reference StartingDirectory and thats it
		if (!ObReferenceObject(StartingDirectory)) {
			// Dead parent path
			return MT_OBJECT_DELETED;
		}

		ParentDirectory = StartingDirectory;

		// Set the leaf start and length to the path arguments, because thats the leaf
		LeafStart = 0;
		LeafLength = PathLength;
	}
	else {
		// Path has a slash (or multiple ones)
		LeafStart = FinalSlashIndex + 1;

		if (LeafStart == PathLength) {
			// Final slash is at the end of the string ("Leaf\")
			// Thats invalid since that means there is no actual leaf
			return MT_INVALID_PARAM;
		}

		if (FinalSlashIndex == 0) {
			// Root path
			// Set the starting directory as the root (and ref it)
			bool Ok = ObReferenceObject(ObRootDirectoryObject);
			assert(Ok, "Root directory is dead where it should be valid ref.");
			ParentDirectory = ObRootDirectoryObject;

			// Leaf length is just where we found the slash (plus one to skip over it), decremented from the path length
			LeafLength = PathLength - LeafStart;
		}
		else {
			// Non-Root path, reference parent with the lookup function
			LeafLength = PathLength - LeafStart;

			MTSTATUS Status = ObpLookupObjectPath(StartingDirectory, Path, FinalSlashIndex, Attributes, NULL, (void**)&ParentDirectory);

			if (MT_FAILURE(Status)) {
				return Status;
			}

			// ObpLookup already references ParentDirectory

		}
	}

	if (OBJECT_TO_OBJECT_HEADER(ParentDirectory)->Type != ObDirectoryType) {
		// Parent is not a directory..?
		ObDereferenceObject(ParentDirectory);
		return MT_TYPE_MISMATCH;
	}

	POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
	
	// Create the NameInfo for the object
	MTSTATUS Status = ObpCreateNameInfo(Header, Path + LeafStart, LeafLength);
	if (MT_FAILURE(Status)) {
		ObDereferenceObject(ParentDirectory);
		return Status;
	}

	// Call internal function to insert the object
	Status = ObpInsertDirectoryEntry(ParentDirectory, Object, Attributes & MT_OBJ_CASE_INSENSITIVE);
	if (MT_FAILURE(Status)) {
		// Dereference parent dir and destroy the name info
		ObpDeleteNameInfo(Header);
		ObDereferenceObject(ParentDirectory);
		return Status;
	}

	// Finally inserted, all good now.
	ObDereferenceObject(ParentDirectory);
	return MT_SUCCESS;
}

MTSTATUS
ObOpenObjectByName(
	IN POBJECT_DIRECTORY StartingDirectory,
	IN const char* Path,
	IN size_t PathLength,
	IN uint32_t Attributes,
	IN void* ParseContext,
	IN POBJECT_TYPE ExpectedType,
	IN ACCESS_MASK DesiredAccess,
	OUT PHANDLE Handle
)

{
	if (!Path || !PathLength || !Handle || !ExpectedType) return MT_INVALID_PARAM;

	// da big function
	// First init the ptr to an invalid value
	*Handle = MT_INVALID_HANDLE;

	// Lookup now
	void* Object = NULL;
	MTSTATUS Status = ObpLookupObjectPath(StartingDirectory, Path, PathLength, Attributes, ParseContext, &Object);
	if (MT_FAILURE(Status)) return Status;

	// Validate the object is the one the function requests
	POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);

	if (Header->Type != ExpectedType) {
		ObDereferenceObject(Object);
		return MT_TYPE_MISMATCH;
	}

	// Create the handle for the object
	Status = ObCreateHandleForObject(Object, DesiredAccess, Handle);

	// We dereference the object either way, since ObCreateHandle either is successful and adds another reference to the object (which we must decrement from the ObpLookup)
	// Or it fails, and we dereference the object so it explodes AKA gets deleted.
	// And mov eax, Status; RET
	ObDereferenceObject(Object);
	return Status;
}