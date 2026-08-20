/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      AHCI Driver Implementation.
 */

#include "ahci.h"
#include "../../assert.h"
#include "../../includes/mg.h"
#include "../../includes/mm.h"

#ifdef REMINDER
_Static_assert(false, "Reminder: AHCI, and other DMA stuff DEAL WITH PHYSICAL ADDRESSES ONLY! not virtual, so supply to them the translated addresses.");
#endif

//#define AHCI_DEBUG_PRINT

#ifdef AHCI_DEBUG_PRINT
static void decode_serr(uint32_t serr) {
    if (serr == 0) {
        // gop_printf(0xFFFFFF00, "SERR: No errors\n");
        return;
    }

    // gop_printf(0xFFFF0000, "SERR: 0x%08x - Errors detected:\n", serr);

    // ERR bits (0-15) - Recoverable and non-recoverable errors
    //if (serr & (1 << 0))  // gop_printf(0xFFFFFF00, "  [0] ERR.I - Recovered Data Integrity Error\n");
    //if (serr & (1 << 1))  // gop_printf(0xFFFFFF00, "  [1] ERR.M - Recovered Communications Error\n");
    //if (serr & (1 << 8))  // gop_printf(0xFFFFFF00, "  [8] ERR.T - Transient Data Integrity Error\n");
    //if (serr & (1 << 9))  // gop_printf(0xFFFFFF00, "  [9] ERR.C - Persistent Communication/Data Integrity Error\n");
    //if (serr & (1 << 10)) // gop_printf(0xFFFFFF00, "  [10] ERR.P - Protocol Error\n");
    //if (serr & (1 << 11)) // gop_printf(0xFFFFFF00, "  [11] ERR.E - Internal Error\n");
    //
    //// DIAG bits (16-31) - Diagnostic errors
    //if (serr & (1 << 16)) // gop_printf(0xFFFFFF00, "  [16] DIAG.N - PhyRdy Change\n");
    //if (serr & (1 << 17)) // gop_printf(0xFFFFFF00, "  [17] DIAG.I - Phy Internal Error\n");
    //if (serr & (1 << 18)) // gop_printf(0xFFFFFF00, "  [18] DIAG.W - Comm Wake\n");
    //if (serr & (1 << 19)) // gop_printf(0xFFFFFF00, "  [19] DIAG.B - 10B to 8B Decode Error\n");
    //if (serr & (1 << 20)) // gop_printf(0xFFFFFF00, "  [20] DIAG.D - Disparity Error\n");
    //if (serr & (1 << 21)) // gop_printf(0xFFFFFF00, "  [21] DIAG.C - CRC Error\n");
    //if (serr & (1 << 22)) // gop_printf(0xFFFFFF00, "  [22] DIAG.H - Handshake Error\n");
    //if (serr & (1 << 23)) // gop_printf(0xFFFFFF00, "  [23] DIAG.S - Link Sequence Error\n");
    //if (serr & (1 << 24)) // gop_printf(0xFFFFFF00, "  [24] DIAG.T - Transport State Transition Error\n");
    //if (serr & (1 << 25)) // gop_printf(0xFFFFFF00, "  [25] DIAG.F - Unknown FIS Type\n");
    //if (serr & (1 << 26)) // gop_printf(0xFFFFFF00, "  [26] DIAG.X - Exchanged\n");
}
#endif
// Context per initialized port
typedef struct _AHCI_PORT_CTX {
    HBA_PORT* port;             // MMIO base for this port
    HBA_CMD_TBL* cmd_tbl;       // Command table memory
    void* clb;                  // Cmd list buffer
    void* fis;                  // FIS receive buffer
    SPINLOCK IoLock;            // Serializes command-slot/table publication
    BLOCK_DEVICE bdev;          // Associated BLOCK_DEVICE interface
} AHCI_PORT_CTX;

static HBA_MEM* hba_mem;
static AHCI_PORT_CTX ports[AHCI_MAX_PORTS];
static int port_count;

#define AHCI_COMMAND_TABLE_SIZE 256U
#define AHCI_MAX_PRDT_ENTRIES \
    ((AHCI_COMMAND_TABLE_SIZE - offsetof(HBA_CMD_TBL, prdt_entry)) / sizeof(HBA_PRDT_ENTRY))
#define AHCI_MAX_PRDT_BYTES (1U << 22)

