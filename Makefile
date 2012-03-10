all:
	make -C boot
	make -C cctl-prog

clean:
	make -C boot clean
	make -C cctl-prog clean

