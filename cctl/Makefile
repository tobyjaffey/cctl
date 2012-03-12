PROG = /cygdrive/c/Program\ Files/Texas\ Instruments/SmartRF\ Tools/Flash\ Programmer/bin/SmartRFProgConsole.exe

TARGET = cctl.hex

CC = sdcc
AS = sdas8051

CFLAGS = --model-small --opt-code-size

LDFLAGS_FLASH = \
	--out-fmt-ihx \
	--code-loc 0x0000 --code-size 0x400 \
	--xram-loc 0xf000 --xram-size 0x1000 \
	--iram-size 0x100

ASFLAGS = -plosgff

ifdef DEBUG
CFLAGS += --debug
endif

SRC = main.c

ASM_SRC = start.asm

ADB = $(SRC:.c=.c.adb)
ASM = $(SRC:.c=.c.asm)
LNK = $(SRC:.c=.c.lnk)
LST = $(SRC:.c=.c.lst)
REL = $(SRC:.c=.c.rel)
RST = $(SRC:.c=.c.rst)
SYM = $(SRC:.c=.c.sym)

ASM_ADB = $(ASM_SRC:.asm=.adb)
ASM_LNK = $(ASM_SRC:.asm=.lnk)
ASM_LST = $(ASM_SRC:.asm=.lst)
ASM_REL = $(ASM_SRC:.asm=.rel)
ASM_RST = $(ASM_SRC:.asm=.rst)
ASM_SYM = $(ASM_SRC:.asm=.sym)

PROGS = $(TARGET)
PCDB = $(PROGS:.hex=.cdb)
PLNK = $(PROGS:.hex=.lnk)
PMAP = $(PROGS:.hex=.map)
PMEM = $(PROGS:.hex=.mem)
PAOM = $(PROGS:.hex=)

%.c.rel : %.c
	$(CC) -c $(CFLAGS) -o$*.c.rel $<

%.rel : %.asm
	$(AS) -c $(ASFLAGS) $<

all: $(PROGS)

$(TARGET): $(REL) $(ASM_REL) Makefile
	$(CC) $(LDFLAGS_FLASH) $(CFLAGS) -o $(TARGET) $(ASM_REL) $(REL)
	@echo Binary size `makebin -p < $(TARGET) | wc -c`


clean:
	rm -f $(ADB) $(ASM) $(LNK) $(LST) $(REL) $(RST) $(SYM)
	rm -f $(ASM_ADB) $(ASM_LNK) $(ASM_LST) $(ASM_REL) $(ASM_RST) $(ASM_SYM)
	rm -f $(PROGS) $(PCDB) $(PLNK) $(PMAP) $(PMEM) $(PAOM)

install: $(TARGET)
	$(PROG) S EPV F="$(TARGET)"
