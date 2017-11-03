/* Copyright (c) 2000 James Archibald, Brigham Young University */
/* Modifications copyright (c) 2006-2007 David A. Penry, Brigham Young University */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef max
#define max(x,y) ((x)>(y)?(x):(y))
#endif

#include "stalldef.h"	/* #defines in countstall parameters */
#include "ops.h"

/* these specify the latencies used: will remain constant throughout
   all labs.  */
#define LATENCY_INT  1
#define LATENCY_ADD  2
#define LATENCY_LOAD 1
#define LATENCY_MUL  5
#define LATENCY_DIV 19


/********* global variables declared in another file that you will need at
	   some point to complete the labs *************/
extern int verbose;		/* flag: set if verbose output expected */
extern int in_execution;	/* flag: set if currently executing */
extern int i_count;		/* number of instructions executed */
extern int nop_count;		/* number of nops executed */
extern char verbosestr[];	/* string output in verbose mode */
extern int branch_flag;		/* branch outcome flag */
extern int b_count;		/* number of branch instructions so far */
extern int j_count;		/* number of jump instructions so far */
extern int pc;			/* program counter */
extern unsigned long ir;        /* instruction register */
extern int newpc;               /* BTA for taken branches */
extern unsigned int ldstaddr;	/* ref address for loads&stores */

/********** globals that specify the simulated configuration ***********/

int latency[5];		/* latencies for all f_units, initialized with
                           #defines above */

#define NOBP      0
#define PERFECTBP 1
#define STATICBP  2
#define DYNAMICBP 3
int btbSize;
int historyBits;
int bpType;             /* branch prediction type */

/******************* globals to track state of pipeline *****************/
unsigned long prevIFcycle;
unsigned long prevIDcycle;

unsigned long branchRedirect;   /* cycle in which redirect takes effect */

/* global variables to store resource timestamps */
unsigned long  IRstamps[32];	/* int regs */
unsigned long  FRstamps[32];	/* float regs */
unsigned long  DRstamps[32];	/* double regs */
unsigned long  FPSstamps;	/* FP status bit updates */

/* global variables to store identity of last funit to write */
unsigned long  IRwriter[32];	/* int regs */
unsigned long  FRwriter[32];	/* float regs */
unsigned long  DRwriter[32];	/* double regs */
unsigned long  FPSwriter;	/* FP status bit */

#define GHTAKEN    0		/* used to maintain global history bits */
#define GHNOTTAKEN 1

/* #defs for 2-bit branch predictor */
#define ST1  0			/* predict strong taken */
#define ST2  1			/* predict taken, but last NT */
#define ST3  2			/* predict NT, but last taken */
#define ST4  3			/* strong NT */

/*
 * TODO: DEFINE BRANCH PREDICTOR DATA STRUCTURES HERE
 */

/****** global variables defined here for statistics of interest ***********/

#define MAXUNITS 5
#define RAWindex 0
#define WAWindex 1
unsigned long totalstalls;		/* total stall count */
unsigned long specstalls[MAXUNITS][3];	/* specific stall counts, see tpressed
					   for interpretation */
unsigned long totalcycles;	/* total cycles required to execute */

unsigned long branchstalls;      /* branch stalls */
unsigned long totalMP;                     /* total mispredictions */
unsigned long countMP[3];                  /* count of specific mispredictions */

/* handy global used to simplify output routine */
char *f_unit_name[5] = { "int", "load", "add", "mult", "div" };

/******************* Initialization and options ************************/

void init_latencyBP(void){
    latency[INT_UNIT] = LATENCY_INT;
    latency[ADD_UNIT] = LATENCY_ADD;
    latency[LOAD_UNIT] = LATENCY_LOAD;
    latency[MUL_UNIT] = LATENCY_MUL;
    latency[DIV_UNIT] = LATENCY_DIV;
    bpType = NOBP;
}

void init_latency(void){
    latency[INT_UNIT] = LATENCY_INT;
    latency[ADD_UNIT] = LATENCY_ADD;
    latency[LOAD_UNIT] = LATENCY_LOAD;
    latency[MUL_UNIT] = LATENCY_MUL;
    latency[DIV_UNIT] = LATENCY_DIV;
    bpType = PERFECTBP;
}

