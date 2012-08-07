/*
 * Derived from:
 * Open IMME https://github.com/jkerdels/open_imme
 * Jochen kerdels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 */

//#define DEBUG 1

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "bcm2835.h"

#define NUM_ATTEMPTS 100

#define PIN_DD RPI_GPIO_P1_11
#define PIN_DC RPI_GPIO_P1_12
#define PIN_RST RPI_GPIO_P1_13


#define ST_CHIP_ERASE_DONE   0x80
#define ST_PCON_IDLE         0x40
#define ST_CPU_HALTED        0x20
#define ST_POWER_MODE_0      0x10
#define ST_HALT_STATUS       0x08
#define ST_DEBUG_LOCKED      0x04
#define ST_OSCILLATOR_STABLE 0x02
#define ST_STACK_OVERFLOW    0x01

#define FLASHPAGE_SIZE       1024
#define FLASH_WORD_SIZE         2
#define WORDS_PER_FLASH_PAGE  512

void critical_error(const char *msg)
{
    fprintf(stderr, "Critical error: %s\n", msg);
    exit(1);
}

static void delay_ns(unsigned int ns)
{
    struct timespec sleeper, dummy;
    sleeper.tv_sec  = 0;
    sleeper.tv_nsec = ns ;
    nanosleep (&sleeper, &dummy) ;
}

static void send_byte(uint8_t ch)
{
    int8_t i;
    bcm2835_gpio_fsel(PIN_DD, BCM2835_GPIO_FSEL_OUTP);

    for (i = 7; i >= 0; i--)
    {
        if (ch & (1 << i))
            bcm2835_gpio_write(PIN_DD, HIGH);
        else
            bcm2835_gpio_write(PIN_DD, LOW);

        bcm2835_gpio_write(PIN_DC, HIGH);
        delay_ns(1);
        bcm2835_gpio_write(PIN_DC, LOW);
        delay_ns(1);
    }
}

