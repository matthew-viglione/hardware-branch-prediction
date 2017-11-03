/* Copyright (c) 2000 James Archibald, Brigham Young University */ 

#define DF2      1
#define DREG2a   3
#define DREG2b   5
#define DREG3    7
#define FD2      9
#define FI2     11
#define FREG2a  13
#define FREG2b  15
#define FREG3   17
#define IF2     19
#define IMM1    21
#define IREG1   23
#define LEXP16  25
#define LEXP26  27
#define LOADD   29
#define LOADF   31
#define LOADI   33
#define NONEOP  35
#define REG1IMM 37
#define REG2IMM 39
#define REG3IMM 41
#define REGLAB  43
#define STRD    45
#define STRF    47
#define STRI    49
#define UNIMP   51
#define PSEUDO  70

struct mapper
{
    char *name;
    int op;
    int func;
    int optype;
    unsigned long count; /* dynamic instruction count for each */
    int funitmask;	 /* indicates which funits can handle this inst */
};

#define MAIN_MAX 46
#define SPEC_MAX 27
#define FPOPS_MAX 30

#ifdef MAINSEG 
struct mapper mainops[MAIN_MAX] = 
{
    {"special", 0x00, 0x00, UNIMP},
    {"fparith", 0x01, 0x00, UNIMP}, 
    {"addi",    0x02, 0x00, REG2IMM},
    {"addui",   0x03, 0x00, REG2IMM},
    {"andi",    0x04, 0x00, REG2IMM},
    {"beqz",    0x05, 0x00, REGLAB},
    {"bfpf",    0x06, 0x00, LEXP16},
    {"bfpt",    0x07, 0x00, LEXP16},
    {"bnez",    0x08, 0x00, REGLAB},
    {"j",       0x09, 0x00, LEXP26},
    {"jal",     0x0a, 0x00, LEXP26},
    {"jalr",    0x0b, 0x00, IREG1},
    {"jr",      0x0c, 0x00, IREG1},
    {"lb",      0x0d, 0x00, LOADI},
    {"lbu",     0x0e, 0x00, LOADI},
    {"ld",      0x0f, 0x00, LOADD},
    {"lf",      0x10, 0x00, LOADF},
    {"lh",      0x11, 0x00, LOADI},
    {"lhi",     0x12, 0x00, REG1IMM},
    {"lhu",     0x13, 0x00, LOADI},
    {"lw",      0x14, 0x00, LOADI},
    {"ori",     0x15, 0x00, REG2IMM},
    {"rfe",     0x16, 0x00, UNIMP},
    {"sb",      0x17, 0x00, STRI},
    {"sd",      0x18, 0x00, STRD},
    {"seqi",    0x19, 0x00, REG2IMM},
    {"sf",      0x1a, 0x00, STRF},
    {"sgei",    0x1b, 0x00, REG2IMM},
    {"sgeui",   0x1c, 0x00, REG2IMM}, /* added instruction */
    {"sgti",    0x1d, 0x00, REG2IMM},
    {"sgtui",   0x1e, 0x00, REG2IMM}, /* added instruction */
    {"sh",      0x1f, 0x00, STRI},
    {"slei",    0x20, 0x00, REG2IMM},
    {"sleui",   0x21, 0x00, REG2IMM}, /* added instruction */
    {"slli",    0x22, 0x00, REG2IMM},
    {"slti",    0x23, 0x00, REG2IMM},
    {"sltui",   0x24, 0x00, REG2IMM}, /* added instruction */
    {"snei",    0x25, 0x00, REG2IMM},
    {"srai",    0x26, 0x00, REG2IMM},
    {"srli",    0x27, 0x00, REG2IMM},
    {"subi",    0x28, 0x00, REG2IMM},
    {"subui",   0x29, 0x00, REG2IMM},
    {"sw",      0x2a, 0x00, STRI},
    {"trap",    0x2b, 0x00, IMM1},
    {"xori",    0x2c, 0x00, REG2IMM},
    {"la",      0x30, 0x00, PSEUDO}
};