static MTSTATUS
AhcipBuildPrdt(
    IN HBA_CMD_TBL* CommandTable,
    IN const void* Buffer,
    IN size_t Bytes,
    OUT uint32_t* EntryCount
)

/*++

    Routine description:

        Builds an AHCI physical-region descriptor table for a transfer buffer.

    Arguments:

        [IN] CommandTable - AHCI command table being prepared.
        [IN OUT] Buffer - Buffer used to transfer the data.
        [IN] Bytes - Number of bytes transferred or examined.
        [IN] EntryCount - Number of entry entries.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    if (!CommandTable || !Buffer || !Bytes || !EntryCount) {
        return MT_INVALID_PARAM;
    }

    HBA_PRDT_ENTRY* Entries = (HBA_PRDT_ENTRY*)(
        (uint8_t*)CommandTable + offsetof(HBA_CMD_TBL, prdt_entry)
    );
    uintptr_t CurrentVa = (uintptr_t)Buffer;
    size_t Remaining = Bytes;
    uint32_t Count = 0;

    while (Remaining != 0) {
        if (!MmIsAddressPresent(CurrentVa)) return MT_INVALID_ADDRESS;

        uintptr_t PhysicalAddress = MiTranslateVirtualToPhysical((void*)CurrentVa);
        size_t PageBytes = VirtualPageSize - VA_OFFSET(CurrentVa);
        if (PageBytes > Remaining) PageBytes = Remaining;

        if (Count != 0) {
            HBA_PRDT_ENTRY* Previous = &Entries[Count - 1];
            size_t PreviousBytes = (size_t)Previous->dbc + 1;
            uintptr_t PreviousPhysical =
                ((uintptr_t)Previous->dbau << 32) | Previous->dba;

            if (PreviousPhysical + PreviousBytes == PhysicalAddress &&
                PageBytes <= AHCI_MAX_PRDT_BYTES - PreviousBytes) {
                Previous->dbc = (uint32_t)(PreviousBytes + PageBytes - 1);
                CurrentVa += PageBytes;
                Remaining -= PageBytes;
                continue;
            }
        }

        if (Count == AHCI_MAX_PRDT_ENTRIES) return MT_INVALID_PARAM;

        HBA_PRDT_ENTRY* Entry = &Entries[Count++];
        Entry->dba = (uint32_t)PhysicalAddress;
        Entry->dbau = (uint32_t)(PhysicalAddress >> 32);
        Entry->dbc = (uint32_t)(PageBytes - 1);
        Entry->i = 0;

        CurrentVa += PageBytes;
        Remaining -= PageBytes;
    }

    Entries[Count - 1].i = 1;
    *EntryCount = Count;
    return MT_SUCCESS;
}

// Invalidate cache ranges of the CPU to ensure newest data is fetched from RAM.
static inline void cache_flush_invalidate_range(void* addr, size_t len)

/*++

    Routine description:

        Flushes and invalidates a processor cache range used for DMA.

    Arguments:

        [IN] addr - Virtual or physical address to transform.
        [IN] len - Length of the input in bytes.

    Return Values:

        None.

--*/

{
    uintptr_t p = (uintptr_t)addr & ~(uintptr_t)63;
    uintptr_t end = (uintptr_t)addr + len;
    for (; p < end; p += 64) {
        __asm__ volatile("clflush (%0)" :: "r"((void*)p) : "memory");
    }
    __asm__ volatile("mfence" ::: "memory");
}

static inline void outl_port(uint16_t port, uint32_t val)

/*++

    Routine description:

        Writes a 32-bit value to an I/O port.

    Arguments:

        [IN] port - AHCI port or I/O port affected by the operation.
        [IN] val - Value to write or process.

    Return Values:

        None.

--*/

{
    __asm__ volatile("outl %0, %1" :: "a"(val), "d"(port));
}
static inline uint32_t inl_port(uint16_t port)

/*++

    Routine description:

        Reads a 32-bit value from an I/O port.

    Arguments:

        [IN] port - AHCI port or I/O port affected by the operation.

    Return Values:

        The 32-bit value read from the I/O port.

--*/

{
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "d"(port));
    return val;
}
static uint32_t pci_cfg_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)

