OUTPUT_FORMAT("elf64-x86-64")
ENTRY(_start)

SECTIONS
{
  /* Define the Image Base (Virtual Memory Address) */
  PROVIDE(__image_base = 0x10000);

  /* Set the Virtual Address Counter to the Image Base */
  . = __image_base;
  
  /* Capture the 'File Start' VMA for RVA math */
  __file_start = .;

  /* --- HEADER --- */
  /* VMA = 0x10000. LMA = 0x10000 - 0x10000 = 0 */
  .mtheader : AT(ADDR(.mtheader) - __image_base)
  { 
    KEEP(*(.mtheader)) 
  }
  __header_end = .;

  /* --- TEXT --- */
  . = ALIGN(0x1000);
  __text_start = .;
  .text : AT(ADDR(.text) - __image_base)
  { 
    *(.text) *(.text.*) 
    *(.rodata) *(.rodata*) 
  } 
  __text_end = .;

  /* --- DATA --- */
  . = ALIGN(0x1000);
  __data_start = .;
  .data : AT(ADDR(.data) - __image_base)
  { 
    *(.data) *(.data.*)
  }
  __data_end = .;

  /* --- EXPORTS --- */
  . = ALIGN(8);
  __mtexports_start = .;
  .mtexports : AT(ADDR(.mtexports) - __image_base)
  {
      KEEP(*(.mtexports))
  }
  __mtexports_arr_end = .;
  
  .mtexports.str : AT(ADDR(.mtexports.str) - __image_base)
  {
      KEEP(*(.mtexports.str))
  }
  __mtexports_end = .;
  
  /* Export logic */
  PROVIDE(__mtexports_table_size = __mtexports_arr_end - __mtexports_start);
  PROVIDE(__mtexports_rva  = __mtexports_start - __file_start);

  /* --- RELOCATIONS --- */
  . = ALIGN(8);
  __reloc_start = .;
  .reloc : AT(ADDR(.reloc) - __image_base)
  {
      *(.rela.dyn)
      *(.rela.data)
  }
  __reloc_end = .;
  
  PROVIDE(__reloc_size = __reloc_end - __reloc_start);
  PROVIDE(__reloc_rva  = __reloc_start - __file_start);

  /* --- IMPORTS --- */
  . = ALIGN(8);
  __mtimports_start = .; 
  
  .mtimports : AT(ADDR(.mtimports) - __image_base)
  {
      KEEP(*(.mtimports))
  }
  __mtimports_arr_end = .; /* Snap the end of the structs here */
  
  .mtimports.str : AT(ADDR(.mtimports.str) - __image_base)
  {
      KEEP(*(.mtimports.str))
  }
  __mtimports_end = .;

  /* Calculate RVA and Size for the STRUCTS ONLY */
  PROVIDE(__mtimports_rva  = __mtimports_start - __file_start);
  PROVIDE(__mtimports_size = __mtimports_arr_end - __mtimports_start);

  /* --- BSS --- */
  __bss_start = .;
  .bss : AT(ADDR(.bss) - __image_base)
  { 
    *(.bss) 
  }
  __bss_end = .;

  __file_end = .;

  /* Symbols */
  PROVIDE(__text_size  = __text_end - __text_start);
  PROVIDE(__data_size  = __data_end - __data_start);
  PROVIDE(__bss_size   = __bss_end  - __bss_start);
  PROVIDE(__entry_rva  = _start - __file_start);
  PROVIDE(__text_rva   = __text_start - __file_start);
  PROVIDE(__data_rva   = __data_start - __file_start);
}