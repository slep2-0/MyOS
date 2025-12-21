OUTPUT_FORMAT("elf64-x86-64")
ENTRY(_start)

SECTIONS
{
  PROVIDE(__image_base = 0x10000);

  . = __image_base;
  __file_start = .;

  /* Header */
  .mtheader : { KEEP(*(.mtheader)) }
  __header_end = .;

  /* Align Text to Page Boundary */
  . = ALIGN(0x1000);

  __text_start = .;
  /* Merge .rodata here so string literals are loaded with code */
  .text : { *(.text) *(.rodata*) } 
  __text_end = .;

  /* Align Data to Page Boundary */
  . = ALIGN(0x1000);

  __data_start = .;
  .data : { *(.data) }
  __data_end = .;

  /* BSS */
  __bss_start = .;
  .bss : { *(.bss) }
  __bss_end = .;

  __file_end = .;

  /* Symbols */
  PROVIDE(__text_size  = __text_end - __text_start);
  PROVIDE(__data_size  = __data_end - __data_start);
  PROVIDE(__bss_size   = __bss_end  - __bss_start);
  PROVIDE(__entry_rva  = _start - __file_start);
  PROVIDE(__text_rva   = __text_start - __file_start);
  PROVIDE(__data_rva   = __data_start - __file_start); /* ADDED */
}