/*++

    Routine description:

        Reads a 32-bit PCI configuration-space register.

    Arguments:

        [IN] bus - PCI bus number.
        [IN] slot - PCI device or command slot number.
        [IN] func - Routine containing the assertion.
        [IN] offset - Byte offset of the PCI configuration register.

    Return Values:

        The 32-bit PCI configuration value.

--*/

{
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
        ((uint32_t)func << 8) | (offset & 0xFC);
    outl_port(0xCF8, addr);
    return inl_port(0xCFC);
}
static void pci_cfg_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val)

/*++

    Routine description:

        Writes a 32-bit PCI configuration-space register.

    Arguments:

        [IN] bus - PCI bus number.
        [IN] slot - PCI device or command slot number.
        [IN] func - Routine containing the assertion.
        [IN] offset - Byte offset of the PCI configuration register.
        [IN] val - Value to write or process.

    Return Values:

        None.

--*/

{
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
        ((uint32_t)func << 8) | (offset & 0xFC);
    outl_port(0xCF8, addr);
    outl_port(0xCFC, val);
}

// Scans PCI buses and enables Bus Master bit for first AHCI class device found.
// Call this at start of ahci_init() before enable_controller().
static void ensure_ahci_busmaster_enabled(void)

/*++

    Routine description:

        Enables PCI memory-space and bus-master access for the AHCI controller.

    Arguments:

        None.

    Return Values:

        None.

--*/

{
    for (uint8_t bus = 0; bus < 8; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint32_t d0 = pci_cfg_read32(bus, slot, func, 0x00);
                if ((d0 & 0xFFFF) == 0xFFFF) continue; // no device
                uint32_t cl = pci_cfg_read32(bus, slot, func, 0x08);
                uint8_t base_class = (cl >> 24) & 0xFF;
                uint8_t sub_class = (cl >> 16) & 0xFF;
                uint8_t prog_if = (cl >> 8) & 0xFF;
                if (base_class == 0x01 && sub_class == 0x06 && prog_if == 0x01) {
#ifdef AHCI_DEBUG_PRINT
                    uint32_t hdr = pci_cfg_read32(bus, slot, func, 0x00);
                    uint16_t vendor = hdr & 0xFFFF;
                    uint16_t device = (hdr >> 16) & 0xFFFF;
#endif
                    uint32_t cmd32 = pci_cfg_read32(bus, slot, func, 0x04);
                    uint16_t cmd = cmd32 & 0xFFFF;
#ifdef AHCI_DEBUG_PRINT
                    // gop_printf(0xFFFFFF00, "AHCI PCI at %p:%p vendor=%p device=%p\n", bus, slot, func, vendor, device);
                    // gop_printf(0xFFFFFF00, "PCI CMD before: %p\n", cmd);
#endif
                    if (!(cmd & (1 << 2))) {
                        cmd |= (1 << 2); // set Bus Master
                        pci_cfg_write32(bus, slot, func, 0x04, (cmd32 & 0xFFFF0000) | (uint32_t)cmd);
#ifdef AHCI_DEBUG_PRINT
                        // gop_printf(0xFFFFFF00, "Enabled PCI Bus Master bit for AHCI\n");
#endif
                    }
                    else {
#ifdef AHCI_DEBUG_PRINT
                        // gop_printf(0xFFFFFF00, "PCI Bus Master already enabled\n");
#endif
                    }
                    return;
                }
            }
        }
    }
#ifdef AHCI_DEBUG_PRINT
    // gop_printf(0xFFFF0000, "AHCI PCI device not found while scanning PCI bus\n");
#endif
}

/// <summary>
/// Locate first zero bit in 32-bit mask.
/// </summary>
/// <param name="mask">The 32-bit mask.</param>
/// <returns>First Zero bit from the argument supplied. | -1 if not found.</returns>
static int find_free_slot(uint32_t mask)

/*++

    Routine description:

        Finds an unused AHCI command slot on a port.

    Arguments:

        [IN] mask - Bit mask applied to the value.

    Return Values:

        The located index or identifier, or a negative value when no matching entry is found.

--*/

{
    for (int i = 0; i < 32; i++) {
        if (!(mask & (1u << i))) {
            return i;
        }
    }
    return -1;
}

/// <summary>
/// Enable controller and reset
/// </summary>
static void enable_controller(void)

/*++

    Routine description:

        Enables AHCI mode and required controller capabilities.

    Arguments:

        None.

    Return Values:

        None.

--*/