int handle_option(char *arg){
  if (!strncmp(arg,"--lat:int=",10)){
    latency[INT_UNIT] = atoi(arg+10);
  }
  else if (!strncmp(arg,"--lat:add=",10)){
    latency[ADD_UNIT] = atoi(arg+10);
  }
  else if (!strncmp(arg,"--lat:load=",11)){
    latency[LOAD_UNIT]=atoi(arg+11);
  }
  else if (!strncmp(arg,"--lat:mul=",10)){
    latency[MUL_UNIT] = atoi(arg+10);
  }
  else if (!strncmp(arg,"--lat:div=",10)){
    latency[DIV_UNIT] = atoi(arg+10);
  }
  else if (!strcmp(arg,"--bp=none")){
    bpType = NOBP;
  }
  else if (!strcmp(arg,"--bp=perfect")){
    bpType = PERFECTBP;
  }
  else if (!strcmp(arg,"--bp=static")){
    bpType = STATICBP;
  }
  else if (!strncmp(arg,"--bp=dynamic:",13)){
    bpType = DYNAMICBP;
    sscanf(arg+13,"%d:%d", &historyBits, &btbSize);
  }
  else if (!strcmp(arg,"--labBP")){
    init_latencyBP();
  }
  else return 0;
  return 1;
}

void print_options(FILE *fp){
  fprintf(fp," --labBP           Set parameters to no branch predictor\n");
  fprintf(fp," --lat:<unit>=<#>  Set latency of unit to #\n");
  fprintf(fp,"                   Units are int,load,add,mul,div\n");
  fprintf(fp," --bp=<type>       Branch prediction:\n");
  fprintf(fp,"                   Types are none, perfect, static, dynamic:<# history bits>:<btb size>\n");
}

int mylog2(int n){
     int cnt;
     if (n<=0) return -1;
     n--;
     for (cnt = 0; n; cnt++,n>>=1);
     return(cnt);
}

void clearstall(void)
{
    /* This function is called at the beginning of execution and it
       initializes all data structures used to track timing. */

    int i;
    for (i = 0; i < 32; i++)
    {
	IRstamps[i] = 0;
	FRstamps[i] = 0;
	DRstamps[i] = 0;
	IRwriter[i] = 0;
	FRwriter[i] = 0;
	DRwriter[i] = 0;
    }
    FPSstamps = 0;
    FPSwriter = 0;
    totalstalls = 0;
    totalcycles = 0;
    prevIFcycle = 0;
    prevIDcycle = 0;
    for (i = 0; i < MAXUNITS; i++)
    {
	specstalls[i][0] = 0;
	specstalls[i][1] = 0;
    }

    branchstalls = 0;
    for (i = 0; i < 3; i++)
	countMP[i] = 0;
    totalMP = 0;

    /*
     * TODO: ALLOCATE BRANCH PREDICTOR DATA STRUCTURES HERE
     */

}

int handle_branch(int branch_flag,
		  int pc, /* PC of instruction */
		  unsigned long ir, /* The instruction's encoding */
		  int newpc /* actual target of branch */){

  /*
   * TODO: FILL IN
   */

     if (bpType == PERFECTBP){               // perfcet branch prediction
          return 0;
     } else if(bpType == STATICBP){          // static branch prediction
          branchstalls++;
          return 1;
     } else if(bpType == DYNAMICBP){         // dynamic branch prediction
     } else {                                // no branch prediction
          branchstalls++;
          return 1;
     }
}

