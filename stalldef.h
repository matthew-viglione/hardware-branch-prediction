/* Copyright (c) 2000 James Archibald, Brigham Young University */ 

/* globals and types for parameters passed to countstalls() */

/* functional unit type */
#define INT_UNIT  0
#define LOAD_UNIT 1
#define ADD_UNIT  2
#define MUL_UNIT  3
#define DIV_UNIT  4
#define NOP_UNIT -1		/* marker for NOP for countstalls() */

/* operand specifier type */
#define NONE 0
#define INTR 1
#define FPR  2
#define DPR  3
#define FPS  4

#define NOTABRANCH     0	/* used for global branch flags */
#define BRANCHTAKEN    1
#define BRANCHNOTTAKEN 2

