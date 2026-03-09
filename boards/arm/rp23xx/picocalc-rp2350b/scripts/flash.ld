/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/scripts/flash.ld
 *
 * Linker script for PicoCalc RP2350B-Plus-W
 * - 16 MB flash (XIP)
 * - 520 KB SRAM
 * - 8 MB PSRAM (optional, managed as heap)
 *
 ****************************************************************************/

MEMORY
{
    /* RP2350B flash: 16 MB, memory-mapped via XIP at 0x10000000 */
    flash (rx)  : ORIGIN = 0x10000000, LENGTH = 16M

    /* RP2350B SRAM: 520 KB
     * 10 x 32KB banks = 320KB SRAM0-9
     * + additional SRAM regions
     */
    sram (rwx)  : ORIGIN = 0x20000000, LENGTH = 520K

    /* PSRAM is managed as a separate heap (see rp23xx_psram.c),
     * not included in the linker memory map.
     * Approximate mapping: 0x11000000, 8M (verify with RP2350 docs)
     */
}

/* Entry point */
ENTRY(_entry_point)

SECTIONS
{
    /* Boot2 stage (RP2350 specific) */
    .boot2 :
    {
        __boot2_start__ = .;
        KEEP (*(.boot2))
        __boot2_end__ = .;
    } > flash

    /* Vector table */
    .vectors :
    {
        . = ALIGN(256);
        _svectors = .;
        KEEP(*(.vectors))
        _evectors = .;
    } > flash

    /* Code */
    .text :
    {
        _stext = .;
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.gnu.linkonce.t.*)
        *(.gnu.linkonce.r.*)
        . = ALIGN(4);
        _etext = .;
    } > flash

    /* ARM exception tables */
    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > flash

    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > flash

    /* Initialized data (copied from flash to SRAM at startup) */
    .data :
    {
        _sdata = .;
        *(.data .data.*)
        *(.gnu.linkonce.d.*)
        . = ALIGN(4);
        _edata = .;
    } > sram AT > flash

    _eronly = LOADADDR(.data);

    /* BSS (zero-initialized) */
    .bss (NOLOAD) :
    {
        _sbss = .;
        *(.bss .bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > sram

    /* Heap starts after BSS */
    _sheap = .;
    _eheap = ORIGIN(sram) + LENGTH(sram);
}

/* Stack (NuttX manages this via configuration) */
PROVIDE(_sstack = ORIGIN(sram) + LENGTH(sram));
