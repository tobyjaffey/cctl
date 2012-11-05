/*
 * CCTL-RF - ChipCon Tiny Loader Radio Version
 * A 1KB RF bootloader for the CC1110/CC1111
 *
 * Kristoffer Larsen (c) 2012 <kri@kri.dk>
 *
 * Based on Serial bootload from 
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

// Select crystal frequency
// Remember to change in start.asm also.
#define CRYSTAL_26_MHZ 


//#define CONSOLE_DEBUG 

#include <stdint.h>
#include <cc1110.h>
#include "cc1110-ext.h"

#define RADIOBUF_MAX 256

// Flash write timer value:
// FWT = 21000 * FCLK / (16 * 10^9)
// For FCLK = 24MHz, FWT = 0x1F
// For FCLK = 26MHz, FWT = 0x11
#ifdef CRYSTAL_26_MHZ
#define FLASH_FWT 0x1F
#endif
#ifdef CRYSTAL_24_MHZ
#define FLASH_FWT 0x11
#endif
// Address of flash controller data register
#define FLASH_FWDATA_ADDR 0xDFAF



static __xdata struct cc_dma_channel dma0_config;

static __xdata uint8_t radiobuf[RADIOBUF_MAX];
static uint8_t radiobuf_index;
#ifdef CONSOLE_DEBUG
static __xdata uint8_t * __at (0x0000) flashp;
#endif

__xdata uint8_t rambuf[1024];

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
  FADDRH = radiobuf[3] << 1;
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
  FADDRH = (radiobuf[3] << 1) & 0x3F;
  //FADDRL = 0;//(radiobuf[3] << 9) & 0xFF;    // reset value is 0x00

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

#ifdef CONSOLE_DEBUG
void cons_putc(uint8_t ch)
{ U0DBUF = ch;
    while(!(U0CSR & U0CSR_TX_BYTE)); // wait for byte to be transmitted
    U0CSR &= ~U0CSR_TX_BYTE;         // Clear transmit byte status
}

char nibble_to_char(uint8_t nibble)
{
	if (nibble < 0xA)
		return nibble + '0';
	return nibble - 0xA + 'A';
}

void cons_puthex8(uint8_t h)
{
	cons_putc(nibble_to_char((h & 0xF0)>>4));
	cons_putc(nibble_to_char(h & 0x0F));
}

void cons_puthex16(uint16_t h)
{
	cons_putc(nibble_to_char((h & 0xF000)>>12));
	cons_putc(nibble_to_char((h & 0x0F00)>>8));
	cons_putc(nibble_to_char((h & 0x00F0)>>4));
	cons_putc(nibble_to_char(h & 0x000F));
}

void cons_puts(const char *s)
{
	while(0 != *s)
		cons_putc((uint8_t)(*s++));
}
#endif

void rftxrx_isr(void) __interrupt RFTXRX_VECTOR {
	switch (MARCSTATE) {
		case MARC_STATE_RX:
			radiobuf[radiobuf_index] = RFD;
			goto inc;  // stupid way but saves space.
			break;
		case MARC_STATE_TX:
			RFD = radiobuf[radiobuf_index];
inc:
			radiobuf_index++;
			break;
	}
}
void tx_pkt(void) {
#ifdef CONSOLE_DEBUG
	cons_puts("Start TX\r\n");
#endif

	T3CTL=0xDC; // User Timer3 to delay TX, without the delay the programer does not have time to switch from TX to RX.
	T3OVFIF=0;
	while (!T3OVFIF);
	T3CTL=0;
	radiobuf[1]=0xFD; // Set dev once to save space
	radiobuf_index=0;
        RFST = RFST_STX;      /* enter TX */
	while(MARCSTATE != MARC_STATE_TX);
        //while (!(RFIF & RFIF_IRQ_DONE));
	while(MARCSTATE != MARC_STATE_IDLE);
	RFIF =0;
#ifdef CONSOLE_DEBUG
	cons_puts("Done TX\r\n");
#endif
}
uint8_t rx_pkt(void) {
	if (RFIF & RFIF_IRQ_DONE) {
#ifdef CONSOLE_DEBUG
		cons_puts("\r\nR");
		cons_puthex8(LQI);
		cons_puthex8(RFIF);
		cons_puthex8(radiobuf[0]);
		cons_puthex8(radiobuf[1]);
		cons_puthex8(radiobuf[2]);
		cons_puthex8(radiobuf[3]);
		cons_puthex8(radiobuf[4]);
		cons_puthex8(radiobuf[5]);
		cons_puthex8(radiobuf[6]);
		cons_puthex8(radiobuf[7]);
		cons_puts("\r\n");
#endif
		RFIF =0;
		if (LQI & 0x80) { // packet has valid CRC
#ifdef CONSOLE_DEBUG
			cons_puts("Valid CRC\r\n");
#endif
			while(MARCSTATE != MARC_STATE_IDLE);
			return 1;
		} else {
			radiobuf_index=0;
		}
	}
	if (MARCSTATE != MARC_STATE_RX) {
		radiobuf_index=0;
		RFST = RFST_SRX; // Set RX mode
		while (MARCSTATE != MARC_STATE_RX);
#ifdef CONSOLE_DEBUG
		cons_puts("Start RX\r\n");
#endif
	}
	return 0;
}

