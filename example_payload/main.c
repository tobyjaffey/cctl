#include "cc1110.h"

#define nop()	__asm nop __endasm;

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
 
void main(void)
{  
    // toggle P2_3
    P2DIR |= (1<<3);
    P2_3 = 0;

    while (1) 
    {
        P2_3 ^= 1;
        delay(3);
    }
}
