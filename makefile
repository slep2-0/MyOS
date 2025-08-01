# Variables
TOOLCHAIN_PATH = tools64/bin

ASM = nasm
CC = $(TOOLCHAIN_PATH)/x86_64-elf-gcc
LD = $(TOOLCHAIN_PATH)/x86_64-elf-ld
OBJCOPY = $(TOOLCHAIN_PATH)/x86_64-elf-objcopy

# Flags
ASMFLAGS_ELF = -f elf64
ASMFLAGS_BIN = -f bin
# warning as errors ENABLED: -Werror -Wmissing-prototypes -Wstrict-prototypes -Wshadow -Wcast-align
CFLAGS = -std=gnu99 -m64 -ffreestanding -c -Wall -Wextra -Werror -Wmissing-prototypes -Wstrict-prototypes -Wshadow -Wcast-align
LDFLAGS = -T kernel/linker.ld -static -nostdlib -m elf_x86_64

ifeq ($(DEBUG), 1)
    CFLAGS += -DDEBUG -g -O0
endif

# Targets
all: clearlog build/os-image.img

clearlog:
	@echo "" > log.txt

clean:
	rm -f build/*.o build/*.elf build/os-image.img

# Compile C files
build/kernel.o: kernel/kernel.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/idt.o: kernel/interrupts/idt.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/isr.o: kernel/interrupts/isr.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/handlers.o: kernel/interrupts/handlers/handlers.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/memory.o: kernel/memory/memory.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/paging.o: kernel/memory/paging/paging.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/bugcheck.o: kernel/bugcheck/bugcheck.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/allocator.o: kernel/memory/allocator/allocator.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/ata.o: kernel/drivers/blk/ata.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/block.o: kernel/drivers/blk/block.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/fat32.o: kernel/filesystem/fat32/fat32.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/gop.o: kernel/drivers/gop/gop.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1
	
build/irql.o: kernel/cpu/irql/irql.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/scheduler.o: kernel/cpu/scheduler/scheduler.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1
	
build/dpc.o: kernel/cpu/dpc/dpc.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/thread.o: kernel/cpu/thread/thread.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

# Assemble ASM to ELF
build/kernel_entry.o: kernel/kernel_entry.asm
	mkdir -p build
	$(ASM) $(ASMFLAGS_ELF) $< -o $@ >> log.txt 2>&1

build/isr_stub.o: kernel/interrupts/isr_stub.asm
	mkdir -p build
	$(ASM) $(ASMFLAGS_ELF) $< -o $@ >> log.txt 2>&1
	
build/paging_asm.o: kernel/memory/paging/paging_asm.asm
	mkdir -p build
	$(ASM) $(ASMFLAGS_ELF) $< -o $@ >> log.txt 2>&1

build/capture_registers.o: kernel/intrin/capture_registers.asm
	mkdir -p build
	$(ASM) $(ASMFLAGS_ELF) $< -o $@ >> log.txt 2>&1
	
build/context.o: kernel/cpu/scheduler/context.asm
	mkdir -p build
	$(ASM) $(ASMFLAGS_ELF) $< -o $@ >> log.txt 2>&1

# Link kernel
build/kernel.elf: build/kernel_entry.o build/kernel.o build/idt.o build/isr.o build/handlers.o build/memory.o build/paging.o build/bugcheck.o build/allocator.o build/ata.o build/block.o build/fat32.o build/gop.o build/irql.o build/scheduler.o build/dpc.o build/thread.o build/isr_stub.o build/paging_asm.o build/capture_registers.o build/context.o kernel/linker.ld
	mkdir -p build
	$(LD) $(LDFLAGS) -o $@ $^ >> log.txt 2>&1

# Convert to flat binary
build/kernel.bin: build/kernel.elf
	$(OBJCOPY) -O binary $< $@ >> log.txt 2>&1
	@clear
	@echo "Successfully Compiled and Linked Kernel"

# Combine all to final image
build/os-image.img: build/kernel.bin
	cat build/kernel.bin > build/os-image.img
	@echo "Created OS image successfully."

.PHONY: all clean clearlog