void radio_init(void) {
	uint8_t a,b;
	uint16_t p;
	uint8_t conf;
        p=0xDF00; // SYNC1 position
	b=0;
#ifdef CONSOLE_DEBUG
	cons_puts("\r\nRadio Init\r\n");
#endif
	for (a=22;a<72;a++) {
#ifdef CONSOLE_DEBUG
		//cons_puthex8(a);
		//cons_puts(" ");
		//cons_puthex16(p);
		//cons_puts(" ");
		//cons_puthex8(flashp[a]);
#endif

		conf=*(__xdata uint8_t*)a;
		*(__xdata uint8_t*)p=conf;
#ifdef CONSOLE_DEBUG
		//cons_puts(" ");
		cons_puthex8(flashp[p]);
		cons_puthex8(conf);
		cons_puts("\r\n");
#endif
		b++;
		p++;
		if (b>4) {
			b=0;
			a=a+3;
		}
	}
		

        /* 250kbaud @ 868MHz from RF Studio 7 */

	/*
        SYNC1      =     0xD3;       // Sync Word, High Byte
        SYNC0      =     0x91;       // Sync Word, Low Byte
        PKTLEN     =     0x50;       // Packet Length
        PKTCTRL1   =     0x05;       // Packet Automation Control , Only accept ADDR filter.
        PKTCTRL0   =     0x05;       // Packet Automation Control
        ADDR       =     0xFE;       // Device Address
        CHANNR     =     0x00;       // Channel Number
        FSCTRL1    =     0x0C;       // Frequency Synthesizer Control
        FSCTRL0    =     0x00;       // Frequency Synthesizer Control
#ifdef CRYSTAL_26_MHZ
        FREQ2      =     0x21;       // Frequency Control Word, High Byte
        FREQ1      =     0x65;       // Frequency Control Word, Middle Byte
        FREQ0      =     0x6A;       // Frequency Control Word, Low Byte
#endif
#ifdef CRYSTAL_24_MHZ
        FREQ2      =     0x24;       // Frequency Control Word, High Byte
        FREQ1      =     0x2D;       // Frequency Control Word, Middle Byte
        FREQ0      =     0xDD;       // Frequency Control Word, Low Byte
#endif
#ifdef CRYSTAL_26_MHZ
        MDMCFG4    =     0x2D;       // Modem configuration
        MDMCFG3    =     0x3B;       // Modem Configuration
#endif
#ifdef CRYSTAL_24_MHZ
        MDMCFG4    =     0x1D;       // Modem configuration
        MDMCFG3    =     0x55;       // Modem Configuration
#endif
        MDMCFG2    =     0x13;       // Modem Configuration

        MDMCFG1    =     0x22;       // Modem Configuration
        MDMCFG0    =     0xF8;       // Modem Configuration
        DEVIATN    =     0x62;       // Modem Deviation Setting
        MCSM2      =     0x07;       // Main Radio Control State Machine Configuration
        MCSM1      =     0x30;       // Main Radio Control State Machine Configuration
        MCSM0      =     0x18;       // Main Radio Control State Machine Configuration
        FOCCFG     =     0x1D;       // Frequency Offset Compensation Configuration
        BSCFG      =     0x1C;       // Bit Synchronization Configuration
        AGCCTRL2   =     0xC7;       // AGC Control
        AGCCTRL1   =     0x00;       // AGC Control
        AGCCTRL0   =     0xB0;       // AGC Control
        FREND1     =     0xB6;       // Front End RX Configuration
        FREND0     =     0x10;       // Front End TX Configuration
        FSCAL3     =     0xEA;       // Frequency Synthesizer Calibration
        FSCAL2     =     0x2A;       // Frequency Synthesizer Calibration
        FSCAL1     =     0x00;       // Frequency Synthesizer Calibration
        FSCAL0     =     0x1F;       // Frequency Synthesizer Calibration
	*/
        //TEST2      =     0x88;       // Various Test Settings
        TEST1      =     0x31;       // Various Test Settings
        TEST0      =     0x09;       // Various Test Settings
        //PA_TABLE7  =     0x00;       // PA Power Setting 7
        //PA_TABLE6  =     0x00;       // PA Power Setting 6
        //PA_TABLE5  =     0x00;       // PA Power Setting 5
        //PA_TABLE4  =     0x00;       // PA Power Setting 4
        //PA_TABLE3  =     0x00;       // PA Power Setting 3
        //PA_TABLE2  =     0x00;       // PA Power Setting 2
        //PA_TABLE1  =     0x00;       // PA Power Setting 1
        PA_TABLE0  =     0x50;       // PA Power Setting 0
        //IOCFG2     =     0x00;       // Radio Test Signal Configuration (P1_7)
        //IOCFG1     =     0x00;       // Radio Test Signal Configuration (P1_6)
        //IOCFG0     =     0x00;       // Radio Test Signal Configuration (P1_5)
        //FREQEST    =     0x00;       // Frequency Offset Estimate from Demodulator
        //LQI        =     0x00;       // Demodulator Estimate for Link Quality
	//RSSI       =     0x80;       // Received Signal Strength Indication
        //MARCSTATE  =     0x01;       // Main Radio Control State Machine State
        //PKTSTATUS  =     0x00;       // Packet Status
        //VCO_VC_DAC =     0x94;       // Current Setting from PLL Calibration Module

#ifdef TX_HIGHPOWER_ENABLED
        PA_TABLE0  = 0xC2;      /* turn power up to 10dBm */
#endif
#if 0
        IEN2 |= IEN2_RFIE;
        RFIM |= RFIF_IRQ_DONE   | RFIF_IRQ_TXUNF  | RFIF_IRQ_RXOVF  | RFIF_IRQ_SFD    | RFIF_IRQ_TIMEOUT;
#endif
        radiobuf_index = RFIF = 0;
	RFTXRXIF = 0;       /* enable interrupts */
        RFTXRXIE = 1;
        RFST = RFST_SIDLE;      /* enter idle */
	while(MARCSTATE != MARC_STATE_IDLE);
}



