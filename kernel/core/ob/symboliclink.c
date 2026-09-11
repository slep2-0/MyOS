/*++

Module Name:

	symboliclink.c

Purpose:

	This translation unit contains the implementation of the object symbolic links.

Author:

	slep (Matanel) 2026.

Revision History:

--*/

#include "../../includes/ob.h"
#include "../../../shared/include/mtstatus.h"
#include "../../../shared/include/accessrights.h"
#include "../../assert.h"

POBJECT_TYPE ObSymbolicLinkType = NULL;

static
void
ObpDeleteSymbolicLink(
	void* Object
)

{
	assert(Object);

	// Free the name that came with the symbolic link
	POBJECT_SYMBOLIC_LINK Symbolic = Object;

	if (Symbolic->TargetPath) {
		MmFreePool(Symbolic->TargetPath);
	}

	// Object Deleted on return
}

static
MTSTATUS
ObpParseSymbolicLink(
	IN void* ParseObject,
	IN const char* RemainingPath,
	IN size_t RemainingPathLength,
	IN void* ParseContext,
	OUT POB_PARSE_RESULT Result
);

MTSTATUS
ObpInitializeSymbolicLinkType(
	void
)

{
	if (ObSymbolicLinkType != NULL) {
		// Symbolic Link Type already initialized, bugcheck.
		// This should bugcheck on return.
		return MT_ALREADY_EXISTS;
	}

	MTSTATUS Status;
	OBJECT_TYPE_INITIALIZER ObjectTypeInitializer;
	kmemset(&ObjectTypeInitializer, 0, sizeof(OBJECT_TYPE_INITIALIZER));

	const char* Name = "Symbolic Link";
	ObjectTypeInitializer.PoolType = PagedPool;
#ifdef DEBUG
	ObjectTypeInitializer.DumpProcedure = NULL; // TODO DUMP PROC!
#else
	ObjectTypeInitializer.DumpProcedure = NULL;
#endif
	ObjectTypeInitializer.DeleteProcedure = &ObpDeleteSymbolicLink;
	ObjectTypeInitializer.ParseProcedure = &ObpParseSymbolicLink;
	ObjectTypeInitializer.ValidAccessRights = MT_SYMBOLIC_LINK_ALL_ACCESS;
	Status = ObCreateObjectType(Name, &ObjectTypeInitializer, &ObSymbolicLinkType);

	return Status;
}

static
MTSTATUS
ObpParseSymbolicLink(
	IN void* ParseObject,
	IN const char* RemainingPath,
	IN size_t RemainingPathLength,
	IN void* ParseContext,
	OUT POB_PARSE_RESULT Result
)

