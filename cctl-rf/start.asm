; RF config is interleaved witht the interupt vectors to save space.
; To select crystal frequency set 1 or 0
CRYSTAL_26_MHZ=1
; Remember to change in main.c also.
;
	.globl __start__stack
;--------------------------------------------------------
; Stack segment in internal ram
;--------------------------------------------------------
	.area	SSEG	(DATA)
__start__stack:
	.ds	1

;--------------------------------------------------------
; interrupt vector 
;--------------------------------------------------------
	.area VECTOR    (CODE)
	.globl __interrupt_vect
__interrupt_vect:
        ljmp    __sdcc_gsinit_startup
	
    	jnb psw.1, 00001$  ; RFTXRX if F1=1 call bootload isr
	ljmp _rftxrx_isr ; 
	.ds	2
	ljmp #(0x400+0x0B) ; ADC
00001$:
	ljmp #(0x400+0x03) ; User code RFTXRX usr
	.ds	2
	ljmp #(0x400+0x13) ; URX0
					.db	0xD3 ; SYNC1
					.db	0x91 ; SYNC0
					.db	0xFF ; PKTLEN
					.db	0x05 ; PKTCTRL1
					.db	0x45 ; PKTCTRL0
	ljmp #(0x400+0x1B) ; URX1
					.db	0xFE ; ADDR
					.db	0x00 ; CHANNR
					.db	0x0C ; FSCTRL1
					.db	0x00 ; FSCTRL0
				.if CRYSTAL_26_MHZ
					.db	0x21 ; FREQ2
				.else
					.db	0x24 ; FREQ2
				.endif
	ljmp #(0x400+0x23) ; ENC
				.if CRYSTAL_26_MHZ
					.db	0x65 ; FREQ1
					.db	0x6A ; FREQ0
					.db	0x2D ; MDMCFG4
					.db	0x3B ; MDMCFG3
				.else
					.db	0x2D ; FREQ1
					.db	0xDD ; FREQ0
					.db	0x1D ; MDMCFG4
					.db	0x55 ; MDMCFG3
				.endif
					.db	0x13 ; MDMCFG2
	ljmp #(0x400+0x2B) ; ST
					.db	0x22 ; MDMCFG1
					.db	0xF8 ; MDMCFG0
					.db	0x62 ; DEVIATN
					.db	0x07 ; MCSM2
					.db	0x30 ; MCSM1
	ljmp #(0x400+0x33) ; P2INT
					.db	0x18 ; MCSM0
					.db	0x1D ; FOCCFG
					.db	0x1C ; BSCFG
					.db	0xC7 ; AGCCTRL2
					.db	0x00 ; AGCCTRL1
	ljmp #(0x400+0x3B) ; UTX0
					.db	0xB0 ; AGCCTRL0
					.db	0xB6 ; FREND1
					.db	0x10 ; FREND0
					.db	0xEA ; FSCAL3
					.db	0x2A ; FSCAL2
	ljmp #(0x400+0x43) ; DMA
					.db	0x00 ; FSCAL1
					.db	0x1F ; FSCAL0
	.ds	3
	ljmp #(0x400+0x4B) ; T1
	.ds	5
	ljmp #(0x400+0x53) ; T2
	.ds	5
	ljmp #(0x400+0x5B) ; T3
	.ds	5
	ljmp #(0x400+0x63) ; T4
	.ds	5
	ljmp #(0x400+0x6B) ; P0INT
	.ds	5
	ljmp #(0x400+0x73) ; UTX1
	.ds	5
	ljmp #(0x400+0x7B) ; P1INT
	.ds	5
	ljmp #(0x400+0x83) ; RF
	.ds	5
	ljmp #(0x400+0x8B) ; WDT
	

;--------------------------------------------------------
; external initialized ram data
;--------------------------------------------------------
	.area XISEG   (XDATA)
	.area HOME    (CODE)
	.area GSINIT0 (CODE)
	.area GSINIT1 (CODE)
	.area GSINIT2 (CODE)
	.area GSINIT3 (CODE)
	.area GSINIT4 (CODE)
	.area GSINIT5 (CODE)
	.area GSINIT  (CODE)
	.area GSFINAL (CODE)
	.area CSEG    (CODE)

;--------------------------------------------------------
; global & static initialisations
;--------------------------------------------------------
	
	.area GSINIT  (CODE)
	.globl __sdcc_gsinit_startup
	.globl __sdcc_program_startup
	.globl __start__stack
	.globl __mcs51_genXINIT
	.globl __mcs51_genXRAMCLEAR
	.globl __mcs51_genRAMCLEAR
	.area GSFINAL (CODE)
        ljmp    __sdcc_program_startup
;--------------------------------------------------------
; Home
;--------------------------------------------------------
        .area HOME    (CODE)
        .area HOME    (CODE)
__sdcc_program_startup:
        acall   _bootloader_main
        ;       return from main will lock up
