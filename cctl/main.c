/*
 * CCTL - ChipCon Tiny Loader
 * A 1KB serial bootloader for the CC1110/CC1111
 * Joby Taffey (c) 2012 <jrt-cctl@hodgepig.org>
 *
 * Derived from CC Bootloader
 * Fergus Noble (c) 2011
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

// Flash write timer value:
// FWT = 21000 * FCLK / (16 * 10^9)
// For FCLK = 24MHz, FWT = 0x1F
// For FCLK = 26MHz, FWT = 0x11
#define FLASH_FWT 0x11
// Address of flash controller data register
#define FLASH_FWDATA_ADDR 0xDFAF


#define RXFIFO_ELEMENTS 2048
#define RXFIFO_SIZE (RXFIFO_ELEMENTS - 1)
static __xdata uint8_t rxfifo[RXFIFO_SIZE];
static uint8_t rxfifo_in;
static uint8_t rxfifo_out;
static __xdata struct cc_dma_channel dma0_config;
static const __code uint8_t * __at (0x0000) flashp;
__xdata uint8_t rambuf[1024];
uint8_t page;

static const char banner[] = {'\r', '\n', 'C', 'C', 'T', 'L', '\r', '\n'};

struct cc_dma_channel
{
    uint8_t src_high;
    uint8_t src_low;
    uint8_t dst_high;
    uint8_t dst_low;
    uint8_t len_high;
    uint8_t len_low;
    uint8_t cfg0;
    uint8_t cfg1;
};

#define DMA_CFG0_TRIGGER_FLASH     18
#define DMA_CFG1_SRCINC_1      (1 << 6)
#define DMA_CFG1_DESTINC_0     (0 << 4)
#define DMA_CFG1_PRIORITY_HIGH     (2 << 0)
#define DMAARM_DMAARM0         (1 << 0)

#define DMA_LEN_HIGH_VLEN_MASK     (7 << 5)
#define DMA_LEN_HIGH_VLEN_LEN      (0 << 5)
#define DMA_LEN_HIGH_VLEN_PLUS_1   (1 << 5)
#define DMA_LEN_HIGH_VLEN      (2 << 5)
#define DMA_LEN_HIGH_VLEN_PLUS_2   (3 << 5)
#define DMA_LEN_HIGH_VLEN_PLUS_3   (4 << 5)
#define DMA_LEN_HIGH_MASK      (0x1f)

#define DMA_CFG0_WORDSIZE_8        (0 << 7)
#define DMA_CFG0_WORDSIZE_16       (1 << 7)
#define DMA_CFG0_TMODE_MASK        (3 << 5)
#define DMA_CFG0_TMODE_SINGLE      (0 << 5)
#define DMA_CFG0_TMODE_BLOCK       (1 << 5)
#define DMA_CFG0_TMODE_REPEATED_SINGLE (2 << 5)
#define DMA_CFG0_TMODE_REPEATED_BLOCK  (3 << 5)


void flash_erase_page(void)
{
  while (FCTL & FCTL_BUSY);
  
  FWT = FLASH_FWT;
  FADDRH = page << 1;
  FADDRL = 0x00;

  // Erase the page that will be written to
  FCTL |=  FCTL_ERASE;
  __asm nop __endasm;
  
  // Wait for the erase operation to complete
  while (FCTL & FCTL_BUSY) {}
}

void flash_write_trigger(void)
{
  // Enable flash write. Generates a DMA trigger. Must be aligned on a 2-byte
  // boundary and is therefore implemented in assembly.
  
  // p.s. if this looks a little crazy its because it is, sdcc doesn't currently
  // support explicitly specifying code alignment which would make this easier
  
  __asm
    .globl flash_write_trigger_instruction
    .globl flash_write_trigger_done
    
    ; Put our trigger instruction in the HOME segment (shared with some startup code)
    ; where it wont move around too much
    .area HOME (CODE)
    ; Comment or uncomment these lines to adjust if you change the start.asm code
    nop               ; Padding to get onto 16-bit boundary
  flash_write_trigger_instruction:
    orl _FCTL, #0x02  ; FCTL |=  FCTL_ERASE
    nop               ; Required, see datasheet.
    ljmp flash_write_trigger_done ; Jump back into our function
    
    ; Meanwhile, back in the main CSEG segment...
    .area CSEG (CODE)
    ; Jump to the trigger instruction
    ljmp flash_write_trigger_instruction
  flash_write_trigger_done:
  __endasm;
}

void flash_write(void)
{
  // Setup DMA descriptor
  dma0_config.src_high  = (((uint16_t)(__xdata uint16_t *)rambuf) >> 8) & 0x00FF;
  dma0_config.src_low   = ((uint16_t)(__xdata uint16_t *)rambuf) & 0x00FF;
  dma0_config.dst_high  = (FLASH_FWDATA_ADDR >> 8) & 0x00FF;
  dma0_config.dst_low   = FLASH_FWDATA_ADDR & 0x00FF;
  dma0_config.len_high  = DMA_LEN_HIGH_VLEN_LEN;
  dma0_config.len_high |= ((1024) >> 8) & DMA_LEN_HIGH_MASK;
  dma0_config.len_low   = (1024) & 0x00FF;
  
  dma0_config.cfg0 = \
    DMA_CFG0_WORDSIZE_8 | \
    DMA_CFG0_TMODE_SINGLE | \
    DMA_CFG0_TRIGGER_FLASH;
  
  dma0_config.cfg1 = \
    DMA_CFG1_SRCINC_1 | \
    DMA_CFG1_DESTINC_0 | \
    DMA_CFG1_PRIORITY_HIGH;
  
  // Point DMA controller at our DMA descriptor
  DMA0CFGH = ((uint16_t)&dma0_config >> 8) & 0x00FF;
  DMA0CFGL = (uint16_t)&dma0_config & 0x00FF;

  // Waiting for the flash controller to be ready
  while (FCTL & FCTL_BUSY);

  // Configure the flash controller
  FWT = FLASH_FWT;
  FADDRH = (page << 1) & 0x3F;
  //FADDRL = 0;//(page << 9) & 0xFF;    // reset value is 0x00

  // Arm the DMA channel, so that a DMA trigger will initiate DMA writing
  DMAARM |= DMAARM_DMAARM0;

  // Enable flash write - triggers the DMA transfer
  flash_write_trigger();
  
  // Wait for DMA transfer to complete
  while (!(DMAIRQ & DMAIRQ_DMAIF0));

  // Wait until flash controller not busy
  while (FCTL & (FCTL_BUSY | FCTL_SWBSY));
  
  // By now, the transfer is completed, so the transfer count is reached.
  // The DMA channel 0 interrupt flag is then set, so we clear it here.
  DMAIRQ &= ~DMAIRQ_DMAIF0;
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

void jump_to_user(void)
{
    if (*((__xdata uint8_t*)0x400) != 0xFF)
    {
        // Disable all interrupts
        EA = 0;
        IEN0 = IEN1 = IEN2 = 0;

        // bootloader not running
        F1 = 0;

        // Jump to user code
        __asm
        ljmp #0x400
        __endasm;
    }
}


void bootloader_main(void)
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

    F1 = 1;
    EA = 1;

    n = 0;
    while(n < sizeof(banner))
        cons_putc(banner[n++]);

    n = 8;
    i = 65535;
    while(!cons_getch() && n > 0)
    {
        if (i-- == 0)
        {
            cons_putc('B');
            n--;
        }
    }
    if (n != 0)
        goto upgrade_loop;

    jump_to_user();

upgrade_loop:
    WDCTL = (WDCTL & ~WDCTL_INT) | WDCTL_INT_SEC_1; // watchdog on LS RCOSC, ~1s
    WDCTL = (WDCTL & ~WDCTL_MODE) | WDCTL_EN;   // start

    while(1)
    {
        if (cons_getch())
        {
            WDCTL = (WDCTL & ~0xF0) | (0xA0);   // pat
            WDCTL = (WDCTL & ~0xF0) | (0x50);

            switch(page)
            {
                case 'e':
                    while(!cons_getch());
                    flash_erase_page();
                    goto ack;
                break;

                case 'p':
                    while(!cons_getch());
                    flash_write();
                    goto ack;
                break;

                case 'r':
                    while(!cons_getch());
                    for (i=page<<10;i<(page+1)<<10;i++)
                        cons_putc(flashp[i]);
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
                    jump_to_user();
                break;

                ack:
                    cons_putc(0);

            }
        }
    }
}