{
    hba_mem->ghc |= (1u << 31); // AHCI Enable.
    hba_mem->ghc |= (1u << 0); // Global Reset.
    /// Busy wait.
    while (hba_mem->ghc & (1u << 0));
}

/// <summary>
/// Initialize individual port at index.
/// </summary>
/// <param name="idx">Index to initialize ports in</param>
/// <returns>True or False based on succession</returns>
static bool init_one_port(int idx)

/*++

    Routine description:

        Initializes command-list and FIS memory for one AHCI port.

    Arguments:

        [IN] idx - Debug-register or collection index.

    Return Values:

        Zero on success, or a negative value when the port cannot be initialized.

--*/

{
    HBA_PORT* p = (HBA_PORT*)((uint8_t*)hba_mem + 0x100 + idx * 0x80);
    uint32_t status = p->ssts;
    if ((status & 0x0F) != 3) return false; // no device present

    // Stop the port before configuration
    p->cmd &= ~(1u << 0); // Clear ST (START)
    p->cmd &= ~(1u << 4); // Clear PRE (FIS Receive Enable)

    // Wait until port is idle
    while ((p->cmd & (1u << 15)) || (p->cmd & (1u << 14))) {
        __pause();
    }

    // Allocate and zero CLB (1 KiB)
    void* clb = MmAllocateContigiousMemory(1024, UINT64_T_MAX);
    if (!clb) return false;
    kmemset(clb, 0, 1024);
    // pass the PHYSICAL address.
    uintptr_t clb_phys = MiTranslateVirtualToPhysical(clb);
    assert(((uintptr_t)clb_phys & 0x3FF) == 0, "CLB must be 1KiB-aligned (1024 byte multiple)");
#ifdef AHCI_DEBUG_PRINT
    // gop_printf(COLOR_BLUE, "In INIT_ONE_PORT, clb_phys: %p | virt: %p\n", clb_phys, clb);
#endif
    p->clb = (uint32_t)(uintptr_t)clb_phys;
    p->clbu = (uint32_t)((uintptr_t)clb_phys >> 32);

    // Allocate and zero FIS receive buffer (256 B)
    void* fis_buf = MmAllocateContigiousMemory(256, UINT64_T_MAX);
    if (!fis_buf) return false;
    kmemset(fis_buf, 0, 256);
    // Again, pass the PHYSICAL.
    uintptr_t fis_buf_phys = MiTranslateVirtualToPhysical(fis_buf);
#ifdef AHCI_DEBUG_PRINT
    // gop_printf(COLOR_BLUE, "In INIT_ONE_PORT, fis_buf_phys: %p | virt: %p\n", fis_buf_phys, fis_buf);
#endif
    p->fb = (uint32_t)(uintptr_t)fis_buf_phys;
    p->fbu = (uint32_t)((uintptr_t)fis_buf_phys >> 32);

    // Allocate and zero Command Table buffers: 256 B x 32 slots
    size_t tbl_size = 256 * 32;
    void* cmd_tbl = MmAllocateContigiousMemory(tbl_size, UINT64_T_MAX);
    if (!cmd_tbl) return false;
    kmemset(cmd_tbl, 0, tbl_size);
    uintptr_t cmd_tbl_phys = MiTranslateVirtualToPhysical(cmd_tbl);
    assert(((uintptr_t)cmd_tbl_phys & 0xFF) == 0, "Command table block must be 256-byte aligned");
#ifdef AHCI_DEBUG_PRINT
    // gop_printf(COLOR_BLUE, "In INIT_ONE_PORT, cmd_tbl_phys: %p | virt: %p\n", cmd_tbl_phys, cmd_tbl);
#endif

    // Point each command header to its table
    for (int slot = 0; slot < 32; slot++) {
        // Header at clb + slot*32 bytes
        HBA_CMD_HEADER* hdr = (HBA_CMD_HEADER*)((uint8_t*)clb + slot * sizeof(HBA_CMD_HEADER));
        uintptr_t tbl_pa_phys = (uintptr_t)cmd_tbl_phys + slot * 256;
        hdr->ctba = (uint32_t)(tbl_pa_phys & 0xFFFFFFFF);
        hdr->ctbau = (uint32_t)(tbl_pa_phys >> 32);
        hba_cmd_hdr_set_prdtl(hdr, 1); // one PRDT entry
    }

    // Clear any old errors and start the port
    p->serr = ~0U; // Clear all SERROR bits by writing 1 to them.
    p->cmd |= (1u << 4); // Set FRE
    p->cmd |= (1u << 0); // Set ST

    // Add this assertion to ensure the port actually starts
    assert((p->cmd & 1) != 0, "Port ST bit failed to set!");

    // Save context
    AHCI_PORT_CTX* ctx = &ports[port_count];
    ctx->port = p;
    ctx->clb = clb;
    ctx->fis = fis_buf;
    ctx->cmd_tbl = cmd_tbl;
    ctx->IoLock.locked = 0;
    ctx->bdev.read_sector = ahci_read_sector;
    ctx->bdev.write_sector = ahci_write_sector;
    ctx->bdev.dev_data = ctx;

    /* CAP and slot counts */
#ifdef AHCI_DEBUG_PRINT
    uint32_t cap = (uint32_t)hba_mem->cap;
    uint32_t ncs = (cap >> 8) & 0x1Fu;
    assert(((ncs + 1) >= 1) && ((ncs + 1) <= 32), "CAP.NCS invalid (command slots out of range)");

    bool s64a = !!((cap >> 31) & 1u);

    /* Alignment checks */
    assert(((uintptr_t)clb_phys & 0x3FF) == 0, "PxCLB must be 1KiB-aligned (1024 bytes)");
    assert(((uintptr_t)fis_buf_phys & 0xFF) == 0, "PxFB (FIS) must be 256-byte aligned");
    assert(((uintptr_t)cmd_tbl_phys & 0xFF) == 0, "Command table region must start at 256-byte boundary");

    /* 64-bit addressing: if device doesn't advertise S64A, upper bits must be zero */
    if (!s64a) {
        assert(((uintptr_t)clb_phys >> 32) == 0, "CLB high dword must be zero when CAP.S64A==0");
        assert(((uintptr_t)cmd_tbl_phys >> 32) == 0, "CMD_TBL high dword must be zero when CAP.S64A==0");
        assert(((uintptr_t)fis_buf_phys >> 32) == 0, "FIS high dword must be zero when CAP.S64A==0");
    }
#endif
    /* Per-header CTBA programmed correctly (for the number of slots the HBA advertises) */
#ifdef AHCI_DEBUG_PRINT
    for (unsigned sl = 0; sl <= ncs; ++sl) {
#ifdef DEBUG
        HBA_CMD_HEADER* hdr = (HBA_CMD_HEADER*)((uint8_t*)clb + sl * sizeof(HBA_CMD_HEADER));
        uintptr_t expected = (uintptr_t)cmd_tbl_phys + sl * 256;
#endif
        assert(hdr->ctba == (uint32_t)(expected & 0xFFFFFFFFu), "Header CTBA low doesn't match expected CTBA");
        if (s64a) {
            assert(hdr->ctbau == (uint32_t)(expected >> 32), "Header CTBAU mismatch (S64A advertised)");
        }
        else {
            assert(hdr->ctbau == 0, "Header CTBAU must be zero when S64A==0");
        }
    }
#endif

    port_count++;
    return true;
}

