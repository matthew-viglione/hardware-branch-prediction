/* Copyright (c) 2000 James Archibald, Brigham Young University */ 

#define UREG(x)  reg_file[x]	/* unsigned GPR */
#define SREG(x)  *((int *) &(reg_file[x]))  /* signed GPR */

/* doubles accessed with 0-30, even only, so must map to 0-15 */
#define DREG(x)  fp_reg[x>>1]
#define FREG(x)  ((float *) &(fp_reg[0]))[x] 
#define SFREG(x) ((int *)   &(fp_reg[0]))[x] 
#define UFREG(x) ((unsigned int *) &(fp_reg[0]))[x] 