void addstalls(int s1type, int src1, int s2type, int src2, int funit, int rtype, int result){
    /*   s1type, s2type, rtype:
	     NONE (0) => no operand
          INTR (1) => int register
	     FPR  (2) => float register (32 bits)
	     DPR  (3) => double register (64 bits)
	     FPS  (4) => fpstatus
	 src1, src2, result:
	         specifies register number
	 funit:  the functional unit required by this instruction
	         (one of #defines in stalldef.h)
	        INT_UNIT
		   LOAD_UNIT
		   ADD_UNIT
		   MUL_UNIT
		   DIV_UNIT
		   NOP_UNIT (don't execute this instruction)
    */

    /* Here is the algorithm for the basic pipeline:

       1. Check for RAW and WAW hazards: determine latest time that a)
       source operands will be available, and b) that most recent
       write to dest register will be complete to avoid WAW hazard.
       (These are done by comparing the current time with each
       register's timestamp.)  If a RAW and WAW hazard exist on
       the same register, ignore the WAW hazard.

       If one or more hazards cause a stall, attribute entire stall to
       functional unit of latest hazard to clear.  In case of tie,
       attribute to src1 if involved, else src2.  (There isn't a
       terrific reason for this other than that we can match results
       exactly.)

       2. Having determined the issue cycle of the current
       instruction, update all timing variables to reflect the
       appropriate passage of time.

       3. Update the time stamp for the destination register (if any)
       with the last cycle on which the new value is NOT available */

    int i,max1,maxunit1,max2,maxunit2,max3,maxunit3,stalls;
    unsigned long BGcycle,IFcycle,IDcycle,EXcycle,MEMcycle,WBcycle;
    char stallstr[200], tmpbuf[200];

    /* clear output strings if they will be used */
    if (verbose || in_execution){
	verbosestr[0] = '\0';
	stallstr[0] = '\0';
    }

    /* now handle timing */

    BGcycle = prevIFcycle; /* will be IF of last instruction */

    IFcycle = (prevIDcycle == 0) ? 1 : prevIDcycle;

    /* first possible IDcycle */
    prevIFcycle = IFcycle;
    IDcycle = IFcycle + 1;      /* first possible IDcycle */

    /* 1. check for RAW hazards */
    max1 = 0;
    switch (s1type)		/* check first operand  */
    {
    case NONE:
	break;
    case INTR:
	if (IRstamps[src1] > IDcycle)
	{
	    max1 = IRstamps[src1];
	    maxunit1 = IRwriter[src1];
	}
	break;
    case FPR:
	if (FRstamps[src1] > IDcycle)
	{
	    max1 = FRstamps[src1];
	    maxunit1 = FRwriter[src1];
	}
	break;
    case DPR:
	if (DRstamps[src1] > IDcycle)
	{
	    max1 = DRstamps[src1];
	    maxunit1 = DRwriter[src1];
	}
	break;
    case FPS:
	if (FPSstamps > IDcycle)
	{
	    max1 = FPSstamps;
	    maxunit1 = FPSwriter;
	}
	break;
    }

    max2 = 0;
    /* check second operand  */
    /* special case handling for stores */
    if (funit == LOAD_UNIT && s2type != NONE)
    {				/* this is a store */
	switch (s2type)
	{
	case INTR:
	    if (IRstamps[src2]-1 > IDcycle)
	    {
		max2 = IRstamps[src2]-1;
		maxunit2 = IRwriter[src2];
	    }
	    break;
	case FPR:
	    if (FRstamps[src2]-1 > IDcycle)
	    {
		max2 = FRstamps[src2]-1;
		maxunit2 = FRwriter[src2];
	    }
	    break;

	case DPR:
	    if (DRstamps[src2]-1 > IDcycle)
	    {
		max2 = DRstamps[src2]-1;
		maxunit2 = DRwriter[src2];
	    }
	    break;
	}
    }
    else
    {
	switch (s2type)		/* check second operand  */
	{
	case NONE:
	    break;
	case INTR:
	    if (IRstamps[src2] > IDcycle)
	    {
		max2 = IRstamps[src2];
		maxunit2 = IRwriter[src2];
	    }
	    break;
	case FPR:
	    if (FRstamps[src2] > IDcycle)
	    {
		max2 = FRstamps[src2];
		maxunit2 = FRwriter[src2];
	    }
	    break;
	case DPR:
	    if (DRstamps[src2] > IDcycle)
	    {
		max2 = DRstamps[src2];
		maxunit2 = DRwriter[src2];
	    }
	    break;
	case FPS:
	    if (FPSstamps > IDcycle)
	    {
		max2 = FPSstamps;
		maxunit2 = FPSwriter;
	    }
	    break;
	}
    }

    /* check for WAW hazards, but only if destination register != src
       regs */
    max3 = 0;
    if ((s1type != rtype || src1 != result) &&
	(s2type != rtype || src2 != result))
    {
	switch (rtype)		/* check destination register */
	{
	case NONE:
	    break;
	case INTR:
	    if (IRstamps[result] > IDcycle)
	    {
		max3 = IRstamps[result]+2;
		maxunit3 = IRwriter[result];
	    }
	    break;
	case FPR:
	    if (FRstamps[result] > IDcycle)
	    {
		max3 = FRstamps[result]+2;
		maxunit3 = FRwriter[result];
	    }
	    break;
	case DPR:
	    if (DRstamps[result] > IDcycle)
	    {
		max3 = DRstamps[result]+2;
		maxunit3 = DRwriter[result];
	    }
	    break;
	case FPS:
	    if (FPSstamps > IDcycle)
	    {
		max3 = FPSstamps+2;
		maxunit3 = FPSwriter;
	    }
	    break;
	}
    }

    /* 2. increment IDcycle if stall occurs, update statistics */
    if (max1 > 0 || max2 > 0 || max3 > 0)
    {				/* stall occurs */
	if (max2 > max1)	/* set max1 to longest src op */
	{
	    max1 = max2;
	    maxunit1 = maxunit2;
	}
	if (max3 > max1)
	{
	  stalls = max3-IDcycle;
	  if (verbose || in_execution){/* update output string */
	    sprintf(tmpbuf, " WAW:(%d, %s)", stalls, f_unit_name[maxunit3]);
	    strcat(stallstr,tmpbuf);
	  }
	  specstalls[maxunit3][WAWindex] += stalls;
	  totalstalls += stalls;
	  IDcycle = max3;
	}
	else
	{
	  stalls = max1-IDcycle;
	  if (verbose || in_execution){ /* update stall string */
	    sprintf(tmpbuf, " RAW:(%d, %s)", stalls, f_unit_name[maxunit1]);
	    strcat(stallstr,tmpbuf);
	  }
	  specstalls[maxunit1][RAWindex] += stalls;
	  totalstalls += stalls;
	  IDcycle = max1;
	}
    }
    else {	/* no stall occurs and IDcycle is okay */
    }

    /* wrap up stage timings */
    prevIDcycle = IDcycle;
    if (funit == NOP_UNIT)
	EXcycle = IDcycle + 1;	/* nops + traps*/
    else
	EXcycle = IDcycle + latency[funit];

    MEMcycle = EXcycle + 1;     /* perfect D-cache: never misses */
    WBcycle = MEMcycle + 1;
    totalcycles = IDcycle+1;

    /* 3. update result register timestamp */
    switch (rtype)
    {
    case NONE:
	break;
    case INTR:
	IRstamps[result] = funit == LOAD_UNIT ? MEMcycle : EXcycle;
	IRwriter[result] = funit;
	break;
    case FPR:
	FRstamps[result] = funit == LOAD_UNIT ? MEMcycle : EXcycle;
	FRwriter[result] = funit;
	break;
    case DPR:
	DRstamps[result] = funit == LOAD_UNIT ? MEMcycle : EXcycle;
	DRwriter[result] = funit;
	break;
    case FPS:
	FPSstamps = EXcycle;
	FPSwriter = funit;
	break;
    }

    if (branchRedirect) /* in delay slot of mispred */
      {
	if (branchRedirect > IDcycle){ /* nominal time */
	  stalls = branchRedirect - IDcycle;
	  totalstalls += stalls;
	  branchstalls += stalls;

	  if (verbose || in_execution){
	    sprintf(tmpbuf, " BP:(%d)", stalls);
	    strcat(stallstr,tmpbuf);
	  }

	  //prevIFcycle = branchRedirect-1;
	  prevIDcycle = branchRedirect; /* so stalls end up not being fetch */

	} else {
	  /* make sure we don't say we fetched it ridiculously early when
	   * instruction in delay slot stalled
	   */
	  prevIFcycle = max(branchRedirect-1,prevIFcycle);
	  if (verbose || in_execution){
	    sprintf(tmpbuf, " BP:(0)");
	    strcat(stallstr, tmpbuf);
	  }
	}
	branchRedirect = 0;
      }
    {
      int brlat;
      /* take a look at branches */
      if ((brlat=handle_branch(branch_flag, pc, ir, newpc))){ /* mispredicted */
	branchRedirect = IDcycle + brlat; /* time when new IF should occur */
      }
      else {
	branchRedirect = 0;
      }
    }

    /* generate verbose output if desired */
    if (verbose || in_execution)
    {
	sprintf(verbosestr,"[%5lu IF+%2lu ID+%2lu (%5lu) EX+%2lu MEM+%2lu]%s",
		BGcycle,IFcycle-BGcycle,IDcycle-IFcycle,IDcycle,
		EXcycle-IDcycle, MEMcycle-EXcycle, stallstr);
    }

    /* NOTE: if you want to see the branch predictor state after every branch,
       uncomment the following line */
    /* if (branch_flag != NOTABRANCH) zpressed(); */
}

