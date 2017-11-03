/* Copyright (c) 2001 James Archibald, Brigham Young University */ 

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "mem.h"
#include "dlx.h"

/* The output routines assume that you have an array of integer
   registers accessed in the following way.  (These use macros that I
   have defined.) */

#define STACKPTR 29
#define EOS '\0'

extern unsigned int code_size;
extern unsigned int code_size_act;
extern unsigned int reg_file[];
extern void run_error(char *);

/* Change this if necessary */

/* extern int R[]; I don't need this */

/* This part is needed for use with the -Aa option */

#ifndef F_DUPFD
#define	F_DUPFD	0	/* Duplicate fields */
#define	F_GETFL	3	/* Get file flags */
#endif

FILE *file_ptr[FOPEN_MAX];

void clib_init(void)
{		/* called once at beginning of execution */
    int i; 
    file_ptr[0] = stdin;	/* (&__iob[0]) */
    file_ptr[1] = stdout;	/* (&__iob[1]) */
    file_ptr[2] = stderr;	/* (&__iob[2]) */
    for (i = 3; i < FOPEN_MAX; i++)
	file_ptr[i] = (FILE *) NULL;
}

void do_exit(void)			/* trap #0 */
{
    /* when open, read, write, close are added, need to close all 
       file descriptors also */
    int x;
    for (x = 3; x < FOPEN_MAX; x++)
    {
	if (file_ptr[x] != NULL)
	{
	    fclose(file_ptr[x]);
	    file_ptr[x] = (FILE *) NULL;
	}
    }
}

void do_fopen(void)			/* trap #1 */
{
    unsigned int stackptr;
    unsigned int strarg1, strarg2;
    char *arg1, *arg2;
    int i, nextfree;

    stackptr = UREG(STACKPTR);
    
    /* get ptrs to two char args, then make call to real fopen() */
    strarg1 = read_word(stackptr);
    strarg2 = read_word(stackptr+4);
    arg1 = actual_address(strarg1);
    arg2 = actual_address(strarg2);

    /* find an available entry in FILE ptr array */
    nextfree = 0;
    for (i = 3; i < FOPEN_MAX; i++)
    {
	if (file_ptr[i] == NULL)
	{
	    nextfree = i;
	    break;
	}
    }
    if (nextfree == 0)
    {
	SREG(2) = -1;
	return;
    }
    
    /* make the call, return an index to the file_ptr array */
    file_ptr[nextfree] = fopen(arg1,arg2);
    SREG(2) = nextfree;
}

void do_fclose(void)		/* trap #2 */
{
    unsigned int stackptr;
    unsigned int arg1;

    stackptr = UREG(STACKPTR);
    arg1 = read_word(stackptr);
    SREG(2) = fclose(file_ptr[arg1]);
    file_ptr[arg1] = (FILE *)NULL;
}

void do_fread(void)			/* trap #3 */
{
    unsigned int stackptr, res;
    unsigned int arg1, arg4;
    char *arg1ch;
    int size, nitems;

    stackptr = UREG(STACKPTR);
    arg1 = read_word(stackptr);	/* get ptr to buffer */
    arg1ch = actual_address(arg1);
    res = read_word(stackptr+4); /* get size */
    size = * ((int *) &res);
    res = read_word(stackptr+8); /* get nitems */
    nitems = * ((int *) &res);
    arg4 = read_word(stackptr+12); /* get index to file ptr */
    SREG(2) = fread(arg1ch, size, nitems, file_ptr[arg4]);
}

void do_fwrite(void)		/* trap #4 */
{
    printf("fwrite system call not yet implemented\n");
    return;
}