{
	assert(ParseObject);
	assert(Result);
	assert(RemainingPath || RemainingPathLength == 0);

	(void)ParseContext;

	// Guard, even though it shouldn't happen.
	if (!Result) {
		return MT_INVALID_PARAM;
	}

	// Zero out result
	kmemset(Result, 0, sizeof(OB_PARSE_RESULT));

	if (!ParseObject || (!RemainingPath && RemainingPathLength != 0)) {
		return MT_INVALID_PARAM;
	}

	// The object given is a symbolic link
	POBJECT_SYMBOLIC_LINK Link = ParseObject;
	assert(Link->TargetPath && Link->TargetPathLength != 0);

	if (!Link->TargetPath || Link->TargetPathLength == 0) {
		return MT_INVALID_PARAM;
	}

	// Determine if one of the targets has a slash that would join them for us
	// Why? Because we are joining the 2 paths together so
	// (backslashes are replaced with forward because of visual studio formatting annoyance)
	// STRING 1: Device/HardDiskVolume1
	// STRING 2: Users/Administrator
	// If we join these 2 together, it would be: Device/HardDiskVolume1Users/Administrator
	// Which you can see, does not include a slash between 1 and U, which is an invalid path to return
	// So, we would inspect if the strings already have one inserted for us (like /Device/HardDiskVolume/, or /Users/Admin...)
	// Which means we wouldnt have to insert it ourselves
	// But if they BOTH have a slash inserted, RemainingOffset skips the one at the start of the remaining path.
	bool TargetHasSlash =
		Link->TargetPathLength != 0 &&
		Link->TargetPath[Link->TargetPathLength - 1] == '\\';

	bool RemainingHasSlash =
		RemainingPathLength != 0 &&
		RemainingPath[0] == '\\';

	// Determine whether to skip one of the slashes so no double backslashes
	size_t RemainingOffset = TargetHasSlash && RemainingHasSlash ? 1 : 0;
	size_t SeparatorLength = !TargetHasSlash &&
		RemainingPathLength != 0 &&
		!RemainingHasSlash ? 1 : 0;

	size_t CopiedRemainingLength = RemainingPathLength - RemainingOffset;

	// Anti-0verflow checks
	if (Link->TargetPathLength > SIZE_MAX - SeparatorLength) {
		return MT_INVALID_PARAM;
	}

	size_t CombinedLength = Link->TargetPathLength + SeparatorLength;

	if (CombinedLength > SIZE_MAX - CopiedRemainingLength) {
		return MT_INVALID_PARAM;
	}

	CombinedLength += CopiedRemainingLength;

	// CombinedLength + 1 would overflow
	if (CombinedLength == SIZE_MAX) {
		return MT_INVALID_PARAM;
	}

	// We can finally allocate the string, allocate the combined length of the strings
	// + 1 for a NULL term.
	char* CombinedPath = MmAllocatePoolWithTag(PagedPool, CombinedLength + 1, 'SYMP'); // Simp, wait, Symbolic Path
	if (!CombinedPath) return MT_NO_MEMORY;

	// null terminate before operation
	CombinedPath[CombinedLength] = '\0';

	// Copy the target path AND the remaining path (in that order) while ensuring 1 joining slash.
	kmemcpy(CombinedPath, Link->TargetPath, Link->TargetPathLength);

	// Insert a joining slash when neither path supplies one.
	if (SeparatorLength != 0) {
		CombinedPath[Link->TargetPathLength] = '\\';
	}

	// Append the remaining path, skipping its leading slash if the target already ends in one.
	if (CopiedRemainingLength != 0) {
		kmemcpy(
			CombinedPath + Link->TargetPathLength + SeparatorLength,
			RemainingPath + RemainingOffset,
			CopiedRemainingLength
		);
	}

	// Path reparsed, return it to the caller now
	Result->ReparsePath = CombinedPath;
	Result->ReparsePathLength = CombinedLength;
	// Result->Object is left NULL, because what am I gonna set

	return MT_REPARSE;
}

MTSTATUS
ObCreateSymbolicLink(
	IN POBJECT_DIRECTORY StartingDirectory,
	IN const char* Path,
	IN size_t PathLength,
	IN uint32_t Attributes,
	IN const char* TargetPath,
	IN size_t TargetPathLength,
	OUT POBJECT_SYMBOLIC_LINK* SymbolicLinkObject
)

{
	if (!Path || !PathLength || !TargetPath || !TargetPathLength || !SymbolicLinkObject) {
		return MT_INVALID_PARAM;
	}

	*SymbolicLinkObject = NULL;	

	// TargetPath MUST be absolute, since reparsing starts from the root
	if (TargetPath[0] != '\\') {
		return MT_INVALID_PARAM;
	}

	// Since allocation is TargetPathLength + 1 to include a null terminator, we need to avoid overflow
	if (TargetPathLength == SIZE_MAX) {
		return MT_INVALID_PARAM;
	}

	// Create the symbiote
	POBJECT_SYMBOLIC_LINK Link = NULL;
	MTSTATUS Status = ObCreateObject(ObSymbolicLinkType, sizeof(OBJECT_SYMBOLIC_LINK), (void**)&Link);
	if (MT_FAILURE(Status)) return Status;

	// Allocate the TargetPath to put in the link struct
	char* AllocatedPath = MmAllocatePoolWithTag(PagedPool, TargetPathLength + 1, 'SYMP'); // Symbolic Path
	if (!AllocatedPath) {
		ObDereferenceObject(Link);
		return MT_NO_MEMORY;
	}

	// Copy the path now
	kmemcpy(AllocatedPath, TargetPath, TargetPathLength);

	// NULL Term
	AllocatedPath[TargetPathLength] = '\0';

	// Set the members
	Link->TargetPath = AllocatedPath;
	Link->TargetPathLength = TargetPathLength;

	// Publish to the directory
	Status = ObpInsertNamedObject(Link, StartingDirectory, Path, PathLength, Attributes);
	if (MT_FAILURE(Status)) {
		ObDereferenceObject(Link);
		// No need to MmFreePool the allocated path, the object dereference does that.
		return Status;
	}

	// Successful
	*SymbolicLinkObject = Link;
	return MT_SUCCESS;
}