OUTPUT_FORMAT("elf64-x86-64")
ENTRY(_start)

SECTIONS
{
  PROVIDE(__image_base = 0x10000);
  . = __image_base;
  __file_start = .;

  .mtheader : AT(ADDR(.mtheader) - __image_base)
  {
    BYTE(0)
    . = __file_start + 128;
  }

  . = ALIGN(0x1000);
  __text_start = .;
  .mtapi.text : AT(ADDR(.mtapi.text) - __image_base)
  {
    *(.text.mtapi)
    *(.text.mtapi.*)
  }
  .text : AT(ADDR(.text) - __image_base)
  {
    *(.text)
    *(.text.*)
    *(.plt)
    *(.plt.*)
    *(.rodata)
    *(.rodata.*)
  }
  __text_end = .;

  . = ALIGN(0x1000);
  __data_start = .;
  .data : AT(ADDR(.data) - __image_base)
  {
    *(.data)
    *(.data.*)
    *(.data.rel.ro)
    *(.data.rel.ro.*)
    *(.got)
    *(.got.*)
  }
  __data_end = .;

  . = ALIGN(0x1000);
  __mt_metadata_start = .;
  .mtmetadata : AT(ADDR(.mtmetadata) - __image_base)
  {
    BYTE(0)
    . = __mt_metadata_start + __mt_metadata_size;
  }
  __mt_metadata_end = .;
  __file_end = .;

  . = ALIGN(0x1000);
  ASSERT(. == __file_end, "MTE metadata reservation must end on a page boundary")
  __bss_start = .;
  .bss (NOLOAD) :
  {
    *(.bss)
    *(.bss.*)
    *(COMMON)
  }
  __bss_end = .;

  /* Link-only ELF metadata. The MTE packer consumes but does not copy it. */
  .dynsym : { *(.dynsym) }
  .dynstr : { *(.dynstr) }
  .gnu.hash : { *(.gnu.hash) }
  .hash : { *(.hash) }
  .gnu.version : { *(.gnu.version) }
  .gnu.version_d : { *(.gnu.version_d) }
  .gnu.version_r : { *(.gnu.version_r) }
  .rela.dyn : { *(.rela.dyn) }
  .rela.plt : { *(.rela.plt) }
  .dynamic : { *(.dynamic) }

  /DISCARD/ :
  {
    *(.eh_frame)
    *(.eh_frame_hdr)
    *(.interp)
    *(.note*)
  }

  PROVIDE(__text_rva = __text_start - __file_start);
  PROVIDE(__text_size = __text_end - __text_start);
  PROVIDE(__data_rva = __data_start - __file_start);
  PROVIDE(__data_size = __data_end - __data_start);
  PROVIDE(__bss_size = __bss_end - __bss_start);
}
