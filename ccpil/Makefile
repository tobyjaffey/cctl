CC = gcc
CFLAGS += -Wall -Wextra -O2 -I.
TARGET = ccpil

all: $(TARGET)

OBJS = ccpil.o dbg.o hex.o bcm2835.o

$(TARGET): $(OBJS)

clean:
	rm -f $(TARGET) $(OBJS)

