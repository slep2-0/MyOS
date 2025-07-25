/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:      ATA PI/O Driver API Implementation.
 */

#include "ata.h"

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(x) (void)(x)
#endif


#define ATA_PRIMARY_BASE 0x1F0
#define ATA_PRIMARY_CTRL 0x3F6

static BLOCK_DEVICE ata0_dev;
extern GOP_PARAMS gop_local;

// PIO Read one sector from LBA into buf.
static bool ata_read_sector(BLOCK_DEVICE* dev, uint32_t lba, void* buf) {
	UNREFERENCED_PARAMETER(dev);
	tracelast_func("ata_read_sector");
	// Wait until BSY is 0.
	while (__inbyte(ATA_PRIMARY_BASE + 7) & 0x80) {/*none*/}

	// select drive and head, send LBA.
	__outbyte(ATA_PRIMARY_BASE + 6, (unsigned char)(0xE0 | ((lba >> 24) & 0x0F)));
	__outbyte(ATA_PRIMARY_BASE + 2, 1); // sector count = 1.
	__outbyte(ATA_PRIMARY_BASE + 3, (uint8_t)lba);
	__outbyte(ATA_PRIMARY_BASE + 4, (uint8_t)(lba >> 8));
	__outbyte(ATA_PRIMARY_BASE + 5, (uint8_t)(lba >> 16));
	__outbyte(ATA_PRIMARY_BASE + 7, 0x20); // cmd: read

	// Wait for BSY = 0 again (after sending the command)
	gop_printf(&gop_local, 0xEED3D3D3, "Waiting for BSY=0 (READ)\n");
	while (__inbyte(ATA_PRIMARY_BASE + 7) & 0x80) {}

	// Wait DRQ
	gop_printf(&gop_local, 0xEED3D3D3, "Waiting for DRQ=1 (READ)\n");
	while (!(__inbyte(ATA_PRIMARY_BASE + 7) & 0x08)) {/*wait*/}

	// Read 256 * 16 bit words.
	uint16_t* ptr = buf;
	gop_printf(&gop_local, 0xEED3D3D3, "Reading data now...\n");
	for (int i = 0; i < 256; i++) {
		ptr[i] = __inword(ATA_PRIMARY_BASE);
	}
	return true;
}

// PIO Write one sector from buf to LBA
static bool ata_write_sector(BLOCK_DEVICE* dev, uint32_t lba, const void* buf) {
	UNREFERENCED_PARAMETER(dev);
	tracelast_func("ata_write_sector");
	// Busy wait.
	while (__inbyte(ATA_PRIMARY_BASE + 7) & 0x80) {}

	// Again, OUT to the primary base, this time for cmd write.
	__outbyte(ATA_PRIMARY_BASE + 6, (unsigned char)(0xE0 | ((lba >> 24) & 0x0F)));
	__outbyte(ATA_PRIMARY_BASE + 2, 1);
	__outbyte(ATA_PRIMARY_BASE + 3, (uint8_t)lba);
	__outbyte(ATA_PRIMARY_BASE + 4, (uint8_t)(lba >> 8));
	__outbyte(ATA_PRIMARY_BASE + 5, (uint8_t)(lba >> 16));
	__outbyte(ATA_PRIMARY_BASE + 7, 0x30);               // cmd: write


	// Wait for BSY = 0 again (after sending the command)
	gop_printf(&gop_local, 0xEED3D3D3, "Waiting for BSY=0 (WRITE)\n");
	while (__inbyte(ATA_PRIMARY_BASE + 7) & 0x80) {}

	gop_printf(&gop_local, 0xEED3D3D3, "Waiting for DRQ=1 (WRITE)\n");
	while (!(__inbyte(ATA_PRIMARY_BASE + 7) & 0x08)) {}

	const uint16_t* ptr = buf;
	gop_printf(&gop_local, 0xEED3D3D3, "Writing data now...\n");
	for (int i = 0; i < 256; i++) {
		__outword(ATA_PRIMARY_BASE, ptr[i]);
	}
	return true;
}

void ata_init_primary(void) {
	tracelast_func("ata_init_primary");
	ata0_dev.read_sector = ata_read_sector;
	ata0_dev.write_sector = ata_write_sector;
	ata0_dev.dev_data = NULL;
	
	// register block device
	register_block_device(&ata0_dev);
}