void tpressed(void){
    /* This function is called when the user presses the 't' key at
       the mydlx> prompt.  It prints timing information, including
       classifications of stall types to give you some idea what you
       should speed up to get better performance.  As the pipeline
       changes, the possible causes of stalls also change, so this
       routine and the data structures it accesses need to be
       updated. */

    if (totalcycles == 0)
    {
	printf("  No instructions executed\n");
	return;
    }
    printf("  Stall cycles\n");
    printf("    integer:  RAW        %10lu  [%2d%%]\n",
	   specstalls[INT_UNIT][RAWindex],
	   (int)(specstalls[INT_UNIT][RAWindex]/(totalcycles/100.0)));
    printf("              WAW        %10lu  [%2d%%]\n",
	   specstalls[INT_UNIT][WAWindex],
	   (int)(specstalls[INT_UNIT][WAWindex]/(totalcycles/100.0)));
    printf("    ld/store: RAW        %10lu  [%2d%%]\n",
	   specstalls[LOAD_UNIT][RAWindex],
	   (int)(specstalls[LOAD_UNIT][RAWindex]/(totalcycles/100.0)));
    printf("              WAW        %10lu  [%2d%%]\n",
	   specstalls[LOAD_UNIT][WAWindex],
	   (int)(specstalls[LOAD_UNIT][WAWindex]/(totalcycles/100.0)));
    printf("    FP add:   RAW        %10lu  [%2d%%]\n",
	   specstalls[ADD_UNIT][RAWindex],
	   (int)(specstalls[ADD_UNIT][RAWindex]/(totalcycles/100.0)));
    printf("              WAW        %10lu  [%2d%%]\n",
	   specstalls[ADD_UNIT][WAWindex],
	   (int)(specstalls[ADD_UNIT][WAWindex]/(totalcycles/100.0)));
    printf("    FP mult:  RAW        %10lu  [%2d%%]\n",
	   specstalls[MUL_UNIT][RAWindex],
	   (int)(specstalls[MUL_UNIT][RAWindex]/(totalcycles/100.0)));
    printf("              WAW        %10lu  [%2d%%]\n",
	   specstalls[MUL_UNIT][WAWindex],
	   (int)(specstalls[MUL_UNIT][WAWindex]/(totalcycles/100.0)));
    printf("    FP div:   RAW        %10lu  [%2d%%]\n",
	   specstalls[DIV_UNIT][RAWindex],
	   (int)(specstalls[DIV_UNIT][RAWindex]/(totalcycles/100.0)));
    printf("              WAW        %10lu  [%2d%%]\n",
	   specstalls[DIV_UNIT][WAWindex],
	   (int)(specstalls[DIV_UNIT][WAWindex]/(totalcycles/100.0)));
    printf("    branch:              %10lu  [%2d%%]\n",
	   branchstalls,
	   (int)(branchstalls/(totalcycles/100.0)));
    printf("  Total stalls:          %10lu  [%2d%%]\n",
	   totalstalls, (int)(totalstalls/(totalcycles/100.0)));
    printf("  Total nops:            %10d  [%2d%%]\n",
	   nop_count,(int)(nop_count/(totalcycles/100.0)));
    printf("  Total wasted cycles:   %10lu  [%2d%%]\n",
	   nop_count+totalstalls,(int)((nop_count+totalstalls) /
	   (totalcycles/100.0)));
    printf("  Useful instr:          %10d \n",i_count-nop_count);
    printf("  Total cycles:          %10lu \n",totalcycles);
    printf("  CPI:                   %10.2f\n",(double) totalcycles /
	   (i_count-nop_count));
    if (b_count == 0 && j_count == 0)
	printf("  No branches executed\n");
    else
    {
	if (bpType == DYNAMICBP){

	  printf("  Mispred. br/jmps:      %10lu [%2d%% of %d br/jmps]\n",totalMP,
		 (int)(totalMP/((b_count+j_count)/100.0)),b_count+j_count);
	  printf("    BTB misses:          %10lu [%2d%%]\n",countMP[0],
		 (int)(countMP[0]/((b_count+j_count)/100.0)));
	  printf("    hit, wrong outcome:  %10lu [%2d%%]\n",countMP[1],
		 (int)(countMP[1]/((b_count+j_count)/100.0)));
	  printf("    hit, wrong address:  %10lu [%2d%%]\n",countMP[2],
		 (int)(countMP[2]/((b_count+j_count)/100.0)));
	  if (totalMP == 0)
	    printf("    No mispredictions\n");
	  else {
	    printf("    avg stalls/mispred:  %10.2f \n",
		   (double) (branchstalls)/(double) (totalMP));
	  }
	} else if (bpType == STATICBP){

	  printf("  Mispred. br/jmps:      %10lu [%2d%% of %d br/jmps]\n",totalMP,
		 (int)(totalMP/((b_count+j_count)/100.0)),b_count+j_count);
	  printf("    wrong prediction:    %10lu [%2d%%]\n",countMP[1],
		 (int)(countMP[1]/((b_count+j_count)/100.0)));
	  if (totalMP == 0)
	    printf("    No mispredictions\n");
	  else {
	    printf("    avg stalls/mispred:  %10.2f \n",
		   (double) (branchstalls)/(double) (totalMP));
	  }
	}
    }
}