extern BOOT_INFO boot_info_local;
extern GOP_PARAMS gop_local;

bool ahci_initialized = false;

MTSTATUS ahci_init(void)

/*++

    Routine description:

        Discovers and initializes an AHCI controller.

    Arguments:

        None.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    if (ahci_initialized) { return MT_SUCCESS; } // gop_printf(COLOR_RED, "AHCI Initialization got called again when already init.\n"); return MT_SUCCESS; }
    // Use BootInfo PCI BARs.
    for (size_t i = 0; i < boot_info_local.AhciCount; i++) {
        uint64_t base = boot_info_local.AhciBarBases[i];
        void* virt = MmMapIoSpace(base, VirtualPageSize, MmNonCached);
#ifdef AHCI_DEBUG_PRINT
        // gop_printf(COLOR_ORANGE, "Address of AHCI BAR %u (%p) is: %s\n", i, virt, MmIsAddressPresent((uintptr_t)virt) ? "Valid" : "Invalid");
#endif
        // Now change the values in the struct
        boot_info_local.AhciBarBases[i] = (uint64_t)virt;
    }

    uint64_t bar = boot_info_local.AhciBarBases[0];
    hba_mem = (HBA_MEM*)(uintptr_t)bar;
#ifdef AHCI_DEBUG_PRINT
    // gop_printf(0xFF00FFFF, "About to touch AHCI %u at %p | It's %s\n",0, hba_mem, MmIsAddressPresent((uintptr_t)bar) ? "Valid" : "Invalid");
    //_cli(); __hlt();
#endif
    ensure_ahci_busmaster_enabled();
    enable_controller();
    port_count = 0; // Start from 0.
    uint32_t pi = hba_mem->pi;

    for (int idx = 0; idx < AHCI_MAX_PORTS; idx++) {
        if (pi & (1u << idx)) {
            init_one_port(idx);
        }
    }

    // Register ALL block devices.
    for (int i = 0; i < port_count; i++) {
        register_block_device(&ports[i].bdev);
    }
    ahci_initialized = true;
    return port_count > 0 ? MT_SUCCESS : MT_AHCI_PORT_FAILURE; // If it could register a port, it will return true, if it couldn't, it will return false (bugcheck)
}

static MTSTATUS AhcipReadSectorLocked(BLOCK_DEVICE* dev, uint32_t lba, void* buf, size_t bytes)

/*++

    Routine description:

        Issues one sector read while the AHCI port lock is held.

    Arguments:

        [IN] dev - Block device used for the transfer.
        [IN] lba - Logical block address of the sector.
        [IN OUT] buf - Buffer read, written, or examined by the routine.
        [IN] bytes - Number of bytes transferred or examined.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{

    // 1. Input Validation
    if (bytes == 0 || (bytes % 512 != 0)) {
        // ATA DMA transfers must typically be sector-aligned
        return MT_INVALID_PARAM;
    }

    AHCI_PORT_CTX* ctx = (AHCI_PORT_CTX*)dev->dev_data;
    HBA_PORT* p = ctx->port;

    // 2. Clear Pending Interrupts
    p->is = (uint32_t)-1;

    int slot = find_free_slot(p->sact | p->ci);
    if (slot < 0) return MT_AHCI_PORT_FAILURE;

    uint32_t spin = 0;
    const uint32_t TIMEOUT = 100000000;

    // Wait for slot to be clear (sanity check)
    while (p->ci & (1u << slot)) {
        if (++spin >= TIMEOUT) return MT_AHCI_TIMEOUT;
    }

    // 3. Setup Command Table
    HBA_CMD_TBL* cmd = (HBA_CMD_TBL*)((uint8_t*)ctx->cmd_tbl + slot * 256);
    kmemset(cmd, 0, 256);

    // 4. Setup Command Header
    HBA_CMD_HEADER* hdr = (HBA_CMD_HEADER*)((uint8_t*)ctx->clb + slot * sizeof(HBA_CMD_HEADER));
    hba_cmd_hdr_set_cfl(hdr, (sizeof(FIS_REG_H2D) + 3) / 4);
    hba_cmd_hdr_set_w(hdr, 0);      // Read
    hdr->prdbc = 0;                 // Reset transferred count

    // 5. Calculate Sector Count
    // We assume bytes is a multiple of 512 based on the check above.
    uint32_t sector_count = bytes / 512;

    // 6. Build FIS with Dynamic Sector Count
    FIS_REG_H2D* fis = (FIS_REG_H2D*)(&cmd->cfis);
    kmemset(fis, 0, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1; // Command
    fis->command = 0x25; // READ DMA EXT

    fis->lba0 = (uint8_t)(lba & 0xFF);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis->device = 1 << 6; // LBA mode

    fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4 = 0; // Extended LBA not supported in this simplified LBA32 param
    fis->lba5 = 0;

    // Split sector count for LBA48 command structure
    fis->countl = (uint8_t)(sector_count & 0xFF);
    fis->counth = (uint8_t)((sector_count >> 8) & 0xFF);

    // 7. Setup a scatter/gather PRDT for the virtual buffer.
    if (bytes > 4 * 1024 * 1024) return MT_INVALID_PARAM;
    uint32_t PrdtCount = 0;
    MTSTATUS Status = AhcipBuildPrdt(cmd, buf, bytes, &PrdtCount);
    if (MT_FAILURE(Status)) return Status;
    hba_cmd_hdr_set_prdtl(hdr, PrdtCount);

    // 8. Memory Fences & Cache Flushing
    // Ensure the Table and Buffer are in RAM before the HBA fetches them
    cache_flush_invalidate_range(ctx->clb, 1024);
    cache_flush_invalidate_range(cmd, 256);
    cache_flush_invalidate_range(buf, bytes); // Flush the receiving buffer to be safe (invalidate)
    __asm__ volatile("sfence; mfence" ::: "memory");

    // 9. Start Command
    p->ci = (1u << slot);

    // 10. Wait for Completion
    spin = 0;
    while (p->ci & (1u << slot)) {
        if (++spin >= TIMEOUT) break;
    }

    // 11. Error Checking
    if ((spin >= TIMEOUT) || (p->tfd & ((1 << 7) | (1 << 0)))) {
#ifdef AHCI_DEBUG_PRINT
        // gop_printf(COLOR_RED, "AHCI Err: TFD: %x, SERR: %x\n", p->tfd, p->serr);
#endif
        return MT_AHCI_READ_FAILURE;
    }

    // 12. Check Result
    // Invalidate header cache so CPU reads the updated prdbc from RAM
    __asm__ volatile("mfence" ::: "memory");
    cache_flush_invalidate_range(hdr, sizeof(HBA_CMD_HEADER));
    __asm__ volatile("mfence" ::: "memory");

    if (hdr->prdbc != bytes) {
#ifdef AHCI_DEBUG_PRINT
        // gop_printf(COLOR_RED, "AHCI Partial Read: Req %u, Got %u\n", bytes, hdr->prdbc);
#endif
        // Even if mismatched, return success if data was moved? 
        // Usually strictly strictly enforce equality.
        return MT_AHCI_READ_FAILURE;
    }

    // Invalidate data buffer cache so CPU reads new data from RAM
    cache_flush_invalidate_range(buf, bytes);

    // Ack interrupt
    p->is = p->is;

    return MT_SUCCESS;
}

MTSTATUS ahci_read_sector(BLOCK_DEVICE* dev, uint32_t lba, void* buf, size_t bytes)

/*++

    Routine description:

        Serializes and performs one AHCI sector read.

    Arguments:

        [IN] dev - Block device used for the transfer.
        [IN] lba - Logical block address of the sector.
        [IN OUT] buf - Buffer read, written, or examined by the routine.
        [IN] bytes - Number of bytes transferred or examined.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    if (!dev || !dev->dev_data || !buf) return MT_INVALID_PARAM;

    AHCI_PORT_CTX* ctx = (AHCI_PORT_CTX*)dev->dev_data;
    IRQL OldIrql;
    MsAcquireSpinlock(&ctx->IoLock, &OldIrql);
    MTSTATUS Status = AhcipReadSectorLocked(dev, lba, buf, bytes);
    MsReleaseSpinlock(&ctx->IoLock, OldIrql);
    return Status;
}

static MTSTATUS AhcipWriteSectorLocked(BLOCK_DEVICE* dev, uint32_t lba, const void* buf, size_t bytes)

/*++

    Routine description:

        Issues one sector write while the AHCI port lock is held.

    Arguments:

        [IN] dev - Block device used for the transfer.
        [IN] lba - Logical block address of the sector.
        [IN OUT] buf - Buffer read, written, or examined by the routine.
        [IN] bytes - Number of bytes transferred or examined.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{

    // 1. Input Validation
    if (bytes == 0 || (bytes % 512 != 0)) return MT_INVALID_PARAM;
    // 4MB Limit per PRDT entry check
    if (bytes > 4 * 1024 * 1024) return MT_INVALID_PARAM;

    AHCI_PORT_CTX* ctx = (AHCI_PORT_CTX*)dev->dev_data;
    HBA_PORT* p = ctx->port;

    // 2. Clear Pending Interrupts
    p->is = (uint32_t)-1;

    int slot = find_free_slot(p->sact | p->ci);
    if (slot < 0) return MT_AHCI_GENERAL_FAILURE;

    // Calculate sector count dynamically
    uint32_t sector_count = bytes / 512;

    /* Command table for this slot */
    HBA_CMD_TBL* cmd = (HBA_CMD_TBL*)((uint8_t*)ctx->cmd_tbl + slot * 256);
    kmemset(cmd, 0, 256);

    /* Command header */
    HBA_CMD_HEADER* hdr = (HBA_CMD_HEADER*)((uint8_t*)ctx->clb + slot * sizeof(HBA_CMD_HEADER));
    hba_cmd_hdr_set_cfl(hdr, (sizeof(FIS_REG_H2D) + 3) / 4);
    hba_cmd_hdr_set_w(hdr, 1);       /* write */
    hdr->prdbc = 0;

    /* Build CFIS */
    FIS_REG_H2D* fis = (FIS_REG_H2D*)(&cmd->cfis);
    kmemset(fis, 0, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = 0x35; /* WRITE DMA EXT */
    fis->device = 1 << 6; // LBA mode

    fis->lba0 = (uint8_t)(lba & 0xFF);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4 = 0;
    fis->lba5 = 0;

    // >>> FIXED: Dynamic Sector Count <<<
    fis->countl = (uint8_t)(sector_count & 0xFF);
    fis->counth = (uint8_t)((sector_count >> 8) & 0xFF);

    /* Build a scatter/gather PRDT for every physical run in the buffer. */
    uint32_t PrdtCount = 0;
    MTSTATUS Status = AhcipBuildPrdt(cmd, buf, bytes, &PrdtCount);
    if (MT_FAILURE(Status)) return Status;
    hba_cmd_hdr_set_prdtl(hdr, PrdtCount);

    // >>> CRITICAL: Cache Flushing for Writes <<<
    // For writes, we must ensure the data in the CPU cache is written back to RAM
    // before the DMA controller reads it.
    cache_flush_invalidate_range((void*)buf, bytes); // Flush entire buffer, not just 512

    // Flush metadata
    cache_flush_invalidate_range(ctx->clb, 1024);
    cache_flush_invalidate_range(cmd, 256);

    __asm__ volatile("sfence; mfence" ::: "memory");

    /* Issue */
    p->ci = (1u << slot);

    /* Wait */
    uint32_t spin = 0;
    const uint32_t TIMEOUT = 100000000;
    while (p->ci & (1u << slot)) {
        if (++spin >= TIMEOUT) break;
    }

    if (spin >= TIMEOUT) {
#ifdef AHCI_DEBUG_PRINT
        // gop_printf(COLOR_RED, "AHCI TIMEOUT ahci_write_sector\n");
#endif
        return MT_AHCI_TIMEOUT;
    }

    // IMPORTANT: Check for errors from the device
    if (p->tfd & ((1 << 7) | (1 << 0))) {
#ifdef AHCI_DEBUG_PRINT
        // gop_printf(0xFFFF0000, "AHCI write error!\n");
        // gop_printf(0xFFFFFF00, "Port TFD: %p, SERR: %p\n", (void*)(uintptr_t)p->tfd, (void*)(uintptr_t)p->serr);
#endif
        return MT_AHCI_WRITE_FAILURE;
    }

    // Optional: Verify prdbc matches bytes written
    // __asm__ volatile("mfence" ::: "memory");
    // cache_flush_invalidate_range(hdr, sizeof(HBA_CMD_HEADER));
    // if (hdr->prdbc != bytes) { /* Warning */ }

    // clear int
    p->is = p->is;

    return MT_SUCCESS;
}