void do_printf(void)		/* trap #5 */
{
    /* get pointer to format string (rSTACKPTR is pointing to memory */
    /* location that has its address stored). Then march through */
    /* string, looking for each argument, peel off stack as each is */
    /* encountered */
    
    char buf[256], *p;
    int in_arg;			/* 1 if between % and end of arg */
    int buf_index;
    unsigned int stackptr, fmt_index;
    
    unsigned int temp;
    char *ctemp;
    double dtmp;
    
    /* get a pointer to current top of stack which has address to */
    /* format string */
    
    stackptr = UREG(STACKPTR);
    
    /* get address (in MemSpace of format string) */
    
    fmt_index = read_word(stackptr);
    
    /* format index is the first argument */
    
    /* update pointer to next word. We will parse the format string
       and take the necessary parameters from the stack */
    
    stackptr += 4; /* */
    
    /* start out not in argument */
    in_arg = 0;
    buf_index = 0;

    /* init a character pointer to march through the format string */
    p = actual_address(fmt_index);

    /* DEBUG ONLY 
    fprintf(stderr, "\n in printf: string pointed to is \"%s\"\n", p);
     END DEBUG */
    
    for ( ; *p != EOS ; p++)
    {
	if (in_arg) /* in arg: copy to terminating char */
	{
	    switch (*p)
	    {
	      case '%':		/* escape if prev was % also: %% is  */
		if (*(p-1) == '%')  /* just one % in printf */
		{
		    in_arg = 0;
		    printf("%c", *p);
		}
		else		/* a bad format string: continue */
		    buf[buf_index++] = *p;
		break;
	      case 'd':		/* handle signed ints */
	      case 'i': 
		buf[buf_index++] = *p;
		buf[buf_index] = EOS;
		temp = read_word(stackptr);
		stackptr += 4;
		printf(buf,(int) temp);
		in_arg = 0;
		break;
		
	      case 'o':		/* handle unsigned ints */
	      case 'x':
	      case 'X':
	      case 'u':
	      case 'c':
	      case 'p':
		buf[buf_index++] = *p;
		buf[buf_index] = EOS;
		temp = read_word(stackptr);
		stackptr += 4;
		printf(buf,temp);
		in_arg = 0;
		break;
		
	      case 's':		/* handle string */
		buf[buf_index++] = *p;
		buf[buf_index] = EOS;
		temp = read_word(stackptr);
		stackptr += 4;
		ctemp = actual_address(temp);
		if ( ctemp != NULL )
		    printf(buf, ctemp);
		in_arg = 0;
		break;
		
	      case 'f':		/* handle fp args (all doubles) */
	      case 'e':
	      case 'E':
	      case 'g':
	      case 'G':
		buf[buf_index++] = *p;
		buf[buf_index] = EOS;
		if (stackptr & 0x04) /* aligned on dbl word?  */
		    stackptr += 4;
		dtmp = read_double(stackptr);
		stackptr += 8;
		printf(buf, dtmp);
		in_arg = 0;
		break;
		
	      default:		/* still in arg: just copy */
		buf[buf_index++] = *p;
		break;
	    }
	}
	else                    /* in_arg == 0 */
	{		        /* (not in % arg) */
	    if (*p == '%')	/* start of % arg */
	    {
		in_arg = 1;
		buf_index = 0;	/* start copying to buf */
		buf[buf_index++] = *p;
	    }
	    else		/* just output */
		printf("%c", *p);
	}
    }				/* end for */
}

void do_scanf(void)			/* trap #6 */
{
    printf("scanf system call not yet implemented\n");
    return;
}

