/* Copyright (c) 2000 James Archibald, Brigham Young University */ 
/* Modifications copyright (c) 2006 David A. Penry, Brigham Young University */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#define MAINSEG		/* get variable declarations from dlxdef.h */

#include "ops.h"		/* define op codes */
#include "dlxdef.h"
#include "dlx.h"		/* macros to access registers */
#include "stalldef.h"		/* define #define parameters for
				   addstalls() calls */

#include "mem.h"    /* prototypes for functions in mem.c */
#include "clib.h"   /* prototypes for functions in clib.c */

#define PRINT_INSTR() if (verbose||in_execution) out_instr()

#define MAX_ARGS 10

#define CMD_ARG 1
#define NUM_ARG 2
#define ID_ARG  3

/* macros to get at IR fields */
#define REGa   ((ir & 0x03E00000) >> 21)
#define REGb   ((ir & 0x001F0000) >> 16)
#define REGc   ((ir & 0x0000F800) >> 11)
#define UIMMED (ir & 0x0000FFFF)
#define SIMMED ((int) (UIMMED | ((ir&0x00008000)?0xFFFF0000:0x0)))
#define OFFSET ((ir & 0x03FFFFFF)|((ir&0x02000000)?0xFC000000:0x0))
#define MEMADD ((unsigned) (UREG(REGa) + SIMMED))

#define NUMDLXINST  (MAIN_MAX - 3 + SPEC_MAX + FPOPS_MAX)

#define PCOUNT   0		/* parameters for out_istats() */
#define PPERCENT 1

#define TAKEN   0		/* parameters for do_br_stats() */
#define UNTAKEN 1

extern void fatalerrormsg(void);
extern void clearstall(void);
extern void addstalls(int, int, int, int, int, int, int);
extern void ppressed(void);
extern void tpressed(void);
extern void zpressed(void);
extern void print_options(FILE *);
extern int handle_option(char *);
extern void init_latency(void);

unsigned char *stack_seg;	/* pointer to stack segment */
unsigned char *code_seg;	/* pointer to code segment */
unsigned int stack_size_act;	/* allocated size of stack segment */
unsigned int stack_size;	/* stack size (dynamic) */
unsigned int code_size_act;	/* allocated size of code segment */
unsigned int code_size;		/* size of current binary loaded */

unsigned int reg_file[32];	/* data structure for GPR */

/* was originally float -- changed to align doubles correctly on other
 platforms that simulator has been ported to. */
double fp_reg[16];		/* data structure for FPR */

unsigned int ir;		/* instruction register */
int fp_status;			/* fp status register  */
int pc;				/* global for errors + debugging */
int nextpc;			/* global (for delayed branch) */
int mainpc;		        /* address of _main after load */
int newpc;			/* BTA on taken branches */
int in_execution;		/* global flag if single stepping */
int verbose;			/* flag for verbose printf's */
int brkpt_cnt;  		/* count of breakpoints used */
int brkpt[32];			/* breakpoint values */
int monon;			/* on/off flag for address monitor */
int monhit;			/* flag to indicate range access */
unsigned int monhi;		/* high address for monitor */
unsigned int monlo;		/* low address for monitor */

int i_count;			/* instruction count */
int b_count;			/* branch count */
int b_count_f;			/* forward branches */
int b_count_f_u;		/* forward, untaken */
int b_count_f_t;		/* forward, taken */
int b_count_b;			/* backward branches */
int b_count_b_u;		/* backward, untaken */
int b_count_b_t;		/* backward, taken */
int b_count_d_f;		/* branch delay slots filled */
int b_count_d_u;		/* branch delay slots unfilled */
int j_count;			/* jump count */
int j_count_f;			/* forward jumps */
int j_count_b;			/* backward jumps */
int j_count_d_f;		/* jump delay slots filled */
int j_count_d_u;		/* jump delay slots unfilled */

unsigned int rfile_init[32];	/* integer register initializers */

struct mapper *sort_hist[NUMDLXINST]; /* for sorted output */

int nop_count;			/* count of nops executed */

char verbosestr[500];		/* string for stall output in verbose
				   mode; made very long to avoid
				   overflow */

/* values branch_flag can take: NOTABRANCH, BRANCHTAKEN,
   BRANCHNOTTAKEN */
int branch_flag;		/* value set in dlx.c */
unsigned int ldstaddr;

#define PROGNAMELEN 20
char progname[PROGNAMELEN];	/* stores name of prog from cmd line
				 * */
void run_error(char *message)
{
    fprintf(stderr, "%s\n Aborting\n", message);
    fprintf(stderr, "Current PC: %d\n", pc);
    fatalerrormsg();
    exit(1);
}

void init_rfile_init(void)
{
    int i;
    for (i = 0; i < 32; i++)
	rfile_init[i] = 37;
}

void init_registers(int type)
{
    int i;
    for (i = 0; i < 32; i++)
    {
	UREG(i) = rfile_init[i];
	SFREG(i) = 0;
    }
    fp_status = 0;
    if (type == 0)		/* don't always reset pc */
	pc = mainpc;
}

void init_vars(void)
{				/* initialize internal variables */
    mainpc = -1;
    in_execution = 0;
    monon = 0;
    verbose = 0;
    brkpt_cnt = 0;
}

void clear_stats(void)
{
    int i;
    i_count = 0;
    for (i = 0; i < MAIN_MAX; i++)
	mainops[i].count = 0;
    for (i = 0; i < SPEC_MAX; i++)
	spec[i].count = 0;
    for (i = 0; i < FPOPS_MAX; i++)
	fpops[i].count = 0;
    b_count = b_count_f = b_count_f_u = b_count_f_t = b_count_b = 0;
    b_count_b_u = b_count_b_t = b_count_d_f = b_count_d_u = 0;
    j_count = j_count_f = j_count_b = j_count_d_f = j_count_d_u = 0;
    nop_count = 0;
}

void clear_most(void)
{
    init_registers(1);		/* clear all registers but pc */
    clear_stats();		/* clear all stats */
    clearstall();		/* clear stall counters */
}

void clear_all(void)
{
    in_execution = 0;		/* clear flag */
    verbose = 0;		/* turn off verbose */
    init_registers(1);		/* clear all registers but pc */
    clear_stats();		/* clear all stats */
    clearstall();		/* clear stall counters */
    clear_mem();		/* clear memory model */
    mainpc = -1;
}

struct mapper *map_op(char *s)
{   /* returns ptr to struct that matches the name of passed op code */
    int i;
    for (i = 0; i < MAIN_MAX; i++)
    {
	if (strcmp(s,mainops[i].name) == 0)
	    return(&(mainops[i]));
    }
    for (i = 0; i < SPEC_MAX; i++)
    {
	if (strcmp(s,spec[i].name) == 0)
	    return(&(spec[i]));
    }
    for (i = 0; i < FPOPS_MAX; i++)
    {
	if (strcmp(s,fpops[i].name) == 0)
	    return(&(fpops[i]));
    }
    fprintf(stderr, "Unknown op code: %s\n", s);
    exit(-1);
}

struct mapper *findmap(unsigned int id)
{  /* returns a ptr to the mapper struct that matches id on opcode & func bits */
    int opcode, func, j;
    struct mapper *found;