MTSTATUS ahci_write_sector(BLOCK_DEVICE* dev, uint32_t lba, const void* buf, size_t bytes)

/*++

    Routine description:

        Serializes and performs one AHCI sector write.

    Arguments:

        [IN] dev - Block device used for the transfer.
        [IN] lba - Logical block address of the sector.
        [IN OUT] buf - Buffer read, written, or examined by the routine.
        [IN] bytes - Number of bytes transferred or examined.

    Return Values:

        MT_SUCCESS on success, or an error status describing the failure.

--*/

{
    if (!dev || !dev->dev_data || !buf) return MT_INVALID_PARAM;

    AHCI_PORT_CTX* ctx = (AHCI_PORT_CTX*)dev->dev_data;
    IRQL OldIrql;
    MsAcquireSpinlock(&ctx->IoLock, &OldIrql);
    MTSTATUS Status = AhcipWriteSectorLocked(dev, lba, buf, bytes);
    MsReleaseSpinlock(&ctx->IoLock, OldIrql);
    return Status;
}


BLOCK_DEVICE* ahci_get_block_device(int index)

/*++

    Routine description:

        Returns the block-device interface exported by an AHCI port.

    Arguments:

        [IN] index - Index of the entry to process.

    Return Values:

        A pointer to the resulting object or storage, or NULL when no result is available.

--*/

{
    return get_block_device(index);
}
