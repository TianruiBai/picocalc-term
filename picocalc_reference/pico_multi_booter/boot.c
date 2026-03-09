
// .------------------------------------------------------------------------.
// | MIT Licensed : Copyright 2022, "Hippy"                                 |
// `------------------------------------------------------------------------'

// **************************************************************************
// *    Check we did build booter with 'Copy to RAM' option                 *
// **************************************************************************

#if !PICO_COPY_TO_RAM
    #error "The booter must use: pico_set_binary_type(${BOOT} copy_to_ram)"
#endif

// **************************************************************************
// *    Pico SDK integration                                                *
// **************************************************************************

#include <stdio.h>
#include "RP2040.h"
#include "pico/stdlib.h"
#include "hardware/resets.h"
#include "hardware/gpio.h"

// **************************************************************************
// *    Utility code                                                        *
// **************************************************************************

#define PEEK32(adr)     *((uint32_t *) (adr)) 
#define POKE32(adr, n)  *((uint32_t *) (adr)) = n  

#define PEEK8(adr)      *((uint8_t  *) (adr))

uint32_t Align(uint32_t n, uint32_t mask) {
    if ((n & (mask-1)) != 0) {
        n = (n | (mask-1)) + 1;
    }
    return n;
}

uint32_t Size(uint32_t n, uint32_t mask) {
    return (((n & 0x0FFFFFFF) | (mask-1)) + 1) / 1024;
}

uint32_t Crc32(uint32_t crc, uint8_t byt) {
    for (uint32_t n = 8; n-- ;
         crc = (crc << 1) ^ (((crc >> 31) ^ (byt >> 7)) * 0x04C11DB7),
         byt <<= 1
    );
    return crc;
}

// **************************************************************************
// *    Low-level interfacing                                               *
// **************************************************************************

extern char __flash_binary_end;

// Credit : https://github.com/usedbytes/rp2040-serial-bootloader
//          Copyright (c) 2021 Brian Starkey <stark3y@gmail.com>
//          SPDX-License-Identifier: BSD-3-Clause

static void disable_interrupts(void) {
    SysTick->CTRL &= ~1;

    NVIC->ICER[0] = 0xFFFFFFFF;
    NVIC->ICPR[0] = 0xFFFFFFFF;
}

static void reset_peripherals(void) {
    reset_block(~(
        RESETS_RESET_IO_QSPI_BITS   |
        RESETS_RESET_PADS_QSPI_BITS |
        RESETS_RESET_SYSCFG_BITS    |
        RESETS_RESET_PLL_SYS_BITS
    ));
}

static void jump_to_vtor(uint32_t vtor) {
    // Derived from the Leaf Labs Cortex-M3 bootloader.
    // Copyright (c) 2010 LeafLabs LLC.
    // Modified 2021 Brian Starkey <stark3y@gmail.com>
    // Originally under The MIT License
    uint32_t reset_vector = *(volatile uint32_t *)(vtor + 0x04);

    SCB->VTOR = (volatile uint32_t)(vtor);

    asm volatile("msr msp, %0"::"g" (*(volatile uint32_t *)vtor));
    asm volatile("bx %0"::"r" (reset_vector));
}

#define LEDPIN 25
#define SD_DET_PIN 22
bool sd_card_inserted(void)
{
    // Active low detection - returns true when pin is low
    return !gpio_get(SD_DET_PIN);
}

// **************************************************************************
// *    Main booter program                                                 *
// **************************************************************************

int main(void) {
    while (true) {

        stdio_init_all();
		sleep_ms(1000);


		gpio_init(SD_DET_PIN);
		gpio_set_dir(SD_DET_PIN, GPIO_IN);
		gpio_pull_up(SD_DET_PIN); // Enable pull-up resistor
		
		gpio_init(LEDPIN);
		gpio_set_dir(LEDPIN,GPIO_OUT);
		gpio_put(LEDPIN,1);

        // Determine where the boot binary ends
        uintptr_t last = (uintptr_t) &__flash_binary_end;
        printf("Booter ends at %08X\n", last);
        // Determine the size in 1K blocks
        printf("Booter size is %luk\n", Size(last, 1024));
        // Determine the size in 4K blocks
        printf("Booter used is %luk\n", Size(last, 4 * 1024));
        // Determine the start of the next 256 byte block
        printf("App info table %08lX\n", Align(last, 256));
        // Determine the start of the next 4K block
        printf("App base is at %08lX\n", Align(last, 4 * 1024));
        printf("\n");

        // Find the start of the next 256 byte block, the info table
        uint32_t info = Align(last, 256);
        uint32_t addr;
        // Report items in the info table
        int max = 0;
        for (uint32_t item=0; item < 16; item++) {
            addr  = PEEK32(info + (item * 16));
            if (addr != 0) {
                printf("%lu : %08lX ", item+1, addr);
                for (uint32_t cptr=4; cptr < 16; cptr++) {
                    char c = PEEK8(info + (item * 16 ) + cptr);
                    if (c != 0) {
                        printf("%c", c);
                    }
                }
                printf("\n");
                max++;
            }
        }
        printf("max %d \n",max);

        // Choose an app to launch
		/*
        int chosen;
		if(!sd_card_inserted()){
		  printf("No sd card\n");
		  chosen = 0;
		}else{
		  printf("Has SD card\n");
		  chosen = max-1;
		  if(chosen <0 ) chosen = 0;
		}		
		*/
		int chosen = 0;
        // Get start address of app
        addr = PEEK32( info + ((chosen * 16)));
        printf("Application at %08lX\n",  addr);

        // Determine if the start of the application is actually
        // the second stage bootloader. if so we need to skip
        // beyond that.

        uint32_t crc = 0xFFFFFFFF;
        for(uint32_t pc = 0; pc < 0xFC; pc += 4) {
             uint32_t u32 = PEEK32(addr + pc);
             crc = Crc32(crc, (u32 >>  0 ) & 0xFF);
             crc = Crc32(crc, (u32 >>  8 ) & 0xFF);
             crc = Crc32(crc, (u32 >> 16 ) & 0xFF);
             crc = Crc32(crc, (u32 >> 24 ) & 0xFF);
        }
        if (crc == PEEK32(addr + 0xFC)) {
            printf("Stage 2 adjust\n");
            addr += 256;
            printf("Application at %08lX\n", addr);
        }
        printf("\n");

        printf("Launching application code ...\n");
        printf("\n");

        sleep_ms(1000);

        disable_interrupts();
        reset_peripherals();
        jump_to_vtor(addr);
    }

}