    opcode = (id >> 26) & 0x003f;
    found = NULL;
    if (opcode == 0)
    {
	func = id & 0x3f;
	for (j = 0; j < SPEC_MAX; j++)
	{
	    if (spec[j].func == func)
	    {
		found = &(spec[j]);
		break;
	    }
	}
    }
    else if (opcode == 1)	/* fparith? */
    {
	func = id & 0x3f;
	for (j = 0; j < FPOPS_MAX; j++)
	{
	    if (fpops[j].func == func)
	    {
		found = &(fpops[j]);
		break;
	    }
	}
    }
    else			/* assumed to be regular instr */
    {
	for (j = 2; j < MAIN_MAX; j++)
	{
	    if (mainops[j].op == opcode)
	    {
		found = &(mainops[j]);
		break;
	    }
	}
    }
    if (found == NULL)
	run_error("can't match on opcode in findmap()\n");
    return(found);
}

void init_sort_hist(void)
{
    int i,j,found;
    struct mapper *tmp, *best;

    clear_stats();
    /* initialize sort_hist[] pointers to dump opcodes in alphabetic order */

    /* step 1: mark special, pseudo ops, fparith */
    tmp = map_op("special");
    tmp->count = 1;
    tmp = map_op("fparith");
    tmp->count = 1;
    tmp = map_op("la");
    tmp->count = 1;

    /* step 2: loop finding each name in sequence, setting ptr */
    for (j = 0; j < NUMDLXINST; j++)
    {
	found = 0;
	for (i = 0; i < MAIN_MAX; i++)
	{
	    if (mainops[i].count == 0)  /* unmarked */
	    {
		if (!found)
		{
		    best = &(mainops[i]);
		    found = 1;
		}
		else if (strcmp(mainops[i].name, best->name) < 0)
		    best = &(mainops[i]);
	    }
	}
	for (i = 0; i < SPEC_MAX; i++)
	{
	    if (spec[i].count == 0)  /* unmarked */
	    {
		if (!found)
		{
		    best = &(spec[i]);
		    found = 1;
		}
		else if (strcmp(spec[i].name, best->name) < 0)
		    best = &(spec[i]);
	    }
	}
	for (i = 0; i < FPOPS_MAX; i++)
	{
	    if (fpops[i].count == 0)  /* unmarked */
	    {
		if (!found)
		{
		    best = &(fpops[i]);
		    found = 1;
		}
		else if (strcmp(fpops[i].name, best->name) < 0)
		    best = &(fpops[i]);
	    }
	}
	if (!found)
	{
	    printf("Error constructing sort_hist[]\n");
	    exit(-1);
	}
	sort_hist[j] = best;
	best->count = 1;
    }
	
    /* step 3: undo marks and return */
    clear_stats();
}

void parse_cmd(char *s, int *xargc, char *xargv[], int xargt[])
/* s is command string, xargc returns arg count, xargv[] returns arg
   strings, and xargt[] returns arg types */
{
    int i,start,state;

    i = 0;
    state = 2;
    *xargc = 0;

    while (s[i] != '\0')
    {
	switch(state)
	{
	  case 2:		/* skipping white space to next arg */
	    if (!isspace(s[i]))
	    {			/* classify next arg type */
		if (*xargc == 0) /* first is assumed to be command */
		    xargt[*xargc] = CMD_ARG;
		else if (isdigit(s[i]) || 
			 (s[i] == 'O' && (s[i+1] == 'x' || s[i+1] == 'X')))
		    xargt[*xargc] = NUM_ARG; /* number (can be hex) */
		else		
		    xargt[*xargc] = ID_ARG; /* identifier */
		state = 3;
		start = i;	/* mark beginning of next arg */
	    }
	    break;
	  case 3:		/* skip to end of arg */
	    if (isspace(s[i]))
	    {
		s[i] = '\0';
		xargv[*xargc] = s+start;
		(*xargc)++;
		state = 2;
	    }
	    break;
	  default:
	    printf("\nbad state in parse_cmd\n");
	    return;
	}
	i++;
    }
    if (state == 3)
    {
	xargv[*xargc] = s+start;
	(*xargc)++;
    }
    if (*xargc == 0)
	xargv[0] = s+i;		/* set up '\0' if empty */
/*    printf("DEBUG: in parse_cmd(): xargc is %d\n",*xargc); */
}

void out_instr(void)
{
    unsigned int rs1, rs2, rd, immed, immed26;
    int simmed, simmed26;
    char buf[100];
    struct mapper *mp;
    
    printf("<pc:%5x>  ", pc);
    mp = findmap(ir);		/* get entry matching current instruction */
    
    rs1 = (ir >> 21) & 0x1f;
    rs2 = (ir >> 16) & 0x1f;
    rd =  (ir >> 11) & 0x1f;
    immed = ir & 0xffff;
    if (immed & 0x8000)
	simmed = 0xffff0000 | immed;
    else
	simmed = immed;
    immed26 = ir & 0x03ffffff;
    if (immed26 & 0x02000000)
	simmed26 = immed26 | 0xfc000000; 
    else
	simmed26 = immed26;

    switch (mp->optype)
    {
    case DF2:
    case DREG2a:
    case FD2:
    case FREG2a:
	sprintf(buf,"%-8s f%d,f%d", mp->name, rd, rs1);
	break;
    case DREG2b:
    case FREG2b:
	sprintf(buf,"%-8s f%d,f%d", mp->name, rs1, rs2);
	break;
    case DREG3:
    case FREG3:
	sprintf(buf,"%-8s f%d,f%d,f%d", mp->name, rd, rs1, rs2);
	break;
    case FI2:
	sprintf(buf,"%-8s f%d,r%d", mp->name, rd, rs1);
	break;
    case IF2:
	sprintf(buf,"%-8s r%d,f%d", mp->name, rd, rs1);
	break;
    case IMM1:
	sprintf(buf,"%-8s #%d", mp->name, immed26);
	break;
    case IREG1:
	sprintf(buf,"%-8s r%d", mp->name, rs1);
	break;
    case LEXP16:
	sprintf(buf,"%-8s %+d", mp->name, simmed);
	break;
    case LEXP26:
	sprintf(buf,"%-8s %+d", mp->name, simmed26);
	break;
    case LOADD:
    case LOADF:
	sprintf(buf,"%-8s f%d,%+d(r%d)", mp->name, rs2, simmed, rs1);
	break;
    case LOADI:
	sprintf(buf,"%-8s r%d,%+d(r%d)", mp->name, rs2, simmed, rs1);
	break;
    case NONEOP:
	sprintf(buf,"%-8s ", mp->name);
	break;
    case REG1IMM:
	sprintf(buf,"%-8s r%d,%d", mp->name, rs2, immed);
	break;
    case REG2IMM:
	sprintf(buf,"%-8s r%d,r%d,#%d", mp->name, rs2, rs1, simmed);
	break;
    case REG3IMM:
	sprintf(buf,"%-8s r%d,r%d,r%d", mp->name, rd, rs1, rs2);
	break;
    case REGLAB:
	sprintf(buf,"%-8s r%d,%+d", mp->name, rs1, simmed);
	break;
    case STRD:
    case STRF:
	sprintf(buf,"%-8s %+d(r%d),f%d", mp->name, simmed, rs1, rs2);
	break;
    case STRI:
	sprintf(buf,"%-8s %+d(r%d),r%d", mp->name, simmed, rs1, rs2);
	break;
    case UNIMP:
	sprintf(buf,"%-8s: unimplemented instr", mp->name);
	break;
    default:
	sprintf(buf,"BAD OPTYPE IN DOREST()");
	break;
    }
    printf("%-22s ",buf);
    if (verbosestr[0] != '\0')
	printf("%-44s\n",verbosestr);
    else
	printf("\n");
}

int overflow(int result, int src1, int src2)
{
    if ((src1 < 0 && src2 < 0 && result > 0) || 
	(src1 > 0 && src2 > 0 && result < 0))
    {
	printf("Overflow \n");
	return(1);
    }
    else
	return(0);
}

