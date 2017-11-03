/* Copyright (c) 2000 James Archibald, Brigham Young University */

#include <stdio.h>
#include <stdlib.h>
#include "mem.h"

#define STACK_BASE 0x7fff0000
#define CODE_BASE  0x00000000   /* was 0x00 */

#define ZZSTACKSIZE  65536	/* 64K -- 2nd stab at stack size */
#define ZZCODESIZE  8388608	/*  8M -- 3rd stab at code size */

#define TRUE 1
#define FALSE 0

extern unsigned int stack_size;
extern unsigned int stack_size_act;
extern unsigned int code_size;
extern unsigned int code_size_act;
extern unsigned char *stack_seg;
extern unsigned char *code_seg;
extern int monon, monhit;
extern unsigned int monhi, monlo;

unsigned int little_endian;

extern void run_error(char *);


/*  Memory is allocated in two chunks: user text+data (static), and
    user stack (also static in the model, but conceptually dynamic).

    User text_data is assumed to begin at address 0x0 and goes up.
    The stack is initialized to begin at 0x7fff0000 and grow down.

    Neither is automatically extended.
*/

int range_ok(unsigned int u)
{
    if (u >= CODE_BASE && u < (CODE_BASE + code_size))
	return(1);
    if (u >= ((unsigned int) STACK_BASE - stack_size_act) &&
	u < (unsigned int) STACK_BASE)
	return(1);
    return(0);
}

void clear_mem(void)
{
    unsigned int i;
    for (i = 0; i < stack_size; i++)
	stack_seg[i] = 0;
    for (i = 0; i < code_size; i++)
	code_seg[i] = 0;
}

void init_mem(void)
{				/* allocate space for memory */
    unsigned char *c;
    unsigned int l;

    stack_size_act = ZZSTACKSIZE;
    stack_seg = (unsigned char *) calloc(sizeof(unsigned char),
					 stack_size_act+1);
    if (stack_seg == NULL)
	run_error("Can't allocate memory for stack");

    code_size_act = ZZCODESIZE;
    code_seg = (unsigned char *) calloc(sizeof(unsigned char),
					code_size_act);
    if (code_seg == NULL)
	run_error("Can't allocate memory for code segment");
    stack_size = 0;		/* currently unused  */
    code_size = 0;		/* dynamic size: used to check range */

    /* determine endianess */
    l = 0x1;
    c = (unsigned char *)&l;
    if(*c)
    {
      little_endian = TRUE;
      fprintf(stderr,"Little Endian \n");
    }
    else
    {
      little_endian = FALSE;
      fprintf(stderr,"Big Endian \n");
    }
}

unsigned char read_byte(unsigned int addr)
{
    char buf[200];
    if (addr >= (unsigned int) CODE_BASE
	&& addr < ((unsigned int) (CODE_BASE + code_size)))
	return(code_seg[addr-CODE_BASE]);
    else if (addr >= ((unsigned int) (STACK_BASE - stack_size_act))
	     && addr <= (unsigned int) STACK_BASE)
	return(stack_seg[addr-(STACK_BASE-stack_size_act)]);
    else
    {
	sprintf(buf,"Illegal memory read: address [%x] out of bounds\n", addr);
	run_error(buf);
	return(0);
    }
}

void write_byte(unsigned char b, unsigned int addr)
{
    char buf[200];
    if (monon)
    {
	if (addr >= monlo && addr <= monhi)
	    monhit = addr;
    }
    if (addr >= (unsigned int) CODE_BASE
	&& addr < ((unsigned int) (CODE_BASE + code_size)))
	code_seg[addr-CODE_BASE] = b;
    else if (addr >= ((unsigned int) (STACK_BASE - stack_size_act))
	     && addr <= (unsigned int) STACK_BASE)
	stack_seg[addr-(STACK_BASE-stack_size_act)] = b;
    else
    {
	sprintf(buf,"Illegal memory write: address [%x] out of bounds\n",
		addr);
	run_error(buf);
    }
}