struct mapper spec[SPEC_MAX] = 
{
    {"nop",     0x00, 0x00, NONEOP},
    {"add",     0x00, 0x01, REG3IMM},
    {"addu",    0x00, 0x02, REG3IMM},
    {"and",     0x00, 0x03, REG3IMM},
    {"movd",    0x00, 0x04, DREG2a},
    {"movf",    0x00, 0x05, FREG2a},
    {"movfp2i", 0x00, 0x06, IF2},
    {"movi2fp", 0x00, 0x07, FI2},
    {"movi2s",  0x00, 0x08, UNIMP},
    {"movs2i",  0x00, 0x09, UNIMP},
    {"or",      0x00, 0x0a, REG3IMM},
    {"seq",     0x00, 0x0b, REG3IMM},
    {"sge",     0x00, 0x0c, REG3IMM},
    {"sgeu",    0x00, 0x0d, REG3IMM}, /* added instruction */
    {"sgt",     0x00, 0x0e, REG3IMM},
    {"sgtu",    0x00, 0x0f, REG3IMM}, /* added instruction */
    {"sle",     0x00, 0x10, REG3IMM},
    {"sleu",    0x00, 0x11, REG3IMM}, /* added instruction */
    {"sll",     0x00, 0x12, REG3IMM},
    {"slt",     0x00, 0x13, REG3IMM},
    {"sltu",    0x00, 0x14, REG3IMM}, /* added instruction */
    {"sne",     0x00, 0x15, REG3IMM},
    {"sra",     0x00, 0x16, REG3IMM},
    {"srl",     0x00, 0x17, REG3IMM},
    {"sub",     0x00, 0x18, REG3IMM},
    {"subu",    0x00, 0x19, REG3IMM},
    {"xor",     0x00, 0x1a, REG3IMM}
};

struct mapper fpops[FPOPS_MAX] = 
{
    {"addd",   0x01, 0x00, DREG3},
    {"addf",   0x01, 0x01, FREG3},
    {"cvtd2f", 0x01, 0x02, FD2},
    {"cvtd2i", 0x01, 0x03, FD2},
    {"cvtf2d", 0x01, 0x04, DF2},
    {"cvtf2i", 0x01, 0x05, FREG2a},
    {"cvti2d", 0x01, 0x06, DF2},
    {"cvti2f", 0x01, 0x07, FREG2a},
    {"div",    0x01, 0x08, FREG3},
    {"divd",   0x01, 0x09, DREG3},
    {"divf",   0x01, 0x0a, FREG3},
    {"divu",   0x01, 0x0b, FREG3},
    {"eqd",    0x01, 0x0c, DREG2b},
    {"eqf",    0x01, 0x0d, FREG2b},
    {"ged",    0x01, 0x0e, DREG2b},
    {"gef",    0x01, 0x0f, FREG2b},
    {"gtd",    0x01, 0x10, DREG2b},
    {"gtf",    0x01, 0x11, FREG2b},
    {"led",    0x01, 0x12, DREG2b},
    {"lef",    0x01, 0x13, FREG2b},
    {"ltd",    0x01, 0x14, DREG2b},
    {"ltf",    0x01, 0x15, FREG2b},
    {"mult",   0x01, 0x16, FREG3},
    {"multd",  0x01, 0x17, DREG3},
    {"multf",  0x01, 0x18, FREG3},
    {"multu",  0x01, 0x19, FREG3},
    {"ned",    0x01, 0x1a, DREG2b},
    {"nef",    0x01, 0x1b, FREG2b},
    {"subd",   0x01, 0x1c, DREG3},
    {"subf",   0x01, 0x1d, FREG3}
};

#endif

#ifndef MAINSEG
extern struct mapper mainops[];
extern struct mapper spec[];
extern struct mapper fpops[];
#endif