int izero(int num)
/* num is fp register number */
{
    if (SFREG(num) == 0)
    {
	printf("Divide by zero error\n");
	return(1);
    }
    else
	return(0);
}

int fzero(int num)
/* num is fp register number */
{
    if (FREG(num) == 0.0)
    {
	printf("Divide by zero error\n");
	return(1);
    }
    else
	return(0);
}

int dzero(int num)
/* num is dreg register number */
{
    if (DREG(num) == 0.0)
    {
	printf("Divide by zero error\n");
	return(1);
    }
    else
	return(0);
}

void do_j_stats(unsigned int target)
{
    unsigned int tmp;
    j_count++;
    /* forward or backward ? */
    if (target > pc)		/* forward */
	j_count_f++;
    else
	j_count_b++;
    tmp = read_word(pc+4);
    if (tmp == 0x00000000)	/* nop */
	j_count_d_u++;
    else
	j_count_d_f++;
}

void do_br_stats(int type)
{
    unsigned int tmp;
    b_count++;
    /* forward or backward ? */
    if (SIMMED & 0x80000000)	/* backward */
    {
	b_count_b++;
	if (type == TAKEN)
	    b_count_b_t++;
	else
	    b_count_b_u++;
    }
    else			/* forward */
    {
	b_count_f++;
	if (type == TAKEN)
	    b_count_f_t++;
	else
	    b_count_f_u++;
    }
    tmp = read_word(pc+4);
    if (tmp == 0x00000000)	/* nop */
	b_count_d_u++;
    else
	b_count_d_f++;
}

int handle_trap(int num)
{   /* return 0 if okay to continue, return 1 to end execution */

    PRINT_INSTR();
    switch (num)
    {
    case 0:		/* exit */
	do_exit();
	printf("Trap #0 received: exiting \n");
	return(1);
    case 1:
	do_fopen();
	break;
    case 2:
	do_fclose();
	break;
    case 3:
	do_fread();
	break;
    case 4:
	do_fwrite();
	break;
    case 5:
	do_printf();
	break;
    case 6:
	do_scanf();
	break;
    case 7:
	do_fprintf();
	break;
    case 8:
	do_fscanf();
	break;
    case 9:
	do_feof();
	break;
    case 10:
	do_malloc();
	break;
    case 11:
	do_sprintf();
	break;
    default:
	printf("Trap #%d received: undefined [PC = %x]\n",num,pc);
	return(1);
    }
    return(0);
}

