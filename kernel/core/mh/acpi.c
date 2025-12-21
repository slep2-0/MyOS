/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      ACPI Parsing & Implementation.
 * DEVELOPER:	 https://github.com/slep2-0
 */

#include "../../includes/mh.h"
#include "../../includes/mg.h"
#include "../../includes/efi.h"

static bool validate_acpi_chksum(uint8_t* data, size_t len) {
	uint8_t sum = 0;
	// acpi checksums work by 8 bit addition, if it results 0, the checksum is valid.
	for (size_t i = 0; i < len; i++) {
		sum += data[i];
	}
	return sum == 0;
}

extern BOOT_INFO boot_info_local;

static void map_physical_range(uint64_t phys, size_t length, uint32_t flags) {
	uint64_t start = phys & 0xFFFULL;
	uint64_t end = ((phys + length) + VirtualPageSize - 1) & 0xFFFULL;
	for (uint64_t p = start; p < end; p += VirtualPageSize) {
		uintptr_t v = (p + PhysicalMemoryOffset);
		PMMPTE pte = MiGetPtePointer(v);
		MI_WRITE_PTE(pte, v, p, flags);
	}
}

static void* MiFindACPIHeader(XSDT* xsdt, const char* headerSignature) {
	uint32_t xsdt_len = xsdt->h.Length;
	if (xsdt_len < sizeof(ACPI_SDT_HEADER)) return NULL;
	uint32_t entries = (xsdt_len - sizeof(ACPI_SDT_HEADER)) / sizeof(uint64_t);
#ifdef DEBUG
	gop_printf(COLOR_RED, "Amount of ACPI Entries: %d\n", entries);
#endif
	for (uint32_t i = 0; i < entries; ++i) {
		uint64_t headerPhys = xsdt->Entries[i]; 
		map_physical_range(headerPhys, sizeof(ACPI_SDT_HEADER), PAGE_PRESENT | PAGE_RW | PAGE_PCD);
		ACPI_SDT_HEADER* hdr = (ACPI_SDT_HEADER*)(headerPhys + PhysicalMemoryOffset);

		// check signature quickly
		if (kmemcmp(hdr->Signature, headerSignature, 4) == 0) {
#ifdef DEBUG
			gop_printf(COLOR_RED, "Iteration %d, signature valid.\n", i);
#endif
			uint32_t table_len = hdr->Length;
			if (table_len < sizeof(ACPI_SDT_HEADER)) {
#ifdef DEBUG
				gop_printf(COLOR_RED, "Iteration %d, table_len < sizeof(ACPI_SDT_HEADER), continuing...\n", i); 
#endif
				continue; // invalid length
			}
			// map full table
			map_physical_range(headerPhys, table_len, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
			// validate table checksum
			if (!validate_acpi_chksum((uint8_t*)hdr, table_len)) { gop_printf(COLOR_RED, "ACPI Checksum invalid..\n"); continue; }
#ifdef DEBUG
			gop_printf(COLOR_LIME, "Returning specified header at pointer %p\n", hdr);
#endif
			return (void*)hdr;
		}
#ifdef DEBUG
		else {
			gop_printf(COLOR_RED, "Signature for iteration %d isn't valid... Pointer (physical): %p\n", i, (void*)(uintptr_t)headerPhys);
		}
#endif
	}
#ifdef DEBUG
	gop_printf(COLOR_RED, "Exhausted all iterations, returning NULL.\n");
#endif
	return NULL;
}

// Global table definitions
FADT* fadt;
MADT* madt;

void 
MhRebootComputer (
	void
) 

/*++

	Routine description : This function cold resets the computer, it does not return. Call appropriate cleanup functions before calling this function.

	Arguments:

		None.

	Return Values:

		None.

--*/

{
	if (!fadt) return;
	if (fadt->ResetReg.Address == 0) {
		gop_printf(COLOR_RED, "No ACPI Reset Register present.\n");
		return;
	}
	uint16_t port;
	uint64_t phys;
	switch (fadt->ResetReg.AddressSpace) {
	case 1: // System I/O
		port = (uint16_t)fadt->ResetReg.Address;
		__outbyte(port, fadt->ResetValue);
		break;
	case 0: // Memory mapped (MMIO)
		phys = fadt->ResetReg.Address;
		// Map one page containing the reset register itself.
		PMMPTE pte = MiGetPtePointer((uintptr_t)phys);
		MI_WRITE_PTE(pte, phys, phys, PAGE_PRESENT | PAGE_RW | PAGE_PWT | PAGE_PCD);
		volatile uint8_t* reg = (volatile uint8_t*)phys;
		*reg = fadt->ResetValue;
		break;
	default:
		gop_printf(COLOR_RED, "Unsupported ACPI reset AddressSpace: %d\n",
			fadt->ResetReg.AddressSpace);
		break;
	}

}

MTSTATUS MhParseLAPICs(uint8_t* buffer, size_t maxCPUs, uint32_t* cpuCount, uint32_t* lapicAddress) {
	if (!madt) return MT_NO_RESOURCES;
	// Deference the lapicAddress ptr and put the lapic address for each CPU there.
	*lapicAddress = madt->lapicAddress;
	size_t count = 0;
	// The pointer right after the MADT table (according to the spec)
	uint8_t* ptr = (uint8_t*)madt + sizeof(MADT);
	// The end of pointer is the length of the whole table itself.
	uint8_t* end = (uint8_t*)madt + madt->h.Length;

	while (ptr < end && count < maxCPUs) {
		uint8_t type = ptr[0];
		uint8_t len = ptr[1];

		if (type == MADT_LAPIC) {
			// Found a LAPIC table.
			MADT_LOCAL_APIC* lapic = (MADT_LOCAL_APIC*)ptr;
			if (lapic->Flags & 1) {
				// Enabled
				gop_printf(COLOR_LIME, "Found a CPU with LAPIC ID %d\n", lapic->ApicId);
				buffer[count++] = lapic->ApicId; // Store the APIC ID.
			}
		}

		ptr += len;
	}

	if (count > 0) {
		*cpuCount = count;
		return MT_SUCCESS;
	}
	else return MT_GENERAL_FAILURE;
}

MTSTATUS MhInitializeACPI(void) {
	uintptr_t rsdpPhys = boot_info_local.AcpiRsdpPhys;
	if (!rsdpPhys) return MT_INVALID_ADDRESS;
	map_physical_range(rsdpPhys, sizeof(RSDP_Descriptor), PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	RSDP_Descriptor* rsdp = (RSDP_Descriptor*)(rsdpPhys + PhysicalMemoryOffset);

	// Validate signatures and checksums.
	if (kmemcmp(rsdp->Signature, "RSD PTR ", 8) != 0) return (MTSTATUS)0xC000BABE;
	if (!validate_acpi_chksum((uint8_t*)rsdp, 20)) return MT_INVALID_CHECK;
	uint64_t xsdtPhys = 0;
	if (rsdp->Revision >= 2 && rsdp->Length >= sizeof(RSDP_Descriptor)) {
		// map whole RSDP length
		map_physical_range(rsdpPhys, rsdp->Length, PAGE_PRESENT | PAGE_RW | PAGE_PCD);
		if (!validate_acpi_chksum((uint8_t*)rsdp, rsdp->Length)) return MT_INVALID_CHECK;
		xsdtPhys = rsdp->XsdtAddress;
	}
	if (!xsdtPhys) return (MTSTATUS)0xC000BEEF;

	// Map XSDT header first so we can read its Length
	map_physical_range(xsdtPhys, sizeof(ACPI_SDT_HEADER), PAGE_PRESENT | PAGE_RW | PAGE_PCD);
	XSDT* xsdt = (XSDT*)(xsdtPhys + PhysicalMemoryOffset);
	if (xsdt->h.Length < sizeof(ACPI_SDT_HEADER)) return MT_INVALID_STATE;

	// Map the entire XSDT table
	map_physical_range(xsdtPhys, xsdt->h.Length, PAGE_PRESENT | PAGE_RW | PAGE_PCD);

	// Now find FACP (FADT) (the function checks for checksum automatically)
	ACPI_SDT_HEADER* facp = (ACPI_SDT_HEADER*)MiFindACPIHeader(xsdt, "FACP");
	if (!facp) return MT_NOT_FOUND;

	// put in the global FADT ptr
	fadt = (FADT*)facp;

	// Find MADT
	ACPI_SDT_HEADER* madtACPI = (ACPI_SDT_HEADER*)MiFindACPIHeader(xsdt, "APIC");
	if (!madtACPI) return MT_NOT_FOUND;

	madt = (MADT*)madtACPI;

	return MT_SUCCESS;
}

