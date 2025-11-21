/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:      AHCI Driver types and functions.
 */

#ifndef X86_DRIVER_AHCI_H
#define X86_DRIVER_AHCI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../blk/block.h"
#include "../../mtstatus.h"

// Maximum number of AHCI Ports supported
#define AHCI_MAX_PORTS 32

typedef enum _FIS_TYPE {
	FIS_TYPE_REG_H2D = 0x27,	// Register FIS - host to device
	FIS_TYPE_REG_D2H = 0x34,	// Register FIS - device to host
	FIS_TYPE_DMA_ACT = 0x39,	// DMA activate FIS - device to host
	FIS_TYPE_DMA_SETUP = 0x41,	// DMA setup FIS - bidirectional
	FIS_TYPE_DATA = 0x46,	// Data FIS - bidirectional
	FIS_TYPE_BIST = 0x58,	// BIST activate FIS - bidirectional
	FIS_TYPE_PIO_SETUP = 0x5F,	// PIO setup FIS - device to host
	FIS_TYPE_DEV_BITS = 0xA1,	// Set device bits FIS - device to host
} FIS_TYPE;

/// AHCI Register layout (Global HBA Registers)
typedef volatile struct _HBA_MEM {
	uint32_t cap;		// 0x00: Host Capabilities.
	uint32_t ghc;		// 0x04: Global host control
	uint32_t is;		// 0x08: Interrupt Status
	uint32_t pi;		// 0x0C: Ports implemented.
	uint32_t vs;		// 0x10: Version
	uint32_t ccc_ctl;	// 0x14: Command completion coalescing control
	uint32_t ccc_pts;	// 0x18: Command completion coalescing ports.
	uint32_t em_loc;	// 0x1C: Enclosure management location
	uint32_t em_ctl;	// 0x20: Enclosure management control
	uint32_t cap2;		// 0x24: Host capabilities EXTENDED.
	uint32_t bohc;		// 0x28: BIOS/OS handoff control and status.
	uint8_t  rsv[0xA0 - 0x2C];
	uint8_t	 venor[0x100 - 0xA0];
	/// Port control structures start at offset 0x100, one per port.
} HBA_MEM;

/// Per port registers at HBA_MEM + 0x100 + (port * 0x80)
typedef volatile struct _HBA_PORT {
	uint32_t clb;		// 0x00: Command list base address lower 32 bits.
	uint32_t clbu;		// 0x04: Command list base address higher 32 bits.
	uint32_t fb;		// 0x08: FIS Base address lower 32 bits.
	uint32_t fbu;		// 0x0C: FIS Base address higher 32 bits.
	uint32_t is;		// 0x10: Interrupt Status.
	uint32_t ie;		// 0x14: Interrupt Enable.
	uint32_t cmd;		// 0x18: Command And Status.
	uint32_t rsv0;		// 0x1C: RESERVED.
	uint32_t tfd;		// 0x20: Task File Data.
	uint32_t sig;		// 0x24: Signature.
	uint32_t ssts;		// 0x28: Serial ATA Status.
	uint32_t sctl;		// 0x2C: Serial ATA Control.
	uint32_t serr;		// 0x30: Serial ATA Error.
	uint32_t sact;		// 0x34: Serial ATA Active.
	uint32_t ci;		// 0x38: Command Issue.
	uint32_t sntf;		// 0x3C: Serial ATA notification.
	uint32_t fbs;		// 0x40: FIS-Based switch control.
	uint32_t rsv1[11];	// 0x44 - 0x6F: RESERVED.
	uint32_t vendor[4]; // 0x70: Vendor Specific.
} HBA_PORT;

// Data structures for FIS and Command Tables 

/// Register - Host to Device FIS (FIS_TYPE_REG_H2D)
#pragma pack(push, 1)
typedef struct _FIS_REG_H2D {
	uint8_t fis_type;
	uint8_t pmport : 4;
	uint8_t rsv0 : 3;
	uint8_t c : 1;       // 1: command, 0: control
	uint8_t command;     // ATA command
	uint8_t featurel;    // feature low
	uint8_t lba0;        // LBA low byte
	uint8_t lba1;        // LBA mid byte
	uint8_t lba2;        // LBA high byte
	uint8_t device;
	uint8_t lba3;        // LBA byte 3
	uint8_t lba4;        // LBA byte 4
	uint8_t lba5;        // LBA byte 5
	uint8_t featureh;    // feature high
	uint8_t countl;      // sector count low
	uint8_t counth;      // sector count high
	uint8_t icc;         // ISO command completion
	uint8_t control;
	uint8_t rsv1[4];
} FIS_REG_H2D;

/// Physical Region Descriptor Table Entry
typedef struct _HBA_PRDT_ENTRY {
	uint32_t dba;		// Data base address
	uint32_t dbau;		// Data base address upper 32 bits
	uint32_t rsv0;		// Reserved

	// DW3
	uint32_t dbc : 22;		// Byte count, 4M max
	uint32_t rsv1 : 9;		// Reserved
	uint32_t i : 1;		// Interrupt on completionn
} HBA_PRDT_ENTRY;

/// Command Table: one per slot.
typedef struct _HBA_CMD_TBL {
	// 0x00
	uint8_t  cfis[64];	// Command FIS

	// 0x40
	uint8_t  acmd[16];	// ATAPI command, 12 or 16 bytes

	// 0x50
	uint8_t  rsv[48];	// Reserved

	// 0x80
	HBA_PRDT_ENTRY	prdt_entry[1];	// Physical region descriptor table entries, 0 ~ 65535
} HBA_CMD_TBL;
#pragma pack(pop)

