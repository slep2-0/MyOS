# Tools
ASM = nasm
CC = i686-elf-gcc
LD = i686-elf-ld
OBJCOPY = i686-elf-objcopy

# Flags
ASMFLAGS_ELF = -f elf32
ASMFLAGS_BIN = -f bin
CFLAGS = -m32 -ffreestanding -c -Wall -Wextra
LDFLAGS = -T kernel/linker.ld -static -nostdlib -m elf_i386

# Targets
all: build/os-image.img

clean:
	del /Q build\*.o build\*.bin build\*.elf build\os-image.img 2>nul || echo

# Compile C files
build/kernel.o: kernel/kernel.c
	@mkdir "build" 2>nul || rem
	$(CC) $(CFLAGS) $< -o $@

build/vga.o: kernel/screen/vga/vga.c
	@mkdir "build" 2>nul || rem
	$(CC) $(CFLAGS) $< -o $@

build/idt.o: kernel/interrupts/idt.c
	@mkdir "build" 2>nul || rem
	$(CC) $(CFLAGS) $< -o $@

build/isr.o: kernel/interrupts/isr.c
	@mkdir "build" 2>nul || rem
	$(CC) $(CFLAGS) $< -o $@
	
build/handlers.o: kernel/interrupts/handlers/handlers.c
	@mkdir "build" 2>nul || rem
	$(CC) $(CFLAGS) $< -o $@

build/memory.o: kernel/memory/memory.c
	@mkdir "build" 2>nul || rem
	$(CC) $(CFLAGS) $< -o $@
	
build/paging.o: kernel/memory/paging/paging.c
	@mkdir "build" 2>nul || rem
	$(CC) $(CFLAGS) $< -o $@
	
build/bugcheck.o: kernel/bugcheck/bugcheck.c
	@mkdir "build" 2>nul || rem
	$(CC) $(CFLAGS) $< -o $@
	
build/allocator.o: kernel/memory/allocator/allocator.c
	@mkdir "build" 2>nul || rem
	$(CC) $(CFLAGS) $< -o $@

# Assemble ASM to ELF
build/kernel_entry.o: kernel/kernel_entry.asm
	@mkdir "build" 2>nul || rem
	$(ASM) $(ASMFLAGS_ELF) $< -o $@

build/isr_stub.o: kernel/interrupts/isr_stub.asm
	@mkdir "build" 2>nul || rem
	$(ASM) $(ASMFLAGS_ELF) $< -o $@

build/isr_common_stub.o: kernel/interrupts/isr_common_stub.asm
	@mkdir "build" 2>nul || rem
	$(ASM) $(ASMFLAGS_ELF) $< -o $@

# Link kernel
build/kernel.elf: build/kernel_entry.o build/kernel.o build/vga.o build/idt.o build/isr.o build/handlers.o build/memory.o build/paging.o build/bugcheck.o build/allocator.o build/isr_stub.o build/isr_common_stub.o kernel/linker.ld
	@mkdir "build" 2>nul || rem
	$(LD) $(LDFLAGS) -o $@ $^

# Convert to flat binary
build/kernel.bin: build/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@cls
	@echo Successfully Compiled and Linked Kernel

# Assemble bootloaders
build/bootloader.bin: boot/bootloader.asm
	@mkdir "build" 2>nul || rem
	$(ASM) $(ASMFLAGS_BIN) $< -o $@

build/bootloader_stage2.bin: boot/bootloader_stage2.asm
	@mkdir "build" 2>nul || rem
	$(ASM) $(ASMFLAGS_BIN) $< -o $@

# Combine all to final image
build/os-image.img: build/bootloader.bin build/bootloader_stage2.bin build/kernel.bin
	copy /b build\bootloader.bin + build\bootloader_stage2.bin + build\kernel.bin build\os-image.img >nul
	@echo Created OS image successfully.

.PHONY: all clean