static uint8_t recv_byte(void)
{
    uint8_t ch = 0;
    int8_t i;

    bcm2835_gpio_fsel(PIN_DD, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(PIN_DD, BCM2835_GPIO_PUD_DOWN);

    for (i = 7; i >= 0; i--)
    {
        bcm2835_gpio_write(PIN_DC, HIGH);
        delay_ns(1);
        if (bcm2835_gpio_lev(PIN_DD))
            ch |= (1 << i);
        bcm2835_gpio_write(PIN_DC, LOW);
    }
#ifdef DEBUG
    fprintf(stderr, "RX: %02X\n", ch);
#endif
    return ch;
}


int dbg_init(void)
{
    if (!bcm2835_init())
        return 1;

    bcm2835_gpio_fsel(PIN_RST, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(PIN_DD, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(PIN_DC, BCM2835_GPIO_FSEL_OUTP);

    bcm2835_gpio_write(PIN_DD, LOW);

    // send debug init sequence
    bcm2835_gpio_write(PIN_RST, LOW);
    delay(1);
    bcm2835_gpio_write(PIN_DC, LOW);
    delay(1);
    bcm2835_gpio_write(PIN_DC, HIGH);
    delay(1);
    bcm2835_gpio_write(PIN_DC, LOW);
    delay(1);
    bcm2835_gpio_write(PIN_DC, HIGH);
    delay(1);
    bcm2835_gpio_write(PIN_DC, LOW);
    delay(1);
    bcm2835_gpio_write(PIN_RST, HIGH);
    delay(1);

    return 0;
}

void dbg_reset(void)
{
    bcm2835_gpio_write(PIN_RST, LOW);
    delay(100);
    bcm2835_gpio_write(PIN_RST, HIGH);
}

static uint8_t read_status(void)
{
    send_byte(0x34);
    return recv_byte();
}

int dbg_mass_erase(void)
{
    int attempts = NUM_ATTEMPTS;
    send_byte(0x14);
    recv_byte();
    while (attempts && !(read_status() & ST_CHIP_ERASE_DONE))
        attempts--;

    if (0 == attempts)
        return 1;
    return 0;
}

static uint8_t debug_instr_1(uint8_t in0)
{
    send_byte(0x55);    
    send_byte(in0); 
    return recv_byte();
}

static uint8_t debug_instr_2(uint8_t in0, uint8_t in1)
{
    send_byte(0x56);    
    send_byte(in0); 
    send_byte(in1); 
    return recv_byte();
}

static uint8_t debug_instr_3(uint8_t in0, uint8_t in1, uint8_t in2)
{
    send_byte(0x57);    
    send_byte(in0); 
    send_byte(in1); 
    send_byte(in2); 
    return recv_byte();
}

static void write_xdata_memory(uint16_t address, uint16_t count, const uint8_t *buf)
{
    int i;
    debug_instr_3(0x90,address >> 8,address);
    for (i = 0; i < count; ++i) {
        debug_instr_2(0x74, buf[i]);
        debug_instr_1(0xF0);
        debug_instr_1(0xA3);    
    }
}

static void set_pc(uint16_t address)
{
    debug_instr_3(0x02,address >> 8,address);
}

static void cpu_resume(void)
{
    send_byte(0x4C);
    recv_byte(); // ignore sent value
}



static void read_code_memory(uint16_t address, 
                      uint8_t  bank, 
                      uint16_t count, 
                      uint8_t *outputData)
{
    int i;
    if (address >= 0x8000)
        address = (address & 0x7FFF) + (bank * 0x8000);

    debug_instr_3(0x75,0xC7,(bank * 16) + 1);
    debug_instr_3(0x90,address >> 8,address);
    for (i = 0; i < count; ++i) {
        debug_instr_1(0xE4);
        outputData[i] = debug_instr_1(0x93);
        debug_instr_1(0xA3);
    }
}

static uint8_t updProc[] =
{ 
    0x75, 0xAD, /*ADDRESS*/0x00,
    0x75, 0xAC, 0x00, 
    0x75, 0xAB, 0x23, 0x00,
    0x75, 0xAE, 0x01, // ------
    0xE5, 0xAE,       // erase code
    0x20, 0xE7, 0xFB, // ------
    0x90, 0xF0, 0x00,
    0x7F, WORDS_PER_FLASH_PAGE >> 8,
    0x7E, WORDS_PER_FLASH_PAGE & 0xFF,
    0x75, 0xAE, 0x02,
    0x7D, FLASH_WORD_SIZE,
    0xE0, 
    0xA3, 
    0xF5, 0xAF,
    0xDD, 0xFA,
    0xE5, 0xAE,
    0x20, 0xE6, 0xFB,
    0xDE, 0xF1,
    0xDF, 0xEF,
    0xA5
};


static int write_flash_page(uint32_t address, const uint8_t *buf)
{
    int attempts = NUM_ATTEMPTS;
    uint8_t updProcSize = sizeof(updProc);

    updProc[2] = ((address >> 8) / FLASH_WORD_SIZE) & 0x7E;

    write_xdata_memory(0xF000,  FLASHPAGE_SIZE, buf);

    write_xdata_memory(0xF000 + FLASHPAGE_SIZE, updProcSize, updProc);
    debug_instr_3(0x75, 0xC7, 0x51);

    set_pc(0xF000 + FLASHPAGE_SIZE);
    cpu_resume();
    while (attempts && !(read_status() & ST_CPU_HALTED))
        attempts--;

    if (0 == attempts)
        return 1;
    return 0;
}



static int read_flash_page(uint32_t address, uint8_t *outputData)
{
    read_code_memory(address & 0xFFFF, 
                     (address >> 15) & 0x03, FLASHPAGE_SIZE, outputData);
    return 0;
}


int dbg_readpage(uint8_t page, uint8_t *buf)
{
    return read_flash_page(page * 1024, buf);
}

int dbg_writepage(uint8_t page, const uint8_t *buf)
{
    uint32_t addr = page*1024;
    return write_flash_page(addr, buf);
}

