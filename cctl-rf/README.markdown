ChipCon Tiny Loader Radio version
=================================
A bootloader that can replace the original cctl, in places where you don't wan to load software via serial cable. This is achieved by using the Wireless radio interface of the CC1110.

The readme only has limited information, experience in using the serial version of the cctl is recommended, see the readme of cctl.

Usage
-----

You need 2 cc1110 boards, a device that can run the cctl-prog and a way to to load the bootloaders see readme of cctl.

First board should be loaded with a bootloader and the sensemote app ChipCon Radio Loader (ccrl).

Second board should have the cctl-rf bootloader.

Connect the first board via serial to the device running the cctl-prog.

Use the cctl-prog with -w option.

Building
--------
The code has been tested with SDCC version 3.2.0

Radio Protocol
--------------
On reset the bootloader will send 18 packets containing this:

<- `0x02 0xFD 0x10`

waiting for a reponse like:

-> `0x02 0xFE 0x10`

If response is received it will enter upgrade mode.

If no packet is received, the bootloader will attempt to launch user code from 0x400.

The bootloader enables the watchdog with a 1s timeout while running. It does not engage the hardware watchdog when jumping to user code (as the watchdog cannot be disabled making it incompatible with applications which remain in deep sleep for long periods).

Once in upgrade mode, the bootloader expects to receive at least one packet per second, else it will reset using the hardware watchdog.

In upgrade mode, the following commands are available:

### Erase page

Erase a 1KB page of flash. 

-> `0x04 0xFE 0x01`, `uint8_t page` (0-31), `0x00`

<- `0x04 0xFD 0x01`, `uint8_t page` (0-31), `0x00`

### Program page

Program a 1KB page of flash from RAM buffer.

-> `0x04 0xFE 0x02`, `uint8_t page` (0-31), `0x00`

<- `0x04 0xFD 0x02`, `uint8_t page` (0-31), `0x00`

### Read segment

Read 64 byte segment from flash.

-> `0x04 0xFE 0x03`, `uint8_t page` (0-31), `uint8_t segment` (0-15)

<- `0x44 0xFD 0x03`, `uint8_t page` (0-31), `uint8_t segment` (0-15), `uint8_t data[64]`


### Load page

Load 64 byte segment to RAM buffer.

-> `0x44 0xFE 0x04`, `uint8_t page` (ignored), `uint8_t segment` (0-15), `uint8_t data[64]`

<- `0x04 0xFD 0x04`, `uint8_t page` (ignored), `uint8_t segment` (0-15) 

### Jump to user code

-> 0x02 0xFE 0x05

