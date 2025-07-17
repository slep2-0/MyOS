# Tools
ASM = nasm
CC = i686-elf-gcc
LD = i686-elf-ld
OBJCOPY = i686-elf-objcopy

# Flags
ASMFLAGS_ELF = -f elf32
ASMFLAGS_BIN = -f bin
CFLAGS = -m32 -ffreestanding -c
LDFLAGS = -T kernel/linker.ld -static -nostdlib

# Paths
BUILD = build

ASM_SRC = kernel/kernel_entry.asm
C_SRCS = kernel/kernel.c kernel/screen/vga/vga.c

ASM_OBJ = $(BUILD)/kernel_entry.o
C_OBJS = $(BUILD)/kernel.o $(BUILD)/vga.o

TARGET_ELF = $(BUILD)/kernel.elf
TARGET_BIN = $(BUILD)/kernel.bin

BOOT1_SRC = boot/bootloader.asm
BOOT2_SRC = boot/bootloader_stage2.asm

BOOT1_BIN = $(BUILD)/bootloader.bin
BOOT2_BIN = $(BUILD)/bootloader_stage2.bin

OS_IMAGE = $(BUILD)/os-image.bin

# Default target
all: $(BOOT1_BIN) $(BOOT2_BIN) $(TARGET_BIN) $(OS_IMAGE)

# Clean target
clean:
	del $(BUILD)\*.o $(TARGET_ELF) $(TARGET_BIN) $(BOOT1_BIN) $(BOOT2_BIN) $(OS_IMAGE) 2>nul || echo

# Assemble kernel_entry.asm (ELF object)
$(ASM_OBJ): $(ASM_SRC)
	$(ASM) $(ASMFLAGS_ELF) $< -o $@

# Compile kernel.c
$(BUILD)/kernel.o: kernel/kernel.c
	$(CC) $(CFLAGS) $< -o $@

# Compile vga.c
$(BUILD)/vga.o: kernel/screen/vga/vga.c
	$(CC) $(CFLAGS) $< -o $@

# Link ELF kernel
$(TARGET_ELF): $(ASM_OBJ) $(C_OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

# Convert ELF kernel to binary
$(TARGET_BIN): $(TARGET_ELF)
	$(OBJCOPY) -O binary $< $@
	@cls
	@echo Successfully Compiled and Linked Kernel

# Assemble stage 1 bootloader (binary, flat 512 bytes)
$(BOOT1_BIN): $(BOOT1_SRC)
	$(ASM) $(ASMFLAGS_BIN) $< -o $@

# Assemble stage 2 bootloader (binary)
$(BOOT2_BIN): $(BOOT2_SRC)
	$(ASM) $(ASMFLAGS_BIN) $< -o $@

# Create final OS bootable image by concatenating bootloaders and kernel
$(OS_IMAGE): $(BOOT1_BIN) $(BOOT2_BIN) $(TARGET_BIN)
	copy /b "$(BUILD)\bootloader.bin" + "$(BUILD)\bootloader_stage2.bin" + "$(BUILD)\kernel.bin" "$(BUILD)\os-image.img" >nul
	@echo Created OS image successfully.

.PHONY: all clean
