# Tools
ASM = nasm
CC = i686-elf-gcc
LD = i686-elf-ld
OBJCOPY = i686-elf-objcopy

# Flags
ASMFLAGS_ELF = -f elf32
ASMFLAGS_BIN = -f bin
CFLAGS = -m32 -ffreestanding -c -Wall -Wextra -Werror -Wmissing-prototypes -Wstrict-prototypes -Wshadow -Wconversion -Wcast-align
LDFLAGS = -T kernel/linker.ld -static -nostdlib -m elf_i386

# Targets
all: clearlog build/os-image.img

clearlog:
	@echo. > log.txt

clean:
	del /Q build\*.o build\*.bin build\*.elf build\os-image.img 2>nul || echo

# Compile C files
build/kernel.o: kernel/kernel.c
	@mkdir "build" 2>nul || rem
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/vga.o: kernel/screen/vga/vga.c
	@mkdir "build" 2>nul || rem
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/idt.o: kernel/interrupts/idt.c
	@mkdir "build" 2>nul || rem
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/isr.o: kernel/interrupts/isr.c
	@mkdir "build" 2>nul || rem
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1
	
build/handlers.o: kernel/interrupts/handlers/handlers.c
	@mkdir "build" 2>nul || rem
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/memory.o: kernel/memory/memory.c
	@mkdir "build" 2>nul || rem
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1
	
build/paging.o: kernel/memory/paging/paging.c
	@mkdir "build" 2>nul || rem
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1
	
build/bugcheck.o: kernel/bugcheck/bugcheck.c
	@mkdir "build" 2>nul || rem
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1
	
build/allocator.o: kernel/memory/allocator/allocator.c
	@mkdir "build" 2>nul || rem
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

# Assemble ASM to ELF
build/kernel_entry.o: kernel/kernel_entry.asm
	@mkdir "build" 2>nul || rem
	$(ASM) $(ASMFLAGS_ELF) $< -o $@ >> log.txt 2>&1

build/isr_stub.o: kernel/interrupts/isr_stub.asm
	@mkdir "build" 2>nul || rem
	$(ASM) $(ASMFLAGS_ELF) $< -o $@ >> log.txt 2>&1

build/isr_common_stub.o: kernel/interrupts/isr_common_stub.asm
	@mkdir "build" 2>nul || rem
	$(ASM) $(ASMFLAGS_ELF) $< -o $@ >> log.txt 2>&1

# Link kernel
build/kernel.elf: build/kernel_entry.o build/kernel.o build/vga.o build/idt.o build/isr.o build/handlers.o build/memory.o build/paging.o build/bugcheck.o build/allocator.o build/isr_stub.o build/isr_common_stub.o kernel/linker.ld
	@mkdir "build" 2>nul || rem
	$(LD) $(LDFLAGS) -o $@ $^ >> log.txt 2>&1

# Convert to flat binary
build/kernel.bin: build/kernel.elf
	$(OBJCOPY) -O binary $< $@ >> log.txt 2>&1
	@cls
	@echo Successfully Compiled and Linked Kernel

# Assemble bootloaders
build/bootloader.bin: boot/bootloader.asm
	@mkdir "build" 2>nul || rem
	$(ASM) $(ASMFLAGS_BIN) $< -o $@ >> log.txt 2>&1

build/bootloader_stage2.bin: boot/bootloader_stage2.asm
	@mkdir "build" 2>nul || rem
	$(ASM) $(ASMFLAGS_BIN) $< -o $@ >> log.txt 2>&1

# Combine all to final image
build/os-image.img: build/bootloader.bin build/bootloader_stage2.bin build/kernel.bin
	copy /b build\bootloader.bin + build\bootloader_stage2.bin + build\kernel.bin build\os-image.img >nul
	@echo Created OS image successfully.

.PHONY: all clean clearlog
