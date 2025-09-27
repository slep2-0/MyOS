# Variables
TOOLCHAIN_PATH = tools64/bin

ASM = nasm
CC = $(TOOLCHAIN_PATH)/x86_64-elf-gcc
LD = $(TOOLCHAIN_PATH)/x86_64-elf-ld
OBJCOPY = $(TOOLCHAIN_PATH)/x86_64-elf-objcopy

# Flags
ASMFLAGS_ELF = -f elf64
ASMFLAGS_BIN = -f bin

# Base CFLAGS (no optimization level hardcoded here)
CFLAGS = -std=gnu11 \
         -m64 -ffreestanding -c \
         -fdiagnostics-color=always \
         -fdiagnostics-show-option \
         -fno-omit-frame-pointer \
         -Wno-unused-function \
         -Wall -Wextra -Werror -Wmissing-prototypes \
         -Wstrict-prototypes -Wshadow -Wcast-align \
         -fdebug-prefix-map="/home/kali/Desktop/Operating System=C:/Users/matanel/Desktop/Projects/KernelDevelopment" \
         -mcmodel=large -mno-red-zone -fno-pie -fno-pic

# Scheduler special flags (frame-pointer and no tail-call)
SCHED_EXTRA = -fno-optimize-sibling-calls

# Set optimization based on DEBUG
ifeq ($(DEBUG),1)
    CFLAGS += -DDEBUG -O0 -g -fstack-protector-strong -fstack-clash-protection
else
    CFLAGS += -O2
endif

ifeq ($(GDB),1)
    CFLAGS += -DGDB -g
endif

# $(SCHED_CFLAGS) means no optimizations will be applied on the C file.
ifeq ($(DEBUG),1)
    SCHED_CFLAGS = $(CFLAGS) $(SCHED_EXTRA)
else
    SCHED_CFLAGS = $(filter-out -O2,$(CFLAGS)) $(SCHED_EXTRA)
endif

# Linker flags
LDFLAGS = -T kernel/linker.ld -static -nostdlib -m elf_x86_64

# Targets
all: clearlog build/os-image.img

clearlog:
	@echo "" > log.txt

clean:
	rm -f build/*.o build/*.elf build/os-image.img

# Compile C files with common CFLAGS
build/kernel.o: kernel/kernel.c
	mkdir -p build
	$(CC) $(SCHED_CFLAGS) $< -o $@ >> log.txt 2>&1

build/idt.o: kernel/core/interrupts/idt.c
	mkdir -p build
	$(CC) $(CFLAGS)  $< -o $@ >> log.txt 2>&1

build/isr.o: kernel/core/interrupts/isr.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/handlers.o: kernel/core/interrupts/handlers/handlers.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/memory.o: kernel/core/memory/memory.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/paging.o: kernel/core/memory/paging/paging.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/bugcheck.o: kernel/core/bugcheck/bugcheck.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/allocator.o: kernel/core/memory/allocator/allocator.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/ahci.o: kernel/drivers/ahci/ahci.c
	mkdir -p build
	$(CC) $(SCHED_CFLAGS) $< -o $@ >> log.txt 2>&1

build/block.o: kernel/drivers/blk/block.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/fat32.o: kernel/filesystem/fat32/fat32.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/gop.o: kernel/drivers/gop/gop.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/irql.o: kernel/core/irql/irql.c
	mkdir -p build
	$(CC) $(SCHED_CFLAGS) $< -o $@ >> log.txt 2>&1

build/scheduler.o: kernel/core/scheduler/scheduler.c
	mkdir -p build
	$(CC) $(SCHED_CFLAGS) $< -o $@ >> log.txt 2>&1

build/dpc.o: kernel/core/dpc/dpc.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/thread.o: kernel/core/thread/thread.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

# DPC list can use common CFLAGS
build/dpc_list.o: kernel/core/dpc/dpc_list.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1
	
build/vfs.o: kernel/filesystem/vfs/vfs.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1
	
build/pit.o: kernel/cpu/apic/pit.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/apic.o: kernel/cpu/apic/apic.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

build/mutex.o: kernel/core/mutex/mutex.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1
	
build/events.o: kernel/core/events/events.c
	mkdir -p build
	$(CC) $(SCHED_CFLAGS) $< -o $@ >> log.txt 2>&1

build/debugfunctions.o: kernel/debug/debugfunctions.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1
	
build/smp.o: kernel/cpu/smp/smp.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1
	
build/ap_main.o: kernel/cpu/smp/ap_main.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1
	
build/acpi.o: kernel/core/acpi/acpi.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ >> log.txt 2>&1

# Assemble ASM to ELF
build/kernel_entry.o: kernel/kernel_entry.asm
	mkdir -p build
	$(ASM) $(ASMFLAGS_ELF) $< -o $@ >> log.txt 2>&1

build/isr_stub.o: kernel/core/interrupts/isr_stub.asm
	mkdir -p build
	$(ASM) $(ASMFLAGS_ELF) $< -o $@ >> log.txt 2>&1

build/capture_registers.o: kernel/intrinsics/capture_registers.asm
	mkdir -p build
	$(ASM) $(ASMFLAGS_ELF) $< -o $@ >> log.txt 2>&1

build/context.o: kernel/core/scheduler/context.asm
	mkdir -p build
	$(ASM) $(ASMFLAGS_ELF) $< -o $@ >> log.txt 2>&1

build/cpuid.o: kernel/cpu/cpuid/cpuid.asm
	mkdir -p build
	$(ASM) $(ASMFLAGS_ELF) $< -o $@ >> log.txt 2>&1

build/mutex_asm.o: kernel/core/mutex/mutex.asm
	mkdir -p build
	$(ASM) $(ASMFLAGS_ELF) $< -o $@ >> log.txt 2>&1
	
build/ap_trampoline.bin: kernel/cpu/smp/ap_trampoline.asm
	mkdir -p build
	$(ASM) $(ASMFLAGS_BIN) $< -o $@	

build/ap_trampoline.o: build/ap_trampoline.bin
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
		--rename-section .data=.aptrampoline \
		$< $@

# Link kernel
build/kernel.elf: build/kernel_entry.o build/kernel.o build/idt.o build/isr.o build/handlers.o build/memory.o \
                      build/paging.o build/bugcheck.o build/allocator.o build/ahci.o build/block.o \
                      build/fat32.o build/gop.o build/irql.o build/scheduler.o build/dpc.o build/dpc_list.o \
                      build/thread.o build/vfs.o build/pit.o build/apic.o build/events.o build/mutex.o build/smp.o build/ap_main.o build/acpi.o build/ap_trampoline.o build/debugfunctions.o build/isr_stub.o build/capture_registers.o build/context.o build/cpuid.o \
                      build/mutex_asm.o
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