void do_fprintf(void)		/* trap #7 */
{
    char buf[256], *p;
    int in_arg;			/* 1 if between % and end of arg */
    int buf_index;
    unsigned int stackptr, fmt_index;
    unsigned int temp;
    char *ctemp;
    double dtmp;
    FILE *fp;
    
    /* top of stack has (1) index to stream ptr, (2) address of */
    /* format string, (3) additional args determined by format string */
    
    stackptr = UREG(STACKPTR);
    temp = read_word(stackptr);
    if (temp == 0 )		/* error if stdin */
    {
    	SREG(2) = (-1);
    	return;
    }

    fp = file_ptr[temp];
    stackptr+=4;
    
    /* format index is the first argument */
    fmt_index = read_word(stackptr);
    
    stackptr += 4;    
    in_arg = 0;
    buf_index = 0;
    p = actual_address(fmt_index); /* ptr p will march through string */
    
    for ( ; *p != EOS ; p++)
    {
	if (in_arg) /* in arg: copy to terminating char */
	{
	    switch (*p)
	    {
	      case '%':		/* escape if prev was % also: %% is  */
		if (*(p-1) == '%')  /* just one % in printf */
		{
		    in_arg = 0;
		    fprintf(fp, "%c", *p);
		}
		else		/* a bad format string: continue */
		    buf[buf_index++] = *p;
		break;
	      case 'd':		/* handle signed ints */
	      case 'i': 
		buf[buf_index++] = *p;
		buf[buf_index] = EOS;
		temp = read_word(stackptr);
		stackptr += 4;
		fprintf(fp, buf,(int) temp);
		in_arg = 0;
		break;
		
	      case 'o':		/* handle unsigned ints */
	      case 'x':
	      case 'X':
	      case 'u':
	      case 'c':
	      case 'p':
		buf[buf_index++] = *p;
		buf[buf_index] = EOS;
		temp = read_word(stackptr);
		stackptr += 4;
		fprintf(fp, buf, temp);
		in_arg = 0;
		break;
		
	      case 's':		/* handle string */
		buf[buf_index++] = *p;
		buf[buf_index] = EOS;
		temp = read_word(stackptr);
		stackptr += 4;
		ctemp = actual_address(temp);
		if ( ctemp != NULL )
		    fprintf(fp, buf, ctemp);
		in_arg = 0;
		break;
		
	      case 'f':		/* handle fp args (all doubles) */
	      case 'e':
	      case 'E':
	      case 'g':
	      case 'G':
		buf[buf_index++] = *p;
		buf[buf_index] = EOS;
		if (stackptr & 0x04)
		    stackptr += 4;
		dtmp = read_double(stackptr);
		stackptr += 8;
		fprintf(fp, buf, dtmp);
		in_arg = 0;
		break;
		
	      default:		/* still in arg: just copy */
		buf[buf_index++] = *p;
		break;
	    }
	}
	else                    /* in_arg == 0 */
	{		        /* (not in % arg) */
	    if (*p == '%')	/* start of % arg */
	    {
		in_arg = 1;
		buf_index = 0;	/* start copying to buf */
		buf[buf_index++] = *p;
	    }
	    else		/* just output */
		fprintf(fp, "%c", *p);
	}
    }				/* end for */
}

