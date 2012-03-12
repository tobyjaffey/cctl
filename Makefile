all:
	make -C cctl
	make -C cctl-prog
	make -C cchl
	make -C example_payload

clean:
	make -C cctl clean
	make -C cctl-prog clean
	make -C cchl clean
	make -C example_payload clean