void bootloader_main(void) {
	uint8_t n;
	uint16_t i;

	// Initialise clocks
	SLEEP &= ~SLEEP_OSC_PD;	// enable RC oscillator
	while( !(SLEEP & SLEEP_XOSC_S) );	// let oscillator stabilise

	CLKCON = CLKCON_OSC32 | CLKCON_OSC | TICKSPD_DIV_32 | CLKSPD_DIV_2;  // select internal HS RC oscillator
	while (!(CLKCON & CLKCON_OSC));

	CLKCON = CLKCON_OSC32 | TICKSPD_DIV_32 | CLKSPD_DIV_1;  // select external crystal

	while (CLKCON & CLKCON_OSC);
	SLEEP |= SLEEP_OSC_PD;	// Disable RC oscillator now that we have an external crystal

#ifdef CONSOLE_DEBUG
	PERCFG = (PERCFG & ~PERCFG_U0CFG) | PERCFG_U1CFG;
	P0SEL |= (1<<3) | (1<<2);
	U0CSR = 0x80 | 0x40;    // UART, RX on
	U0BAUD = 34;    // 115200
	U0GCR = 13; // 115k2 baud at 13MHz, useful for coming out of sleep.  Assumes clkspd_div2 in clkcon for HSRC osc
#endif

	radio_init();

	F1 = 1;
	EA = 1;

	
#ifdef CONSOLE_DEBUG
	cons_puts("\r\nBOOT\r\n");
#endif
	n = 20;
	i = 65535;
	while(n > 2) {
		if (rx_pkt()) n=0;
		if (i-- == 0) {
			radiobuf[0] = 2;
			radiobuf[2] = 0x10;
			tx_pkt();
#ifdef CONSOLE_DEBUG
			cons_putc('W');
#endif
			n--;
		}
	}
	if (n == 0)
		goto upgrade_loop;

	jump_to_user();

upgrade_loop:
	WDCTL = 0x08; //Enable watchdog with 1s timeout

#ifdef CONSOLE_DEBUG
	cons_puts("Starting Upgrade loop\r\n");
#endif

	while(1) {
		if (rx_pkt()) {
			WDCTL = 0xA8; WDCTL = 0x58;
			switch (radiobuf[2]) {
				case 0:
					goto ack;
				case 1: //erase page
					flash_erase_page();
					goto ack;
					break;
				case 2: //program page from rambuffer
					flash_write();
					goto ack;
					break;
				case 3: //read page segment from flash
					radiobuf[0]=68;
#ifdef CONSOLE_DEBUG
					cons_puts("Read\r\n");
#endif
					i=(radiobuf[3]<<10)+(radiobuf[4]<<6);
		   			for (n=5;n<69;n++) {
#ifdef CONSOLE_DEBUG
						cons_puthex8(flashp[i]);
#endif
						radiobuf[n]=*(__xdata uint8_t*)i;
						i++;
					}
#ifdef CONSOLE_DEBUG
					cons_puts("\r\n");
#endif
					tx_pkt();
					break;
				case 4:
#ifdef CONSOLE_DEBUG
					cons_puts("Load Segment to ram\r\n");
#endif
					i=radiobuf[4]<<6;
		   			for (n=5;n<69;n++) {
						rambuf[i]=radiobuf[n];
						i++;
					}
					goto ack;
					break;
				case 5:
					jump_to_user();
				ack:
					radiobuf[0] = 4;
					tx_pkt();
			}
		}
	}
}