/// <summary>
/// HBA Command Header (defines an AHCI Command)
/// </summary>
typedef struct _HBA_CMD_HEADER {
	volatile uint32_t dw0;     // control flags + PRDTL
	volatile uint32_t prdbc;   // physical region descriptor byte count transferred
	uint32_t ctba;             // command table base address (lower)
	uint32_t ctbau;            // command table base address upper
	uint32_t rsv1[4];          // reserved
} HBA_CMD_HEADER;
#ifndef _MSC_VER
_Static_assert(sizeof(HBA_CMD_HEADER) == 32, "SIZEOF HBA_CMD_HEADER ISNT 32 BYTES! -- Misalignment check.");
_Static_assert(sizeof(HBA_PRDT_ENTRY) == 16, "PRDT must be 16 bytes");
_Static_assert(offsetof(HBA_CMD_TBL, prdt_entry) == 0x80, "PRDT must start at offset 0x80 in CMD_TBL");
_Static_assert(sizeof(((HBA_CMD_TBL*)0)->cfis) == 64, "cfis must be 64 bytes");
#endif

// AHCI Driver API

// Bit masks / helpers for dw0
// Bit masks / helpers for dw0
#define HBA_CMD_HDR_CFL_MASK    0x0000001Fu
#define HBA_CMD_HDR_A_BIT       (1u << 5)
#define HBA_CMD_HDR_W_BIT       (1u << 6)
#define HBA_CMD_HDR_P_BIT       (1u << 7)
#define HBA_CMD_HDR_PRDTL_MASK  0xFFFF0000u
#define ATA_DEV_BSY 0x80 // Busy
#define ATA_DEV_DRQ 0x08 // Data Request
#define ATA_DEV_ERR 0x01 // Error

#define ATA_CMD_READ_DMA_EX     0x25
#define ATA_CMD_WRITE_DMA_EX    0x35

#define AHCI_DEV_NULL 0
#define AHCI_DEV_SATA 1
#define AHCI_DEV_SEMB 2
#define AHCI_DEV_PM   3
#define AHCI_DEV_SATAPI 4

#define HBA_PORT_IPM_ACTIVE 1
#define HBA_PORT_DET_PRESENT 3

#define HBA_PxCMD_ST    0x0001
#define HBA_PxCMD_FRE   0x0010
#define HBA_PxCMD_FR    0x4000
#define HBA_PxCMD_CR    0x8000
#define HBA_PxIS_TFES   (1 << 30)       /* TFES - Task File Error Status */

static inline void hba_cmd_hdr_set_cfl(HBA_CMD_HEADER* h, uint32_t cfl) {
	h->dw0 = (h->dw0 & ~HBA_CMD_HDR_CFL_MASK) | ((cfl)&HBA_CMD_HDR_CFL_MASK);
}
static inline uint32_t hba_cmd_hdr_get_cfl(HBA_CMD_HEADER* h) {
	return h->dw0 & HBA_CMD_HDR_CFL_MASK;
}

static inline void hba_cmd_hdr_set_w(HBA_CMD_HEADER* h, int w) {
	if (w) h->dw0 |= HBA_CMD_HDR_W_BIT;
	else h->dw0 &= ~HBA_CMD_HDR_W_BIT;
}
static inline int hba_cmd_hdr_get_w(HBA_CMD_HEADER* h) {
	return (h->dw0 & HBA_CMD_HDR_W_BIT) ? 1 : 0;
}

static inline void hba_cmd_hdr_set_prdtl(HBA_CMD_HEADER* h, uint32_t prdtl) {
	h->dw0 = (h->dw0 & ~HBA_CMD_HDR_PRDTL_MASK) | (((prdtl) & 0xFFFFu) << 16);
}
static inline uint32_t hba_cmd_hdr_get_prdtl(HBA_CMD_HEADER* h) {
	return (h->dw0 >> 16) & 0xFFFFu;
}

/* Uncomment to see AHCI Debug Prints */
///#define AHCI_DEBUG_PRINT


/// <summary>
/// Initialize the AHCI Driver.
/// </summary>
/// <returns>True or False based if it initialized correctly or not. (if failure = bugcheck)</returns>
MTSTATUS ahci_init(void);

/// <summary>
/// Read a single bytes-byte sector from the given LBA on a specific BLOCK_DEVICE.
/// </summary>
/// <param name="dev">Takes the BLOCK_DEVICE device pointer (on register_block_device)</param>
/// <param name="lba">LBA to read from.</param>
/// <param name="buf">Return buffer to place the data read.</param>
/// <returns>True or False based on succession | buf pointer changes.</returns>
MTSTATUS ahci_read_sector(BLOCK_DEVICE* dev, uint32_t lba, void* buf, size_t bytes);

/// <summary>
/// Write a single bytes-byte sector to given LBA on a specific BLOCK_DEVICE.
/// </summary>
/// <param name="dev">Takes the BLOCK_DEVICE device pointer (on register_block_device)</param>
/// <param name="lba">LBA to read from.</param>
/// <param name="buf">The buffer to write to the specified LBA.</param>
/// <returns>True or False based on succession</returns>
MTSTATUS ahci_write_sector(BLOCK_DEVICE* dev, uint32_t lba, const void* buf, size_t bytes);

/// <summary>
/// Retrieve a pointer to the AHCI driver's BLOCK_DEVICE instance.
/// </summary>
/// <param name="index">Index of the BLOCK_DEVICE registration.</param>
/// <returns>A BLOCK_DEVICE struct pointer.</returns>
BLOCK_DEVICE* ahci_get_block_device(int index);

#endif