void execute(int steps)
{
    unsigned int tmp,tmp2;
    int itmp1,itmp2,done;
    
    done = 0;
    while (1)
    {
	ir = read_word(pc);	/* fetch instruction */
	newpc = -1;
	monhit = 0;
	branch_flag = NOTABRANCH;
	ldstaddr = MEMADD;

	switch (ir & 0xFC000000) /* switch on 6 opcode bits */
	{
	case SPECIAL:
	    switch(ir & 0xFC00003F)
	    {
	    case NOP:
		nop_count++;
		addstalls(NONE,0,NONE,0,NOP_UNIT,NONE,0);
		PRINT_INSTR();
		break;
	    case ADD:
		if (REGc != 0)
		    SREG(REGc) = SREG(REGa) + SREG(REGb);
		if (overflow(SREG(REGc),SREG(REGa), SREG(REGb)))
		    done = 1;	/* return, but update icount first */
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case ADDU:
		if (REGc != 0)
		    UREG(REGc) = UREG(REGa) + UREG(REGb);
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case AND:
		if (REGc != 0)
		    UREG(REGc) = UREG(REGa) & UREG(REGb);
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case MOVD:
		DREG(REGc) = DREG(REGa);
		addstalls(DPR,REGa,NONE,0,INT_UNIT,DPR,REGc);
		PRINT_INSTR();
		break;
	    case MOVF:
		FREG(REGc) = FREG(REGa);
		addstalls(FPR,REGa,NONE,0,INT_UNIT,FPR,REGc);
		PRINT_INSTR();
		break;
	    case MOVFP2I:
		UREG(REGc) = UFREG(REGa);
		addstalls(FPR,REGa,NONE,0,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case MOVI2FP:
		UFREG(REGc) = UREG(REGa);
		addstalls(INTR,REGa,NONE,0,INT_UNIT,FPR,REGc);
		PRINT_INSTR();
		break;
	    case MOVI2S:
		printf("Opcode not implemented: movi2s\n");
		PRINT_INSTR();
		break;
	    case MOVS2I:
		printf("Opcode not implemented: movs2i\n");
		PRINT_INSTR();
		break;
	    case OR:
		if (REGc != 0)
		    UREG(REGc) = UREG(REGa) |
			UREG(REGb);
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case SEQ:
		SREG(REGc) = (SREG(REGa) == SREG(REGb) &&
				   REGc != 0) ? 1 : 0;
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case SGE:
		SREG(REGc) = (SREG(REGa) >= SREG(REGb) &&
				   REGc != 0) ? 1 : 0;
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case SGEU:
		UREG(REGc) = (UREG(REGa) >= UREG(REGb) &&
				   REGc != 0) ? 1 : 0;
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case SGT:
		SREG(REGc) = (SREG(REGa) > SREG(REGb) &&
				   REGc != 0) ? 1 : 0;
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case SGTU:
		UREG(REGc) = (UREG(REGa) > UREG(REGb) &&
				   REGc != 0) ? 1 : 0;
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case SLE:
		SREG(REGc) = (SREG(REGa) <= SREG(REGb) &&
				   REGc != 0) ? 1 : 0;
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case SLEU:
		UREG(REGc) = (UREG(REGa) <= UREG(REGb) &&
				   REGc != 0) ? 1 : 0;
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case SLL:
		if (REGc != 0)
		    UREG(REGc) = UREG(REGa) << UREG(REGb);
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case SLT:
		SREG(REGc) = (SREG(REGa) < SREG(REGb) &&
				   REGc != 0) ? 1 : 0;
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case SLTU:
		UREG(REGc) = (UREG(REGa) < UREG(REGb) &&
				   REGc != 0) ? 1 : 0;
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case SNE:
		SREG(REGc) = (SREG(REGa) != SREG(REGb) &&
				   REGc != 0) ? 1 : 0;
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case SRA:
		if (REGc != 0)
		{
		    if (0x80000000 & UREG(REGa))
		    {
			UREG(REGc) = UREG(REGa) >> UREG(REGb);
			tmp = 0x80000000;
			for (tmp2 = 0; tmp2 < UREG(REGb); tmp2++)
			    tmp |= (tmp>>tmp2);
			UREG(REGc) |= tmp;
		    }
		    else
			UREG(REGc) = UREG(REGa) >> UREG(REGb);
		}
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case SRL:
		if (REGc != 0)
		    UREG(REGc) = UREG(REGa) >> UREG(REGb);
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case SUB:
		if (REGc != 0)
		    SREG(REGc) = SREG(REGa) -
			SREG(REGb);
		if (overflow(SREG(REGc),SREG(REGa),
			      -1*SREG(REGb)))
		    done = 1;
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case SUBU:
		if (REGc != 0)
		    UREG(REGc) = UREG(REGa) - UREG(REGb);
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    case XOR:
		if (REGc != 0)
		    UREG(REGc) = UREG(REGa) ^ UREG(REGb);
		addstalls(INTR,REGa,INTR,REGb,INT_UNIT,INTR,REGc);
		PRINT_INSTR();
		break;
	    default: 
		printf("Undefined opcode: %x\n",ir);
		return;
	    }
	    break;
	case FPARITH:
	    switch(ir & 0xFC00003F) /* both opcode and func bits */
	    {
	    case ADDD:
		DREG(REGc) = DREG(REGa) +
		    DREG(REGb);
		addstalls(DPR,REGa,DPR,REGb,ADD_UNIT,DPR,REGc);
		PRINT_INSTR();
		break;
	    case ADDF:
		FREG(REGc) = FREG(REGa) + FREG(REGb); 
		addstalls(FPR,REGa,FPR,REGb,ADD_UNIT,FPR,REGc);
		PRINT_INSTR();
		break;
	    case CVTD2F:
		FREG(REGc) = (float) DREG(REGa);
		addstalls(DPR,REGa,NONE,0,ADD_UNIT,FPR,REGc);
		PRINT_INSTR();
		break;
	    case CVTD2I:
		SFREG(REGc) = (int) DREG(REGa);
		addstalls(DPR,REGa,NONE,0,ADD_UNIT,FPR,REGc);
		PRINT_INSTR();
		break;
	    case CVTF2D:
		DREG(REGc) = (double) FREG(REGa);
		addstalls(FPR,REGa,NONE,0,ADD_UNIT,DPR,REGc);
		PRINT_INSTR();
		break;
	    case CVTF2I:
		SFREG(REGc) = (int) FREG(REGa);
		addstalls(FPR,REGa,NONE,0,ADD_UNIT,FPR,REGc);
		PRINT_INSTR();
		break;
	    case CVTI2D:
		DREG(REGc) = (double) SFREG(REGa);
		addstalls(FPR,REGa,NONE,0,ADD_UNIT,DPR,REGc);
		PRINT_INSTR();
		break;
	    case CVTI2F:
		FREG(REGc) = (float) SFREG(REGa);
		addstalls(FPR,REGa,NONE,0,ADD_UNIT,FPR,REGc);
		PRINT_INSTR();
		break;
	    case DIV:
		if (izero(REGb)) 
		    done = 1;
		else
		    SFREG(REGc) = SFREG(REGa) / SFREG(REGb);
		addstalls(FPR,REGa,FPR,REGb,DIV_UNIT,FPR,REGc);
		PRINT_INSTR();
		break;
	    case DIVD:
		if (dzero(REGb)) 
		    done = 1;
		else
		    DREG(REGc) = DREG(REGa) / DREG(REGb);
		addstalls(DPR,REGa,DPR,REGb,DIV_UNIT,DPR,REGc);
		PRINT_INSTR();
		break;
	    case DIVF:
		if (fzero(REGb)) 
		    done = 1;
		else
		    FREG(REGc) = FREG(REGa) / FREG(REGb); 
		addstalls(FPR,REGa,FPR,REGb,DIV_UNIT,FPR,REGc);
		PRINT_INSTR();
		break;
	    case DIVU:
		if (izero(REGb)) 
		    done = 1;
		else
		    UFREG(REGc) = UFREG(REGa) / UFREG(REGb);
		addstalls(FPR,REGa,FPR,REGb,DIV_UNIT,FPR,REGc);
		PRINT_INSTR();
		break;
	    case EQD:
		fp_status = (DREG(REGa) == DREG(REGb)) ? 1: 0; 
		addstalls(DPR,REGa,DPR,REGb,ADD_UNIT,FPS,0);
		PRINT_INSTR();
		break;
	    case EQF:
		fp_status = (FREG(REGa) == FREG(REGb)) ? 1: 0;
		addstalls(FPR,REGa,FPR,REGb,ADD_UNIT,FPS,0);
		PRINT_INSTR();
		break;
	    case GED:
		fp_status = (DREG(REGa) >= DREG(REGb)) ? 1: 0; 
		addstalls(DPR,REGa,DPR,REGb,ADD_UNIT,FPS,0);
		PRINT_INSTR();
		break;
	    case GEF:
		fp_status = (FREG(REGa) >= FREG(REGb)) ? 1: 0;
		addstalls(FPR,REGa,FPR,REGb,ADD_UNIT,FPS,0);
		PRINT_INSTR();
		break;
	    case GTD:
		fp_status = (DREG(REGa) > DREG(REGb)) ? 1: 0; 
		addstalls(DPR,REGa,DPR,REGb,ADD_UNIT,FPS,0);
		PRINT_INSTR();
		break;
	    case GTF:
		fp_status = (FREG(REGa) > FREG(REGb)) ? 1: 0;
		addstalls(FPR,REGa,FPR,REGb,ADD_UNIT,FPS,0);
		PRINT_INSTR();
		break;
	    case LED:
		fp_status = (DREG(REGa) <= DREG(REGb)) ? 1: 0; 
		addstalls(DPR,REGa,DPR,REGb,ADD_UNIT,FPS,0);
		PRINT_INSTR();
		break;
	    case LEF:
		fp_status = (FREG(REGa) <= FREG(REGb)) ? 1: 0;
		addstalls(FPR,REGa,FPR,REGb,ADD_UNIT,FPS,0);
		PRINT_INSTR();
		break;
	    case LTD:
		fp_status = (DREG(REGa) < DREG(REGb)) ? 1: 0; 
		addstalls(DPR,REGa,DPR,REGb,ADD_UNIT,FPS,0);
		PRINT_INSTR();
		break;
	    case LTF:
		fp_status = (FREG(REGa) < FREG(REGb)) ? 1: 0;
		addstalls(FPR,REGa,FPR,REGb,ADD_UNIT,FPS,0);
		PRINT_INSTR();
		break;
	    case MULT:
		itmp1 = SFREG(REGa); itmp2 = SFREG(REGb);
		SFREG(REGc) = SFREG(REGa) * SFREG(REGb);
		if (SFREG(REGc) != (itmp1 * itmp2))
		{
		    printf("Multiply overflow \n");
		    done = 1;
		}
		addstalls(FPR,REGa,FPR,REGb,MUL_UNIT,FPR,REGc);
		PRINT_INSTR();
		break;
	    case MULTD:
		DREG(REGc) = DREG(REGa) *
		    DREG(REGb);
		addstalls(DPR,REGa,DPR,REGb,MUL_UNIT,DPR,REGc);
		PRINT_INSTR();
		break;
	    case MULTF:
		FREG(REGc) = FREG(REGa) * FREG(REGb); 
		addstalls(FPR,REGa,FPR,REGb,MUL_UNIT,FPR,REGc);
		PRINT_INSTR();
		break;
	    case MULTU:
		UFREG(REGc) = UFREG(REGa) * UFREG(REGb);
		addstalls(FPR,REGa,FPR,REGb,MUL_UNIT,FPR,REGc);
		PRINT_INSTR();
		break;
	    case NED:
		fp_status = (DREG(REGa) != DREG(REGb)) ? 1: 0; 
		addstalls(DPR,REGa,DPR,REGb,ADD_UNIT,FPS,0);
		PRINT_INSTR();
		break;
	    case NEF:
		fp_status = (FREG(REGa) != FREG(REGb)) ? 1: 0;
		addstalls(FPR,REGa,FPR,REGb,ADD_UNIT,FPS,0);
		PRINT_INSTR();
		break;
	    case SUBD:
		DREG(REGc) = DREG(REGa) - DREG(REGb);
		addstalls(DPR,REGa,DPR,REGb,ADD_UNIT,DPR,REGc);
		PRINT_INSTR();
		break;
	    case SUBF:
		FREG(REGc) = FREG(REGa) - FREG(REGb); 
		addstalls(FPR,REGa,FPR,REGb,ADD_UNIT,FPR,REGc);
		PRINT_INSTR();
		break;
	    default:
		printf("Undefined opcode: %x\n",ir);
		return;
	    }
	    break;
	case ADDI:
	    if (REGb != 0)
		SREG(REGb) = SREG(REGa) + SIMMED;
	    if (overflow(SREG(REGb),SREG(REGa),SIMMED))
		done = 1;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case ADDUI:
	    if (REGb != 0)
		UREG(REGb) = UREG(REGa) + UIMMED;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case ANDI:
	    if (REGb != 0)
		UREG(REGb) = UREG(REGa) & UIMMED;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case BEQZ:
	    if (UREG(REGa) == 0)
	    {
		newpc = pc + SIMMED + 4;
		do_br_stats(TAKEN);
		branch_flag = BRANCHTAKEN;
	    }
	    else
	    {
		do_br_stats(UNTAKEN);
		branch_flag = BRANCHNOTTAKEN;
	    }
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,NONE,0);
	    PRINT_INSTR();
	    break;
	case BFPF:
	    if (fp_status == 0)
	    {
		newpc = pc + SIMMED + 4;
		do_br_stats(TAKEN);
		branch_flag = BRANCHTAKEN;
	    }
	    else
	    {
		do_br_stats(UNTAKEN);
		branch_flag = BRANCHNOTTAKEN;
	    }
	    addstalls(FPS,0,NONE,0,INT_UNIT,NONE,0);
	    PRINT_INSTR();
	    break;
	case BFPT:
	    if (fp_status != 0)
	    {
		newpc = pc + SIMMED + 4;
		do_br_stats(TAKEN);
		branch_flag = BRANCHTAKEN;
	    }
	    else
	    {
		do_br_stats(UNTAKEN);
		branch_flag = BRANCHNOTTAKEN;
	    }
	    addstalls(FPS,0,NONE,0,INT_UNIT,NONE,0);
	    PRINT_INSTR();
	    break;
	case BNEZ:
	    if (UREG(REGa) != 0)
	    {
		newpc = pc + SIMMED + 4;
		do_br_stats(TAKEN);
		branch_flag = BRANCHTAKEN;
	    }
	    else
	    {
		do_br_stats(UNTAKEN);
		branch_flag = BRANCHNOTTAKEN;
	    }
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,NONE,0);
	    PRINT_INSTR();
	    break;
	case J:
	    newpc = pc + OFFSET + 4;
	    do_j_stats(newpc);
	    branch_flag = BRANCHTAKEN;
	    addstalls(NONE,0,NONE,0,INT_UNIT,NONE,0);
	    PRINT_INSTR();
	    break;
	case JAL:
	    UREG(31) = pc + 8;
	    newpc = pc + OFFSET + 4;
	    do_j_stats(newpc);
	    branch_flag = BRANCHTAKEN;
	    addstalls(NONE,0,NONE,0,INT_UNIT,INTR,31);
	    PRINT_INSTR();
	    break;
	case JALR:
	    UREG(31) = pc + 8;
	    newpc = UREG(REGa);
	    do_j_stats(newpc);
	    branch_flag = BRANCHTAKEN;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,31);
	    PRINT_INSTR();
	    break;
	case JR:
	    newpc = UREG(REGa);
	    do_j_stats(newpc);
	    branch_flag = BRANCHTAKEN;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,NONE,0);
	    PRINT_INSTR();
	    break;
	case LB:
	    if (REGb != 0)
	    {
		UREG(REGb) = read_byte(MEMADD);
		if (UREG(REGb) & 0x80) /* sign extend */
		    UREG(REGb) |= 0xFFFFFF00;
	    }
	    addstalls(INTR,REGa,NONE,0,LOAD_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case LBU:
	    if (REGb != 0)
		UREG(REGb) = read_byte(MEMADD);
	    addstalls(INTR,REGa,NONE,0,LOAD_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case LD:
	    DREG(REGb) = read_double(MEMADD);
	    addstalls(INTR,REGa,NONE,0,LOAD_UNIT,DPR,REGb);
	    PRINT_INSTR();
	    break;
	case LF:
	    UFREG(REGb) = read_word(MEMADD);
	    addstalls(INTR,REGa,NONE,0,LOAD_UNIT,FPR,REGb);
	    PRINT_INSTR();
	    break;
	case LH:
	    if (REGb != 0)
	    {
		UREG(REGb) = read_half(MEMADD);
		if (UREG(REGb) & 0x8000) /* sign extend */
		    UREG(REGb) |= 0xFFFF0000;
	    }
	    addstalls(INTR,REGa,NONE,0,LOAD_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case LHI:
	    if (REGb != 0)
		UREG(REGb) = (UIMMED << 16);
	    addstalls(NONE,0,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case LHU:
	    if (REGb != 0)
		UREG(REGb) = read_half(MEMADD);
	    addstalls(INTR,REGa,NONE,0,LOAD_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case LW:
	    if (REGb != 0)
		UREG(REGb) = read_word(MEMADD);
	    addstalls(INTR,REGa,NONE,0,LOAD_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case ORI:
	    if (REGb != 0)
		UREG(REGb) = UREG(REGa) | UIMMED;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case RFE:
	    printf("Opcode not implemented: rfe \n");
	    PRINT_INSTR();
	    return;
	case SB:
	    write_byte((unsigned char) 0xFF & UREG(REGb), MEMADD);
	    addstalls(INTR,REGa,INTR,REGb,LOAD_UNIT,NONE,0);
	    PRINT_INSTR();
	    break;
	case SD:
	    write_double(DREG(REGb), MEMADD);
	    addstalls(INTR,REGa,DPR,REGb,LOAD_UNIT,NONE,0);
	    PRINT_INSTR();
	    break;
	case SEQI:
	    SREG(REGb) = (SREG(REGa) == SIMMED &&
			   REGb != 0) ? 1 : 0;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case SF:
	    write_word(UFREG(REGb), MEMADD);
	    addstalls(INTR,REGa,FPR,REGb,LOAD_UNIT,NONE,0);
 	    PRINT_INSTR();
	    break;
	case SGEI:
	    SREG(REGb) = (SREG(REGa) >= (int) SIMMED && REGb != 0) ? 1 : 0;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case SGEUI:
	    UREG(REGb) = (UREG(REGa) >= UIMMED && REGb != 0) ? 1 : 0;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case SGTI:
	    SREG(REGb) = (SREG(REGa) > (int) SIMMED && REGb != 0) ? 1 : 0;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case SGTUI:
	    UREG(REGb) = (UREG(REGa) > UIMMED && REGb != 0) ? 1 : 0;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case SH:
	    write_half((unsigned short) UREG(REGb)&0xFFFF, MEMADD);
	    addstalls(INTR,REGa,INTR,REGb,LOAD_UNIT,NONE,0);
	    PRINT_INSTR();
	    break;
	case SLEI:
	    SREG(REGb) = (SREG(REGa) <= (int) SIMMED && REGb != 0) ? 1 : 0;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case SLEUI:
	    UREG(REGb) = (UREG(REGa) <= UIMMED && REGb != 0) ? 1 : 0;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case SLLI:
	    if (REGb != 0)
		UREG(REGb) = UREG(REGa) << UIMMED;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case SLTI:
	    SREG(REGb) = (SREG(REGa) < (int) SIMMED && REGb != 0) ? 1 : 0;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case SLTUI:
	    UREG(REGb) = (UREG(REGa) < UIMMED && REGb != 0) ? 1 : 0;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case SNEI:
	    SREG(REGb) = (SREG(REGa) != SIMMED && REGb != 0) ? 1 : 0;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case SRAI:
	    if (REGb != 0)
	    {
		if (0x80000000 & UREG(REGa)) /* must sign extend */
		{
		    UREG(REGb) = UREG(REGa) >> UIMMED;
		    tmp = 0x80000000;
		    for (tmp2 = 0; tmp2 < UIMMED; tmp2++)
			tmp |= (tmp>>tmp2);
		    UREG(REGb) |= tmp;
		}
		else		/* no sign extension */
		    UREG(REGb) = UREG(REGa) >> UIMMED;
	    }
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case SRLI:
	    if (REGb != 0)
		UREG(REGb) = UREG(REGa) >> UIMMED;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case SUBI:
	    if (REGb != 0)
		SREG(REGb) = SREG(REGa) - SIMMED;
	    if (overflow(SREG(REGb),SREG(REGa), -1*SIMMED))
		done = 1;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case SUBUI:
	    if (REGb != 0)
		UREG(REGb) = UREG(REGa) - UIMMED;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	case SW:
	    write_word(UREG(REGb),MEMADD);
	    addstalls(INTR,REGa,INTR,REGb,LOAD_UNIT,NONE,0);
	    PRINT_INSTR();
	    break;
	case TRAP:
	    /* assume all traps return value in reg 2 */
	    addstalls(NONE,0,NONE,0,NOP_UNIT,INTR,2);
	    if (handle_trap(OFFSET))  /* call routine to handle */
		done = 1;
	    break;
	case XORI:
	    if (REGb != 0)
		UREG(REGb) = UREG(REGa) ^ UIMMED;
	    addstalls(INTR,REGa,NONE,0,INT_UNIT,INTR,REGb);
	    PRINT_INSTR();
	    break;
	default:		/* undefined */
	    printf("Undefined opcode: %x \n", ir);
	    return;
	}			/* close of main switch */

	pc = nextpc;		/* update program counters */
	nextpc = (newpc == -1) ? nextpc + 4 : newpc;

	/* increment instruction stats */
	i_count++;
	tmp =  (ir & 0xFC000000) >> 26; /* create opcode index */
	tmp2 = (ir & 0x0000003F); /* and func or special index */
	if (tmp == (SPECIAL >> 26))
	    spec[tmp2].count++;
	else if (tmp == (FPARITH >> 26))
	    fpops[tmp2].count++;
	else
	    mainops[tmp].count++;

	if (monhit)		/* test monitor range */
	    printf("  <Monitor> write occurred at address %#x \n",
		monhit);

	if (done) 
	{
	    in_execution = 0;
	    return;
	}

	if (steps > 0)
	{
	    if (steps == 1)
	    {
		in_execution = 1;
		return;
	    }
	    steps--;
	}
	if (brkpt_cnt)		/* test breakpoints */
	{
	    for (tmp = 0; tmp < brkpt_cnt; tmp++)
	    {
		if (pc == brkpt[tmp])
		{
		    printf("  Breakpoint reached at %#x \n",pc);
		    in_execution = 1;
		    return;
		}
	    }
	}
	if (monhit)
	{
	    in_execution = 1;
	    return;
	}
	if (i_count % 500000 == 0) /* output progress marker */
	    printf("Instructions: %d\n", i_count);
    }	/* close of while(1) */
}

void out_jstats(void)
{
    int tmp1, tmp2;
    if (i_count == 0)
    {
	printf("  No instructions executed.\n");
	return;
    }
    
    printf("  Total branches: %d   (%d%% of total instructions)\n",
	   b_count,(int)(b_count/(i_count/100.0)));
    tmp1 = b_count_f_t + b_count_b_t;
    tmp2 = b_count_f_u + b_count_b_u;
    if (b_count > 0)
    {
	printf("    taken:   %d [%d%%]     not taken: %d [%d%%]\n",
	       tmp1,(int)(tmp1/(b_count/100.0)),tmp2,(int)(tmp2/(b_count/100.0)));
	printf("    forward: %d [%d%%]\n",b_count_f,
	   (int)(b_count_f/(b_count/100.0)));
	if (b_count_f > 0)
	{
	    printf("        taken: %d [%d%%]   not taken: %d [%d%%]\n",
		   b_count_f_t,(int)(b_count_f_t/(b_count_f/100.0)),
		   b_count_f_u,(int)(b_count_f_u/(b_count_f/100.0)));
	}
	printf("    backward: %d [%d%%]\n",b_count_b,
	   (int)(b_count_b/(b_count/100.0)));
	if (b_count_b > 0)
	{
	    printf("        taken: %d [%d%%]   not taken: %d [%d%%]\n",
		   b_count_b_t,(int)(b_count_b_t/(b_count_b/100.0)),
		   b_count_b_u,(int)(b_count_b_u/(b_count_b/100.0)));
	}
	printf("    delay slots filled: %d [%d%%]\n", b_count_d_f,
	       (int)(b_count_d_f/(b_count/100.0)));
    }
    printf("\n  Total jumps: %d   (%d%% of total instructions)\n",
	   j_count,(int)(j_count/(i_count/100.0)));
    if (j_count > 0)
    {
	printf("    forward: %d [%d%%]     backward: %d [%d%%]\n",
	       j_count_f, (int)(j_count_f/(j_count/100.0)),
	       j_count_b, (int)(j_count_b/(j_count/100.0)));
	printf("    delay slots filled: %d [%d%%]\n", j_count_d_f,
	       (int)(j_count_d_f/(j_count/100.0)));
    }
}

void out_istats(int type)
{   /* type is PCOUNT or PPERCENT: print count or % */
    int i,count;

    count = 0;
    for (i = 0; i < NUMDLXINST; i++)
    {
	printf("%7s ", sort_hist[i]->name);
	if (type == PCOUNT)
	{			/* print count */
	    if (sort_hist[i]->count == 0)
		printf("   -      ");
	    else
		printf("%-10lu", sort_hist[i]->count);
	}
	else			/* print % */
	{
	    if (sort_hist[i]->count == 0)
		printf("   -    ");
	    else
		printf("%4.1f%%   ", (float) (sort_hist[i]->count/(i_count/100.0)));
	}
	count += sort_hist[i]->count;
	if ((i % 4) == 3) printf("\n");
    }
    /* consistency checks */
    if (count != i_count)
	printf("\nInstruction count mismatch! (%d vs %d)\n",
	       count,i_count);
    printf("Total instruction count: %d\n",i_count);
}

int myfread(unsigned int *x, FILE *fp)
{
    /* reads 4 bytes from file (big-endian) and constructs a word with
       the same bit pattern with code that works on little-endian or
       big-endian machine */
    unsigned char c;
    int t;
    fread((void *) &c, sizeof(char), 1, fp);
    *x = c << 24;
    fread((void *) &c, sizeof(char), 1, fp);
    *x |= c << 16;
    fread((void *) &c, sizeof(char), 1, fp);
    *x |= c << 8;
    t = fread((void *) &c, sizeof(char), 1, fp);
    *x |= c;
    return t;
}

int load_binary(char *fname)
{
    /* load in specified .dlx binary file */

    FILE *fp;
    int i, res;
    unsigned int size;
    unsigned int tmp, tmp2, *tmpp;
    char *buf;
    int fname_len;

    fname_len = strlen(fname);
    buf = ".dlx";
    for (i = fname_len-1; i >=0 && fname[i] != '.'; i--);
    if (i == 0 || strcmp(fname+i,buf) != 0)
    {
	printf("Bad file name: %s.  (Must have .dlx extension.)\n", fname);
	return(0);
    }
    fp = fopen(fname, "r");
    if (fp == NULL)
    {
	printf("Couldn't open %s\n", fname);
	return(0);
    }

    /* read magic number */
    res = fread((void *) &tmp, 4, 1, fp);
    if (res < 1)
    {
	printf("Can't read from input file \n");
	return(0);
    }
    buf = (char *) &tmp;
    if (strncmp(buf,"BYU!", 4) != 0)
    {
	printf("Not a dlx binary -- bad magic number\n");
	return(0);
    }
    
    /* read address of main */
    res = myfread(&tmp2, fp);
    if (res < 1)
    {
	printf("Can't read from input file \n");
	return(0);
    }
    mainpc = (int) tmp2;
    
    /* read integer register initializers */
    for (i = 0; i < 32; i++)
    {
	res = myfread(&tmp2, fp);
	if (res < 1)
	{
	    printf("Can't read from input file \n");
	    return(0);
	}
	rfile_init[i] = (unsigned int) tmp2;
    }
    init_registers(0);

    /* read code segment length */
    res = myfread(&tmp2, fp);
    if (res < 1)
    {
	printf("Can't read from input file \n");
	return(0);
    }
    size = (unsigned int) tmp2;

    /* read code segment starting address */
    res = myfread(&tmp, fp);
    if (res < 1)
    {
	printf("Can't read from input file \n");
	return(0);
    }
    if (size + tmp > code_size_act)
    {
	printf("Not enough memory to load file.\n");
	printf("File needs at least %d bytes (static), and memory is %d bytes.\n", 
	       size+tmp, code_size_act);
	return(0);
    }
    code_size = size;

    /* read segment contents into memory one word at a time */
    tmpp = (unsigned int *) &(code_seg[tmp]);
    for (i = 0; size > 0; i++, size -= 4)
    {
	res = fread(&(tmpp[i]), 4, 1, fp);
	if (res < 1)
	{
	    printf("Can't read from input file \n");
	    return(0);
	}
    }

    fclose(fp);
    return(1);
}	 

void run(void)
{
    int done,i,largc,j,largt[MAX_ARGS];
    unsigned int tmp, tmp2;
    unsigned int start,k;
    char *largv[MAX_ARGS];
    char cmd_line[80];
    char buf[80], *buf2;
    float *flt;
    double dbl;

    done = 0;
    while (!done)
    {
	/* get next command from user */
	if (in_execution)
	    printf("%s>e> ", progname);
	else
	    printf("%s>", progname);
	fgets(cmd_line, 80, stdin); /* get line of user input */
	parse_cmd(cmd_line,&largc,largv,largt);
	switch (*(largv[0]))	/* all commands based on first character */
	{
	case '\0':		/* all white space, just do prompt again */
	    break;

	case 'b':
	case 'B':
	    /* set breakpoint */
	    if (largc > 2 || (largc == 2 && largt[1] != NUM_ARG))
		printf("Usage: b [address]\n");
	    else if (largc < 2)
	    {			/* list breakpoints */
		if (brkpt_cnt == 0)
		    printf("  No breakpoints set\n");
		else
		{
		    printf("  Breakpoints\n");
		    for (j = 0; j < brkpt_cnt; j++)
			printf("    %d: %#x \n",j,brkpt[j]);
		}
	    }
	    else
	    {
		j = strtol(largv[1], (char **) NULL, 0); /* hex or decimal ok */
		brkpt[brkpt_cnt] = j;
		brkpt_cnt++;
		printf("  Breakpoint set at %#x \n",j);
	    }
	    break;

	case 'm':
	case 'M':
	    /* set memory monitor: breakpoint for data accesses in specified range */
	    if (largc == 1)
	    {
		if (monon)
		    printf("  Monitor range: %#x to %#x\n", monlo, monhi);
		else
		    printf("  Monitor off\n");
	    }
	    else if (largc > 3)
		printf("Usage: m address [length]\n");
	    else if (largc >= 2)
	    {
		if (largt[1] != NUM_ARG)
		    printf("Usage: m address [length]\n");
		else
		{
		    start = strtol(largv[1], (char **) NULL, 0); /* hex & dec */
		    if (start == 0)
		    {
			monon = 0;
			printf("  Monitor turned off\n");
		    }
		    else
		    {
			if (largc == 3)
			    j = atoi(largv[2]);
			else
			    j = 3;
			monon = 1;
			monlo = start;
			monhi = start + j;
			printf("  Monitor turned on: %#x to %#x\n", monlo, monhi);
		    }
		}
	    }
	    break;

	case 'c':
	case 'C':
	    /* clear specified breakpoints -- all if none listed */
	    if (largc > 2 || (largc == 2 && largt[1] != NUM_ARG))
		printf("Usage: c [breakpoint_number]\n");
	    else if (largc == 1) /* clear all */
	    {
		brkpt_cnt = 0;
		printf("  All breakpoints now cleared \n");
	    }
	    else		/* 2 args, second is bkpt to clear */
	    {
		j = strtol(largv[1], (char **) NULL, 0); /* bkpt to clear */
		for ( ; j < brkpt_cnt-1; j++)
		    brkpt[j] = brkpt[j+1];
		brkpt_cnt -= 1;
	    }
	    break;

	case 'l':
	case 'L':
	    /* load program */
	    if (largc != 2 || (largc == 2 && largt[1] != ID_ARG))
		printf("Usage: l filename\n");
	    else
	    {
		clear_all();
		if (load_binary(largv[1]))
		    printf(" loaded binary file %s\n", largv[1]);
	    }
	    break;

	case 'd':
	case 'D':
	    /* dump contents of memory from specified address */
	    /* for specified length (20 words default) */
	    if (largc == 1 || largc > 3 || largt[1] != NUM_ARG || 
		(largc == 3 && largt[2] != NUM_ARG))
		printf("Usage: d address [length]\n");
	    else
	    {
		/* strtol works for both hex & decimal */ 
		start = 0xFFFFFFFC & strtol(largv[1], (char **) NULL,0); 
		/* aligned to word by AND */
		if (largc > 2)
		    j = atoi(largv[2]);
		else
		    j = 20;
		if (j <= 0)
		    printf("Usage: d address [length]\n");
		else
		{
		    printf("Memory dump from %#x to %#x\n", 
			   start,start+(j*4)-1);
		    printf("                       Hex      Decimal");
		    printf("       Float         Dbl   Chars\n");
		    for (k = start; k < start + (4*j); k += 4)
		    {
			if (range_ok(k))
			{
			    tmp = read_word(k); /* no bytes swapped */
			    tmp2 = read_word(k); 
			    buf2 = (char *) &tmp;
			    flt = (float *) &tmp2;
			    sprintf(buf, "Mem[%x]:",k);
			    printf("  %-15s %8x", buf, tmp);
			    printf(" %12u %11.3g ", tmp2, *flt);
			    if ((k & 0x04) == 0 && range_ok(k+4))
			    {
				dbl = read_double(k);
				printf("%11.3g   \"", dbl);
			    }
			    else
				printf("              \"");
			    for (i = 0; i < 4; i++)
			    {
				if (isgraph(buf2[i]))
				    printf("%c",buf2[i]);
				else
				    printf(".");
			    }
			    printf("\"\n");
			}
			else
			    printf("  Mem[%x]:  out of range\n",k);
		    }
		}
	    }
	    break;

	case 'i':
	case 'I':
	    /* give instruction histogram */
	    if (largc > 2 || (largc == 2 && *(largv[1]) != 'p'))
	    {  
		printf("Usage: i [p]\n");
		break;
	    }
	    if (largc == 1)
		out_istats(PCOUNT);
	    else
		out_istats(PPERCENT);
	    break;

	case 'j':
	case 'J':
	    out_jstats();
	    break;

	case 'e':
	case 'E':
	    /* run loaded program to end or for specified number of */
	    /* steps from current pc or from main if not in execution */

	    if (largc > 2 || (largc == 2 && largt[1] != NUM_ARG))
		printf("Usage: e [steps]\n");
	    else if (largc == 1)
	    {
		if (in_execution == 1)
		{
		    in_execution = 0;
		    execute(0);
		}
		else
		{
		    if (mainpc == -1)
			printf("Cannot execute: don't know address of _main\n");
		    else
		    {
			pc = mainpc;
			clear_most();
			nextpc = pc + 4;
			execute(0);
		    }
		}
	    }
	    else		/* 2 args, second is number */
	    {
		if (in_execution == 0)
		{
		    if (mainpc == -1)
		    {
			printf("Cannot execute: don't know address of _main\n");
			break;
		    }
		    else
		    {
			pc = mainpc;
			clear_most();
			nextpc = pc + 4;
		    }
		}
		in_execution = 0;
		j = atoi(largv[1]);
		if (j >= 1)
		    execute(j);
		else
		    printf("Number of steps to execute must be positive\n");
	    }
	    break;

	case 'h':
	case 'H':
	case '?':
	    printf("  Commands:\n");
	    printf("  b [addr]     ; set a breakpoint for instruction at address\n");
	    printf("               ; if addr is missing, all breakpoints are listed\n");
	    printf("  c            ; clear all breakpoints\n");
	    printf("  d addr [len] ; hex dump of memory contents from addr \n");
            printf("               ; for len locations. len = 20 default \n");
	    printf("               ; addr can be decimal or hex (0x...) \n");
	    printf("  e [steps]    ; execute from current pc to end of program or\n");
	    printf("               ; specified number of instructions \n");
	    printf("  h            ; print this list of commands \n");
	    printf("  i [p]        ; print instruction stats [p: in percent]\n");
	    printf("  j            ; print jump and branch stats\n");
	    printf("  l file.dlx   ; load binary file \n");
	    printf("  m addr [len] ; set monitor to break on changes in specified\n");
	    printf("               ; memory address range. default len = +3 bytes\n");
	    printf("  p            ; output specifics of the pipeline configuration\n");
	    printf("  q            ; quit simulator \n");
	    printf("  r [h]        ; print register values [h: in hex]\n");
	    printf("  s            ; single step through next instruction \n");
	    printf("  t            ; print timing results \n");
	    printf("  v            ; toggle verbose mode \n");
	    printf("  ?            ; print this screen \n");
	    break;

	case 'p':
	case 'P':
	    /* output pipeline specs */
	    ppressed();
	    break;

	case 'r':
	case 'R':
	    /* print registers */
	    if (largc > 2 || (largc == 2 && *(largv[1]) != 'h'))
	    {
		printf("Usage: r [h]\n");
		break;
	    }
	    if (largc == 1)
	    {			/* print in decimal */
		printf("  PC: %d    FPstatus: %d\n", pc, fp_status);
		printf("  Integer: \n  ");
		for (j = 0; j < 32; j++)
		{
		    sprintf(buf,"%2d: %d",j,UREG(j));
		    printf("%-19s",buf);
		    if ((j % 4) == 3)
			printf("\n  ");
		}
		printf("FP: (single precision)\n  ");
		for (j = 0; j < 32; j++)
		{
		    sprintf(buf,"%2d: %.7g",j,FREG(j));
		    printf("%-19s",buf);
		    if ((j % 4) == 3)
			printf("\n  ");
		}
		printf("FP: (double precision)\n  ");
		for (j = 0; j < 16; j++)
		{
		    sprintf(buf,"%2d: %.7g",2*j,DREG(2*j));
		    printf("%-19s",buf);
		    if ((j % 4) == 3)
			printf("\n  ");
		}
	    }
	    else
	    {			/* print in hex */
		printf("  PC: %x    FPstatus: %x\n", pc, fp_status);
		printf("  Integer: \n  ");
		for (j = 0; j < 32; j++)
		{
		    sprintf(buf,"%2d: %x",j,UREG(j));
		    printf("%-19s",buf);
		    if ((j % 4) == 3)
			printf("\n  ");
		}
		printf("FP: \n  ");
		for (j = 0; j < 32; j++)
		{
		    sprintf(buf,"%2d: %x",j,UFREG(j));
		    printf("%-19s",buf);
		    if ((j % 4) == 3)
			printf("\n  ");
		}
	    }
	    printf("\n");
	    break;

	case 's':
	case 'S':
	    /* single step through execution: go from current */
	    /* instruction (if execution has begun) or from _main */
	    if (largc > 1)
		printf("No parameters expected\n");
	    else
	    {
		if (in_execution == 0)
		{
		    if (mainpc == -1)
		    {
			printf("Don't know address of main\n");
			break;
		    }
		    else
			start = mainpc;
		    clear_most();
		    pc = start;
		    in_execution = 1;
		    nextpc = pc + 4;
		    execute(1);
		}
		else
		    execute(1);
	    }
	    break;
	    
	case 't':
	case 'T':
	    /* output timing results */
	    tpressed();
	    break;

	case 'q':
	case 'Q':
	    /* quit simulator */
	    done = 1;
	    break;

	case 'v':
	case 'V':
	    /* toggle verbose output: turns off printf statements in */
	    /* execute() unless single stepping */
	    if (verbose == 1)
	    {
		verbose = 0;
		printf("Verbose mode off\n");	    
	    }
	    else
	    {
		verbose = 1;
		printf("Verbose mode on\n");
	    }
	    break;

	case 'z':
	case 'Z':
	    /* hook to dump BTB contents or anything else desired in
	       user's stall.c code */
	    zpressed();
	    break;

	default:
	    printf("Undefined command: %s\n",largv[0]);
	    break;

	}
    }
}

void init_once(void)
{				/* initializations done just once */
    init_rfile_init();		/* set reg inits to default */
    init_sort_hist();		/* initializes sort_hist[] */
    init_vars();		/* initialize internal variables */
    init_registers(0);		/* initialize all registers */
    clear_stats();		/* initialize internal stats */
    clearstall();		/* initialize all stall counters */
    init_mem();			/* initialize memory model */
    clib_init();		/* initialize clib internals */
    init_latency();
}

void usage(void) {
  fprintf(stderr,"Usage: %s <options> [<program>]\n",progname);
  fprintf(stderr," Options:\n");
  print_options(stderr);
}

int main(int argc, char *argv[])
{
    char tmp[80];
    int i,j;

    /* first, get name of program for prompt line from cmd line */
    strncpy(tmp, argv[0], 80);	/* copy to tmp */
    i = 0;
    j = 0;
    while (tmp[i] != '\0')	/* copy to progname, skipping
				   directory names on path */
    {
	if (tmp[i] == '/')
	    j = 0;
	else
	    progname[j++] = tmp[i];
	i++;
	if (j >= PROGNAMELEN) break;
    }
    progname[j] = '\0';

    init_once();

    j = 1;
    while (j < argc) {
      if (!strcmp(argv[j],"--help")) {
	usage();
	exit(0);
      } 
      else if (!strncmp(argv[j],"--",2)) {
	if (!handle_option(argv[j])) {
	  printf("Unrecognized option %s\n",argv[j]);
	  exit(1);
	}
      }
      else {
	clear_all();
	if (load_binary(argv[j]))
	  printf(" loaded binary file %s\n", argv[j]);
      }
      j++;
    }
    clearstall();
    run();			/* get user input, loop until done */
}

