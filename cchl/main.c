/*
 * CCHL - ChipCon Hardware Loader
 * A hardware programmer for the CC1110/CC1111 which runs on the CC1110/CC1111
 * Joby Taffey (c) 2012 <jrt-cctl@hodgepig.org>
 *
 * Derived from:
 * CC Bootloader
 * Fergus Noble (c) 2011
 * 
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

#include <stdint.h>
#include <cc1110.h>
#include "cc1110-ext.h"


// Connect the target CC1110 up as follows
#define DD P1_6
#define DD_BIT BIT6
#define DC P1_5
#define DC_BIT BIT5
#define RST P1_4
#define RST_BIT BIT4

#define RXFIFO_ELEMENTS 2048
#define RXFIFO_SIZE (RXFIFO_ELEMENTS - 1)
static __xdata uint8_t rxfifo[RXFIFO_SIZE];
static uint8_t rxfifo_in;
static uint8_t rxfifo_out;
static const __code uint8_t * __at (0x0000) flashp;
__xdata uint8_t rambuf[1024];
static uint8_t page;

static const char banner[] = {'\r', '\n', 'C', 'C', 'H', 'L', '\r', '\n'};


#define BIT0 1
#define BIT1 2
#define BIT2 4
#define BIT3 8
#define BIT4 16
#define BIT5 32
#define BIT6 64
#define BIT7 128


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

#define nop()   __asm nop __endasm;

void delay (unsigned char n)
{
    unsigned char i = 0;
    unsigned char j = 0;

    n <<= 1;
    while (--n != 0)
        while (--i != 0)
            while (--j != 0)
                nop();
}

static void send_byte(uint8_t ch)
{
    int8_t i;
    P1DIR |= DD_BIT; // output

    for (i = 7; i >= 0; i--)
    {
        if (ch & (1 << i))
            DD = 1;
        else
            DD = 0;
        DC = 1;
        DC = 0;
    }
}

static uint8_t recv_byte(void)
{
    uint8_t ch = 0;
    int8_t i;

    P1DIR &= ~DD_BIT; // input

    for (i = 7; i >= 0; i--)
    {
        DC = 1;
        if (DD)
            ch |= (1 << i);
        DC = 0;
    }
    return ch;
}


static void dbg_init(void)
{
    P1DIR |= RST_BIT;
    P1DIR |= DC_BIT;  // DC
    P1DIR |= DD_BIT;  // DD
    DD = 0;

    // send debug init sequence
    RST = 0;
    delay(1);
    DC = 0;
    delay(1);
    DC = 1;
    delay(1);
    DC = 0;
    delay(1);
    DC = 1;
    delay(1);
    DC = 0;
    delay(1);
    RST = 1;
    delay(1);
}

static uint8_t read_status(void)
{
    send_byte(0x34);
    return recv_byte();
}

static void dbg_mass_erase(void)
{
    send_byte(0x14);
    recv_byte();
    while (!(read_status() & ST_CHIP_ERASE_DONE));
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

static void write_xdata_memory(uint16_t address, uint16_t count, const __xdata uint8_t *buf)
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
                      __xdata uint8_t *outputData)
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

static __xdata uint8_t updProc[] =
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


static void write_flash_page(uint32_t address)
{
    uint8_t updProcSize = sizeof(updProc);

    updProc[2] = ((address >> 8) / FLASH_WORD_SIZE) & 0x7E;

    write_xdata_memory(0xF000,  FLASHPAGE_SIZE, rambuf);
    write_xdata_memory(0xF000 + FLASHPAGE_SIZE, updProcSize, updProc);
    debug_instr_3(0x75,0xC7,0x51);
    set_pc(0xF000 + FLASHPAGE_SIZE);
    cpu_resume();
    while (!(read_status() & ST_CPU_HALTED));
}



static void read_flash_page(uint32_t address, __xdata uint8_t *outputData)
{
    read_code_memory(address & 0xFFFF, 
                     (address >> 15) & 0x03, FLASHPAGE_SIZE, outputData);
}


static void dbg_readpage(void)
{
    read_flash_page(page * 1024, rambuf);
}

static void dbg_writepage(void)
{
    uint32_t addr = page*1024;
    write_flash_page(addr);
}



uint8_t cons_getch(void)
{
    if (rxfifo_in == rxfifo_out)
        return 0;
    page = rxfifo[rxfifo_out];
    if (rxfifo_out + 1 == RXFIFO_SIZE)
        rxfifo_out = 0;
    else
        rxfifo_out++;
    return 1;
}

void cons_putc(uint8_t ch)
{
    U0DBUF = ch;
    while(!(U0CSR & U0CSR_TX_BYTE)); // wait for byte to be transmitted
    U0CSR &= ~U0CSR_TX_BYTE;         // Clear transmit byte status
}

void uart0_isr(void) __interrupt URX0_VECTOR
{
    URX0IF = 0;

// HACK we know the buffer is big enough, as client is waiting for our ACK
//    if(rxfifo_in != (( rxfifo_out - 1 + RXFIFO_SIZE) % RXFIFO_SIZE)) // not full
    {
        rxfifo[rxfifo_in] = U0DBUF;
        if (rxfifo_in + 1 == RXFIFO_SIZE)
            rxfifo_in = 0;
        else
            rxfifo_in++;
    }
}

void main(void)
{
    uint16_t i;
    uint8_t n;

    // Initialise clocks
    SLEEP &= ~SLEEP_OSC_PD;	// enable RC oscillator
    while( !(SLEEP & SLEEP_XOSC_S) );	// let oscillator stabilise

    CLKCON = CLKCON_OSC32 | CLKCON_OSC | TICKSPD_DIV_32 | CLKSPD_DIV_2;  // select internal HS RC oscillator
    while (!(CLKCON & CLKCON_OSC));

    CLKCON = CLKCON_OSC32 | TICKSPD_DIV_32 | CLKSPD_DIV_1;  // select external crystal

//    while (CLKCON & CLKCON_OSC);
//    SLEEP |= SLEEP_OSC_PD;	// Disable RC oscillator now that we have an external crystal

    rxfifo_in = rxfifo_out = 0;

	PERCFG = (PERCFG & ~PERCFG_U0CFG) | PERCFG_U1CFG;
	P0SEL |= (1<<3) | (1<<2);
	U0CSR = 0x80 | 0x40;    // UART, RX on
	U0BAUD = 34;    // 115200
	U0GCR = 13; // 115k2 baud at 13MHz, useful for coming out of sleep.  Assumes clkspd_div2 in clkcon for HSRC osc
	URX0IF = 0;	// No interrupts pending at start
	URX0IE = 1;	// Serial Rx irqs enabled in system interrupt register

    EA = 1;

    n = 0;
    while(n < sizeof(banner))
        cons_putc(banner[n++]);

    dbg_init();

    i = 0;
    while(!cons_getch())
    {
        if (i-- == 0)
        {
            cons_putc('P');
        }
    }

    while(1)
    {
        if (cons_getch())
        {
            switch(page)
            {
                case 'e':
                    while(!cons_getch());
                    dbg_mass_erase();
                    goto ack;
                break;

                case 'p':
                    while(!cons_getch());
                    dbg_writepage();
                    goto ack;
                break;

                case 'r':
                    while(!cons_getch());
                    dbg_readpage();
                    for (i=0;i<1024;i++)
                        cons_putc(rambuf[i]);
                    goto ack;
                break;
            
                case 'l':
                    i = 0;
                    while(i<1024)
                    {
                        while(!cons_getch());
                        rambuf[i] = page;
                        i++;
                    }
                    goto ack;
                break;

                case 'j':
                    WDCTL = (WDCTL & ~WDCTL_INT) | WDCTL_INT_SEC_1; // watchdog on LS RCOSC, ~1s
                    WDCTL = (WDCTL & ~WDCTL_MODE) | WDCTL_EN;   // start
                    while(1);   // reset
                break;

                ack:
                    cons_putc(0);
            }
        }
    }
}



