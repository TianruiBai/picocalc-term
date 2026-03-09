/*******************************/
/* YAHAL RPi pico Multi boot GCC linker script */
/*******************************/

MEMORY
{
    FLASH(rx)      : ORIGIN = 0x10000000 + 200k, LENGTH = 2048k - 200k
    RAM(rwx)       : ORIGIN = 0x20000000, LENGTH = 256k
    SCRATCH_X(rwx) : ORIGIN = 0x20040000, LENGTH = 4k
    SCRATCH_Y(rwx) : ORIGIN = 0x20041000, LENGTH = 4k
}

ENTRY(_elf_entry_point)

SECTIONS
{
    /***************************/
    /* Second stage bootloader */
    /***************************/

    /********/
    /* Code */
    /********/
    .text : {
        __image_load__  = LOADADDR(.text);
        __image_start__ = .;

        __vector_start__ = .;
        KEEP (*(.isr_vector))
        . = ALIGN(4);
        KEEP (*(.boot_blocks))
        . = ALIGN(4);
        KEEP (*(.reset))

        *(.text.*)

        *(SORT(.ctors.*))
        *(.ctors)
        *(SORT(.dtors.*))
        *(.dtors)

        KEEP (*(.init))
        KEEP (*(.fini))

        __preinit_array_start = .;
        KEEP (*(.preinit_array*))
        __preinit_array_end = .;

        __init_array_start = .;
        KEEP (*(.init_array*))
        __init_array_end = .;

        __fini_array_start = .;
        KEEP (*(.fini_array*))
        __fini_array_end = .;

    } > FLASH

    /*************************/
    /* Exception information */
    /*************************/
    .ARM.extab : {
        KEEP (*(.ARM.extab* .gnu.linkonce.armextab.*))
    } > FLASH

    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > FLASH

    /******************/
    /* Read-only data */
    /******************/
    .rodata : {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4);
    } > FLASH
    
    /********************************************/
    /* .copy.table used by cmsis_gcc.h          */
    /* The CMSIS code handling the copy tables  */
    /* copies uint32_t values, so the start/end */
    /* values have to be aligned and the size   */
    /* has to be divided by 4.                  */
    /********************************************/
    .copy.table : {
        . = ALIGN(4);
        __copy_table_start__ = .;
        LONG  (__data_load__)
        LONG  (__data_start__)
        LONG ((__data_end__ - __data_start__)/4)
        __copy_table_end__ = .;
    } > FLASH

    /***********************************/
    /* .zero.table used by cmsis_gcc.h */
    /***********************************/
    .zero.table : {
        . = ALIGN(4);
        __zero_table_start__ = .;
        LONG  (__bss_start__)
        LONG ((__bss_end__ - __bss_start__)/4)
        __zero_table_end__ = .;
    } > FLASH

    /****************************************/
    /* data which needs to be copied to RAM */
    /****************************************/
    .data : {
        . = ALIGN (4);
        __data_load__  = LOADADDR(.data);
        __data_start__ = .;
        *(.vtable)
        *(.move_to_ram*)
        *(.data*)
        . = ALIGN (4);
        __data_end__ = .;
    } > RAM AT> FLASH

    __image_end__ = .;

    /***************************************/
    /* uninizialized data (will be zeroed) */
    /***************************************/
    .bss (NOLOAD): {
        . = ALIGN (4);
        __bss_start__ = .;
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.bss*)))
        *(COMMON)
        . = ALIGN (4);
        __bss_end__ = .;
    } > RAM

    __end__ = .;    /* end of written RAM */
    end = __end__;

    /********/
    /* heap */
    /********/
    .heap (NOLOAD): {
        __heap_start__ = .;
        __HeapBase = .;
        *(.heap*)
        __heap_end__ = .;
        __HeapLimit = ORIGIN(RAM) + LENGTH(RAM) - 1;
    } > RAM

    /*********/
    /* stack */
    /*********/
    .stack.x (NOLOAD): {
        *(.stack.x*)
    } > SCRATCH_X

    .stack.y (NOLOAD): {
        *(.stack.y*)
    } > SCRATCH_Y

    /* Lower stack limit */
    __StackLimit = ORIGIN(RAM) + LENGTH(RAM);
    /* Higher stack limits */
    __StackXTop = ORIGIN(SCRATCH_X) + LENGTH(SCRATCH_X);
    __StackYTop = ORIGIN(SCRATCH_Y) + LENGTH(SCRATCH_Y);
    __StackTop  = __StackYTop;
    PROVIDE(__stack = __StackTop);
}
