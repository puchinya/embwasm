MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
  SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 512K
}

ENTRY(Reset_Handler)

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text .text*)
    *(.rodata .rodata*)
    . = ALIGN(4);
    _init_array_start = .;
    KEEP(*(.init_array*))
    _init_array_end = .;
    KEEP(*(.fini_array*))
    . = ALIGN(4);
    _etext = .;
  } > FLASH

  _sidata = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data .data*)
    . = ALIGN(4);
    _edata = .;
  } > SRAM AT > FLASH

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss .bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > SRAM

  ._heap_stack :
  {
    . = ALIGN(8);
    _end = .;
    PROVIDE(end = .);
    . = . + 0x8000;
    . = ALIGN(8);
    _estack = ORIGIN(SRAM) + LENGTH(SRAM);
  } > SRAM
}