void do_fscanf(void)		/* trap #8 */
{
    /* CAUTION!!!  There is reason to believe that this code is not
       portable between both big-endian and little-endian machines.
       To the extent it has been tested, it works okay on big-endian
       machines, but has problems on little endian machines.
       (Notably, the backprop benchmark that does file I/O gets the
       float values with byte order reversed.) 

       The problem arises because the routines modify the memory
       location passed as a parameter -- there is probably some way of
       doing this that is portable between both types of machines, but
       even if I solve the above problem, the other cases will
       certainly not be tested exhaustively.  Use this routine with
       caution. 
    */
    
    unsigned int stackptr, fmtstr, tmp;
    char *p;
    char buf[80];
    int index, mod, res, res2;
    FILE *fp;
    int *longp;
    short *shortp;
    int *intp;
    unsigned short *ushortp;
    char *charp;
    unsigned *uintp;
    double *dblp;
    float *fltp, ftmp;
    unsigned int *ulongp;

    stackptr = UREG(STACKPTR);
    tmp = read_word(stackptr);	/* first arg is index to fp */
    fp = file_ptr[tmp];
    stackptr += 4;
    fmtstr = read_word(stackptr); /* second arg is format string */
    p = actual_address(fmtstr);
    stackptr += 4;
    index = 0; res = -1;
    while (*p != EOS)
    {
	/* copy to next conversion code or EOS */
	buf[index] = *p; 
	if (*p == '%')
	{
	    p++; index++;

	    /* handle all special cases -- break on conversion code */
	    if (*p == '*')	/* optional suppression character */
	    {
		buf[index] = *p;
		p++; index++;
	    }
	    while (isdigit((int) *p)) /* optional max field width */
	    {
		buf[index] = *p;
		p++; index++;
	    }
	    if (*p == 'l')	/* optional l or h modifier */
	    {
		buf[index] = *p;
		p++; index++;
		mod = 'l';
	    }
	    else if (*p == 'h')
	    {
		buf[index] = *p;
		p++; index++;
		mod = 'h';
	    }
	    else
		mod = 0;
	    buf[index] = *p;	/* copy next character also */
	    switch (*p)
	    {
	    case '%':		/* just a %% -- continue */
		p++; index++;
		break;
	    case 'd':		/* do integers */
	    case 'o':
	    case 'x':
	    case 'i':
	    case 'n':
		if (mod == 'l')
		{		/* get a long */
		    index++;
		    tmp = read_word(stackptr); stackptr += 4;
		    longp = (int *) actual_address(tmp);
		    buf[index] = EOS;
		    res2 = fscanf(fp, buf, longp);
		    if (res2 == EOF)
		    {		/* out of input */
			if (res == -1)
			{	/* still first real call -- return EOF */
			    SREG(2) = EOF;
			    return;
			}
			else
			{	/* not first call -- return sum */
			    SREG(2) = res;
			    return;
			}
		    }
		    if (res == -1) /* update result sum */
			res = res2;
		    else
			res = res + res2;
		    index = 0; p++;
		    break;
		}
		else if (mod == 'h')
		{		/* get a short */
		    index++;
		    tmp = read_word(stackptr); stackptr += 4;
		    shortp = (short *) actual_address(tmp);
		    buf[index] = EOS;
		    res2 = fscanf(fp, buf, shortp);
		    if (res2 == EOF)
		    {		/* out of input */
			if (res == -1)
			{	/* still first real call -- return EOF */
			    SREG(2) = EOF;
			    return;
			}
			else
			{	/* not first call -- return sum */
			    SREG(2) = res;
			    return;
			}
		    }
		    if (res == -1) /* update result sum */
			res = res2;
		    else
			res = res + res2;
		    index = 0; p++;
		    break;
		}
		else		
		{		/* no modifier -- regular int */
		    index++;
		    tmp = read_word(stackptr); stackptr += 4;
		    intp = (int *) actual_address(tmp);
		    buf[index] = EOS;
		    res2 = fscanf(fp, buf, intp);
		    if (res2 == EOF)
		    {		/* out of input */
			if (res == -1)
			{	/* still first real call -- return EOF */
			    SREG(2) = EOF;
			    return;
			}
			else
			{	/* not first call -- return sum */
			    SREG(2) = res;
			    return;
			}
		    }
		    if (res == -1) /* update result sum */
			res = res2;
		    else
			res = res + res2;
		    index = 0; p++;
		    break;
		}
		break;
	    case 'u':		/* do unsigned ints */
		if (mod == 'l')
		{		/* get a long unsigned */
		    index++;
		    tmp = read_word(stackptr); stackptr += 4;
		    ulongp = (unsigned int *) actual_address(tmp);
		    buf[index] = EOS;
		    res2 = fscanf(fp, buf, ulongp);
		    if (res2 == EOF)
		    {		/* out of input */
			if (res == -1)
			{	/* still first real call -- return EOF */
			    SREG(2) = EOF;
			    return;
			}
			else
			{	/* not first call -- return sum */
			    SREG(2) = res;
			    return;
			}
		    }
		    if (res == -1) /* update result sum */
			res = res2;
		    else
			res = res + res2;
		    index = 0; p++;
		    break;
		}
		else if (mod == 'h')
		{		/* get a short unsigned */
		    index++;
		    tmp = read_word(stackptr); stackptr += 4;
		    ushortp = (unsigned short *) actual_address(tmp);
		    buf[index] = EOS;
		    res2 = fscanf(fp, buf, ushortp);
		    if (res2 == EOF)
		    {		/* out of input */
			if (res == -1)
			{	/* still first real call -- return EOF */
			    SREG(2) = EOF;
			    return;
			}
			else
			{	/* not first call -- return sum */
			    SREG(2) = res;
			    return;
			}
		    }
		    if (res == -1) /* update result sum */
			res = res2;
		    else
			res = res + res2;
		    index = 0; p++;
		    break;
		}
		else
		{		/* get an unsigned int */
		    index++;
		    tmp = read_word(stackptr); stackptr += 4;
		    uintp = (unsigned int *) actual_address(tmp);
		    buf[index] = EOS;
		    res2 = fscanf(fp, buf, uintp);
		    if (res2 == EOF)
		    {		/* out of input */
			if (res == -1)
			{	/* still first real call -- return EOF */
			    SREG(2) = EOF;
			    return;
			}
			else
			{	/* not first call -- return sum */
			    SREG(2) = res;
			    return;
			}
		    }
		    if (res == -1) /* update result sum */
			res = res2;
		    else
			res = res + res2;
		    index = 0; p++;
		    break;
		}
		break;
	    case 'e':		/* do floats & doubles */
	    case 'f':
	    case 'g':
		if (mod == 'l')
		{		/* get a double */
		    index++;
		    tmp = read_word(stackptr); stackptr += 4;
		    dblp = (double *) actual_address(tmp);
		    buf[index] = EOS;
		    res2 = fscanf(fp, buf, dblp);
		    if (res2 == EOF)
		    {		/* out of input */
			if (res == -1)
			{	/* still first real call -- return EOF */
			    SREG(2) = EOF;
			    return;
			}
			else
			{	/* not first call -- return sum */
			    SREG(2) = res;
			    return;
			}
		    }
		    if (res == -1) /* update result sum */
			res = res2;
		    else
			res = res + res2;
		    index = 0; p++;
		    break;
		}
		else
		{		/* get a float */
		    index++;

		    tmp = read_word(stackptr); stackptr += 4;
		    buf[index] = EOS;
		    res2 = fscanf(fp, buf, &ftmp);
		    write_word(*((unsigned int *) &ftmp), tmp); 

		    /* old version 
		    tmp = read_word(stackptr); stackptr += 4;
		    fltp = (float *) actual_address(tmp);
		    buf[index] = EOS;
		    res2 = fscanf(fp, buf, fltp);
		    to here */

		    if (res2 == EOF)
		    {		/* out of input */
			if (res == -1)
			{	/* still first real call -- return EOF */
			    SREG(2) = EOF;
			    return;
			}
			else
			{	/* not first call -- return sum */
			    SREG(2) = res;
			    return;
			}
		    }
		    if (res == -1) /* update result sum */
			res = res2;
		    else
			res = res + res2;
		    index = 0; p++;
		    break;
		}
		break;
	    case 's':		/* do strings */
		index++;
		tmp = read_word(stackptr); stackptr += 4;
		charp = actual_address(tmp);
		buf[index] = EOS;
		res2 = fscanf(fp, buf, charp);
		if (res2 == EOF)
		{		/* out of input */
		    if (res == -1)
		    {	/* still first real call -- return EOF */
			SREG(2) = EOF;
			return;
		    }
		    else
		    {	/* not first call -- return sum */
			SREG(2) = res;
			return;
		    }
		}
		if (res == -1) /* update result sum */
		    res = res2;
		else
		    res = res + res2;
		index = 0; p++;
		break;
	    case '[':		/* do scan set string specifier */
		run_error("scanset input in fscanf unimplemented\n");
		return;
	    case 'c':		/* do chars */
		index++;
		tmp = read_word(stackptr); stackptr += 4;
		charp = actual_address(tmp);
		buf[index] = EOS;
		res2 = fscanf(fp, buf, charp);
		if (res2 == EOF)
		{		/* out of input */
		    if (res == -1)
		    {	/* still first real call -- return EOF */
			SREG(2) = EOF;
			return;
		    }
		    else
		    {	/* not first call -- return sum */
			SREG(2) = res;
			return;
		    }
		}
		if (res == -1) /* update result sum */
		    res = res2;
		else
		    res = res + res2;
		index = 0; p++;
		break;
	    default:
		run_error("ill-formed format string in fscanf call\n");
		return;
	    }
	}
	else
	{
	    p++; index++;
	}
    }
    /* make a last call to fscanf() if buf has anything it in */
}

