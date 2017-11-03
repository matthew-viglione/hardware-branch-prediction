/* Copyright (c) 2000 James Archibald, Brigham Young University */

/* These are functions defined in mem.c that can be called from other
 * files */ 

int range_ok(unsigned int);
void clear_mem(void);
void init_mem(void);
unsigned char read_byte(unsigned int);
void write_byte(unsigned char, unsigned int);
unsigned short read_half(unsigned int);
void write_half(unsigned short, unsigned int);
unsigned int read_word(unsigned int);
void write_word(unsigned int, unsigned int);
double read_double(unsigned int);
void write_double(double, unsigned int);
char *actual_address(unsigned int);