unsigned short read_half(unsigned int addr)
{
    char buf[200];
    unsigned short tmp;
    if (addr & 0x1)
    {
	sprintf(buf,"Illegal memory read: unaligned halfword address [%x]\n",
		addr);
	run_error(buf);
    }
    tmp = (read_byte(addr) << 8) | read_byte(addr+1);
    return tmp;
}

void write_half(unsigned short h, unsigned int addr)
{
    char buf[200];
    unsigned char b;
    if (monon)
    {
	if (addr+1 >= monlo && addr <= monhi)
	    monhit = addr;
    }
    if (addr & 0x1)
    {
	sprintf(buf,"Illegal memory read: unaligned halfword address [%x]\n",
		addr);
	run_error(buf);
    }
    b = (h >> 8) & 0xff;
    write_byte(b,addr);
    b = h & 0xff;
    write_byte(b,addr+1);
}

unsigned int read_word(unsigned int addr)
{
    char buf[200];
    unsigned int tmp;
    if (addr & 0x3)
    {
	sprintf(buf,"Illegal memory read: unaligned word address [%x]\n",
		addr);
	run_error(buf);
    }
    tmp = (read_byte(addr) << 24) | (read_byte(addr+1) << 16) |
	(read_byte(addr+2) << 8) | read_byte(addr+3);
    return tmp;
}

void write_word(unsigned int w, unsigned int addr)
{
    char buf[200];
    unsigned char b;
    if (monon)
    {
	if (addr+3 >= monlo && addr <= monhi)
	    monhit = addr;
    }
    if (addr & 0x3)
    {
	sprintf(buf,"Illegal memory read: unaligned word address [%x]\n",
		addr);
	run_error(buf);
    }
    b = (w >> 24) & 0xff;
    write_byte(b,addr);
    b = (w >> 16) & 0xff;
    write_byte(b,addr+1);
    b = (w >> 8) & 0xff;
    write_byte(b,addr+2);
    b = w & 0xff;
    write_byte(b,addr+3);
}

double read_double(unsigned int addr)
{
    char buf[200];
    unsigned int val[2];

    if (addr & 0x07)
    {
	sprintf(buf,"Illegal memory read: unaligned double address [%x]\n",
		addr);
	run_error(buf);
    }
    if (little_endian)
    {
	val[1] = read_word(addr);
	val[0] = read_word(addr+4);
	return(* (double *) &val[0]);
    }
    else
    {
	val[0] = read_word(addr);
	val[1] = read_word(addr+4);
	return(* (double *) &val[0]);
    }
}

void write_double(double d, unsigned int addr)
{
    char buf[200];
    unsigned int *p, tmp;

    if (monon)
    {
	if (addr+7 >= monlo && addr <= monhi)
	    monhit = addr;
    }
    if (addr & 0x07)
    {
	sprintf(buf,"Illegal memory write: unaligned double address [%x]\n",
		addr);
	run_error(buf);
    }
    p = (unsigned int *) &d;
    if (little_endian)
    {
	write_word(p[1], addr);
	write_word(p[0], addr+4);
    }
    else
    {
	write_word(p[0], addr);
	write_word(p[1], addr+4);
    }
}

char *actual_address(unsigned int addr)
{
    /* the addr parameter is an address in the DLX address space and
       (typically) represents a stack entry that is a pointer to a
       string (passed as an argument to a function, for example).  In
       order to make a call to the actual system function with a
       pointer to the actual string, I need to get the actual address
       of where the string resides in the address space of the machine
       on which the simulator is running.  This routine calculates
       that address and returns a pointer to it. */

    char buf[200];
    if (addr >= CODE_BASE && addr <= (CODE_BASE + code_size))
	return((char *) &(code_seg[addr-CODE_BASE]));
    else if (addr >= (STACK_BASE - stack_size_act) && addr <= STACK_BASE)
	return((char *) &(stack_seg[addr-(STACK_BASE-stack_size_act)]));
    else
    {
	sprintf(buf,"Can't map memory address: address [%x] out of bounds\n",
		addr);
	run_error(buf);
	return(0);
    }
}