void do_feof(void)			/* trap #9 */
{
    unsigned int stackptr;
    unsigned int arg1;

    stackptr = UREG(STACKPTR);
    arg1 = read_word(stackptr);
    SREG(2) = feof(file_ptr[arg1]);
}

void do_malloc(void)		/* trap #10 */
{
    unsigned int stackptr, size;

    stackptr = UREG(STACKPTR);
    /* make sure code_size is aligned on double boundary first */
    while (code_size & 0x7)
	code_size++;
    size = read_word(stackptr);	/* argument in size in bytes */
    if ((code_size + size) > code_size_act)
    {				/* out of memory -- return NULL */
	UREG(2) = (unsigned) 0;
	return;
    }
    else
    {				/* enough memory is left */
	UREG(2) = code_size;
	code_size = code_size + size;
    }
}

void do_sprintf(void)		/* trap #11 */
{
    /* just like do_printf except first argument is a pointer to the
       string that the output will be formatted into.  2nd argument is
       a pointer to format string.  Must march through string, looking
       for each argument, peeling off stack as each is encountered. */
    
    char buf[256], *p, *s;
    int in_arg;			/* 1 if between % and end of arg */
    int buf_index;
    int stmp;
    unsigned int stackptr, fmt_index, str_index;
    
    unsigned int temp;
    char *ctemp;
    double dtmp;
    
    stackptr = UREG(STACKPTR);

    /* get address of string to hold result */
    str_index = read_word(stackptr);

    /* update stack pointer to next entry */
    stackptr += 4;
    
    /* get address of format string */
    fmt_index = read_word(stackptr);
    
    /* update pointer to next argument */
    stackptr += 4;
    
    /* start out not in argument */
    in_arg = 0;
    buf_index = 0;

    /* init a character pointer to march through the format string */
    p = actual_address(fmt_index);

    /* init a char ptr to march through the output string */
    s = actual_address(str_index);
    
    for ( ; *p != EOS ; p++)
    {
	if (in_arg) /* in arg: copy to terminating char */
	{
	    switch (*p)
	    {
	      case '%':		/* escape if prev was % also: %% is  */
		if (*(p-1) == '%')  /* just one % in printf */
		{
		    in_arg = 0;
		    stmp = sprintf(s, "%c", *p);
		    s += stmp;
		}
		else		/* a bad format string: continue */
		    buf[buf_index++] = *p;
		break;
	      case 'd':		/* handle signed ints */
	      case 'i': 
		buf[buf_index++] = *p;
		buf[buf_index] = EOS;
		temp = read_word(stackptr);
		stackptr += 4;
		stmp = sprintf(s, buf,(int) temp);
		s += stmp;
		in_arg = 0;
		break;
		
	      case 'o':		/* handle unsigned ints */
	      case 'x':
	      case 'X':
	      case 'u':
	      case 'c':
	      case 'p':
		buf[buf_index++] = *p;
		buf[buf_index] = EOS;
		temp = read_word(stackptr);
		stackptr += 4;
		stmp = sprintf(s, buf,temp);
		s += stmp;
		in_arg = 0;
		break;
		
	      case 's':		/* handle string */
		buf[buf_index++] = *p;
		buf[buf_index] = EOS;
		temp = read_word(stackptr);
		stackptr += 4;
		ctemp = actual_address(temp);
		if ( ctemp != NULL )
		{
		    stmp = sprintf(s, buf, ctemp);
		    s += stmp;
		}
		in_arg = 0;
		break;
		
	      case 'f':		/* handle fp args (all doubles) */
	      case 'e':
	      case 'E':
	      case 'g':
	      case 'G':
		buf[buf_index++] = *p;
		buf[buf_index] = EOS;
		if (stackptr & 0x04)
		    stackptr += 4;
		dtmp = read_double(stackptr);
		stackptr += 8;
		stmp = sprintf(s, buf, dtmp);
		s += stmp;
		in_arg = 0;
		break;
		
	      default:		/* still in arg: just copy */
		buf[buf_index++] = *p;
		break;
	    }
	}
	else                    /* in_arg == 0 */
	{		        /* (not in % arg) */
	    if (*p == '%')	/* start of % arg */
	    {
		in_arg = 1;
		buf_index = 0;	/* start copying to buf */
		buf[buf_index++] = *p;
	    }
	    else		/* just output */
	    {
		stmp = sprintf(s, "%c", *p);
		s += stmp;
	    }
	}
    }				/* end for */
}
