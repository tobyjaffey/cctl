Raspberry Pi ChipCon Loader
===========================

A bit banging hardware flasher for CC1110 to run on Raspberry Pi.

Wire:

* BCM2835 P1.11/GP0 to CC1110 P2.1/DD (XRF pin 17)
* BCM2835 P1.12/GP1 to CC1110 P2.2/DC (XRF pin 18)
* BCM2835 P1.13/GP2 to CC1110 Reset (XRF pin 5)

I use a Ciseco "Slice of Pi" prototyping board for this, http://openmicros.org/index.php/articles/88-ciseco-product-documentation/160-slice-of-pi-for-the-raspberry-pi

This board also connects the CC1110's serial port to the Raspberry Pi.
To use /dev/ttyAMA0 from the Pi, you will need to disable the serial console - http://www.irrational.net/2012/04/19/using-the-raspberry-pis-serial-port/