static char *bpredtypename[] = { "none", "perfect", "static" , "dynamic"};

void ppressed(void){
    /* This function is called when the user presses the 'p' key at
       the mydlx> prompt.  It prints the current CPU configuration.
       As simulated configurations increase in complexity, it should
       be extended to output all relevant CPU parameters.  */

    printf("--Basic pipeline configuration--\n");
    printf("      UNIT     LATENCY\n");
    printf("  Integer unit:  %2d\n", latency[INT_UNIT]);
    printf("  Load unit:     %2d\n", latency[LOAD_UNIT]);
    printf("  FP adder:      %2d\n", latency[ADD_UNIT]);
    printf("     multiplier: %2d\n", latency[MUL_UNIT]);
    printf("     divider:    %2d\n", latency[DIV_UNIT]);

    printf("Branch predictor: %s\n", bpredtypename[bpType]);
    if (bpType == DYNAMICBP){
      printf("   BTB, with (%d,2) correlating\n", historyBits);
      printf("   BTB size:      %6d entries\n", btbSize);
    }
}

void fatalerrormsg(void){
    /* This function is called when any sort of run-time error is
       detected and execution is terminated.  It simply prints out the
       cycle count (maintained only in stall.c code) to help the user
       determine what happened and when.  */
    fprintf(stderr,"Current cycle: %lu\n",totalcycles);
}

void zpressed(void){
    /* This function is called when the user presses the 'z' key at
       the prompt.  It is used in this lab to dump branch prediction
       info. */

    int i, j, count;

    if (bpType != DYNAMICBP){
      printf("No dynamic branch predictor state\n");
      return;
    }

}
