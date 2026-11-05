#include <stdio.h>
#include <stdint.h>
#include <signal.h>
/* unix only */
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

enum
{
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC, 
    R_COND,
    R_COUNT
};
enum
{
    OP_BR = 0, /* branch */
    OP_ADD,    /* add  */
    OP_LD,     /* load */
    OP_ST,     /* store */
    OP_JSR,    /* jump to label (JSR) or jump to address contained in register (JSSR) */
    OP_AND,    /* bitwise and */
    OP_LDR,    /* load register (add offset6 to base register to find address of data to be loaded into DR */
    OP_STR,    /* store register */
    OP_RTI,    /* unused */
    OP_NOT,    /* bitwise not */
    OP_LDI,    /* load indirect (add PCOffset9 to PC to find address of data to be loaded into DR)*/
    OP_STI,    /* store indirect (add PCOffset9 to PC to find address where data will be stored)*/
    OP_JMP,    /* jump */
    OP_RES,    /* reserved (unused) */
    OP_LEA,    /* load effective address (store address into register)*/
    OP_TRAP    /* execute trap */
};
enum
{
    FL_POS = 1 << 0, /* P */
    FL_ZRO = 1 << 1, /* Z */
    FL_NEG = 1 << 2, /* N */
};
enum
{
    MR_KBSR = 0xFE00, /* keyboard status */
    MR_KBDR = 0xFE02  /* keyboard data */
};
enum
{
    TRAP_GETC = 0x20,  /* get character from keyboard, not echoed onto the terminal */
    TRAP_OUT = 0x21,   /* output a character */
    TRAP_PUTS = 0x22,  /* output a word string */
    TRAP_IN = 0x23,    /* get character from keyboard, echoed onto the terminal */
    TRAP_PUTSP = 0x24, /* output a byte string */
    TRAP_HALT = 0x25   /* halt the program */
};

#define MEMORY_MAX (1 << 16)
uint16_t memory[MEMORY_MAX];  /* 65536 locations */
uint16_t reg[R_COUNT];

const char* getRegister(uint16_t x)
{
	switch(x)
	{
		case 0:{
			return "Register R0";
		}
		break;
		case 1:{
			return "Register R1";
		}
		break;
		case 2:{
			return "Register R2";
		}
		break;
		case 3:{
			return "Register R3";
		}
		break;
		case 4:{
			return "Register R4";
		}
		break;
		case 5:{
			return "Register R5";
		}
		break;
		case 6:{
			return "Register R6";
		}
		break;
		case 7:{
			return "Register R7";
		}
		break;
		case 8:{
			return "Program Counter";
		}
		break;
	}
}
uint16_t sign_extend(uint16_t x, int bit_count)
{
    if ((x >> (bit_count - 1)) & 1) {
        x |= (0xFFFF << bit_count);
    }
    return x;
}
uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}
void update_flags(uint16_t r)
{
    if (reg[r] == 0)
    {
        reg[R_COND] = FL_ZRO;
    }
    else if (reg[r] >> 15) /* a 1 in the left-most bit indicates negative */
    {
        reg[R_COND] = FL_NEG;
    }
    else
    {
        reg[R_COND] = FL_POS;
    }
}
/*const char* getFlag(uint16_t x)
{
		if (reg[x] == 0)
		{
			return "Zero flag is set.\n";
		}
		else if (reg[x] >> 15)
		{
			return "Negative flag is set.\n";
		}
		else
		{
			return "Positive flag is set.\n";
		}
	
}*/
void read_image_file(FILE* file)
{
    /* the origin tells us where in memory to place the image */
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);
    printf(".ORIG x3000 is the first line in every program, where x3000 is the memory location (specified by the user) that the program should start from. For this program we will use .ORIG 0x%04x\n",origin);

    /* we know the maximum file size so we only need one fread */
    uint16_t max_read = MEMORY_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    /* swap to little endian */
    while (read-- > 0)
    {
        *p = swap16(*p);
        ++p;
    }
}
int read_image(const char* image_path)
{
    FILE* file = fopen(image_path, "rb");
    if (!file) { return 0; };
    read_image_file(file);
    fclose(file);
    return 1;
}
void mem_write(uint16_t address, uint16_t val)
{
    memory[address] = val;
}

uint16_t check_key()
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

uint16_t mem_read(uint16_t address)
{
    if (address == MR_KBSR)
    {
        if (check_key())
        {
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        }
        else
        {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}
struct termios original_tio;

void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

int main(int argc, const char* argv[])
{
    if (argc < 2)
    {
        /* show usage string */
        printf("lc3 [image-file1] ...\n");
        exit(2);
    }
    
    for (int j = 1; j < argc; ++j)
    {
        if (!read_image(argv[j]))
        {
            printf("failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    /* since exactly one condition flag should be set at any given time, set the Z flag */
    reg[R_COND] = FL_ZRO;

    /* set the PC to starting position */
    /* 0x3000 is the default */
    enum { PC_START = 0x3000 };
    reg[R_PC] = PC_START;
    
        
    int running = 1;
    while (running)
    {
        
    printf("Do you want to continue to the next operation? (y/n) \n");
    char cont = getchar();
    char x = 'y';
    char z = 'n';
    printf("%c\n",cont);
    if (cont == z)
    {
    	exit(6);
    }
        /* FETCH */
        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = instr >> 12;

        switch (op)
        {
            case OP_ADD:
                {
                    printf("The current operation is an ADD operation.\nThe assembler formats are: \nADD DR,SR1,SR2 example:ADD R2,R3,R4 :R2 <- R3 + R4.\nADD DR,DR1,imm5 example: ADD R2,R3,#7 :R2 <- R3 + 7\nDR is our destination register, SR stands for source register and imm5 is a 5 bit value especially useful for operations like incrementing.\nProgram counter is at memory location 0x%04x.\n",reg[R_PC]);
                    /* destination register (DR) */
                    uint16_t r0 = (instr >> 9) & 0x7;
                    printf("Destination register is %s.\n",getRegister(r0));
                    /* first operand (SR1) */
                    uint16_t r1 = (instr >> 6) & 0x7;
                    printf("First operand is in %s and it's value is %d\n",getRegister(r1),(int16_t)reg[r1]);
                    /* whether we are in immediate mode */
                    uint16_t imm_flag = (instr >> 5) & 0x1;
                    if (imm_flag)
                    {
                        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                        printf("The second operand is an immediate value %d.\n",(int16_t)imm5);
                        printf("%d is added to %d to get ",(int16_t)imm5,(int16_t)reg[r1]);
                        reg[r0] = reg[r1] + imm5;
              		printf("%d and the result is stored in %s.\n",(int16_t)reg[r0],getRegister(r0));
                    }
                    else
                    {
                        uint16_t r2 = instr & 0x7;
                        printf("The second operand is in %s, containing the value %d.\n",getRegister(r2),(int16_t)reg[r2]);
                        printf("%d is added to %d to get ",(int16_t)reg[r2],(int16_t)reg[r1]);
                        reg[r0] = reg[r1] + reg[r2];
                        printf("%d and the result is stored in %s.\n",(int16_t)reg[r0],getRegister(r0));
                    }
                    update_flags(r0);
                    /*getFlag(r0);*/
                }
                break;
            case OP_AND:
                {
                    printf("The current operation is an AND operation.\nThe assembler formats are: \nAND DR,SR1,SR2 example:AND R2,R3,R4 :R2 <- R3 & R4.\nAND DR,DR1,imm5 example: AND R2,R3,#7 :R2 <- R3 & 7\nDR is our destination register, SR stands for source register and imm5 is a 5 bit value especially useful for operations like incrementing.\nProgram counter is at memory location 0x%04x.\n",reg[R_PC]);
                    uint16_t r0 = (instr >> 9) & 0x7;
                    printf("Destination register, where the result will be stored is %s.\n",getRegister(r0));
                    uint16_t r1 = (instr >> 6) & 0x7;
                    printf("First operand is in %s and it's value is %d\n",getRegister(r1),(int16_t)reg[r1]);
                    uint16_t imm_flag = (instr >> 5) & 0x1;
                    if (imm_flag)
                    {
                        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                        printf("The second operand is an immediate value %d.\n",(int16_t)imm5);
                        printf("%d is added to %d to get ",(int16_t)imm5,(int16_t)reg[r1]);
                        reg[r0] = reg[r1] & imm5;
                        printf("%d and the result is stored in %s.\n",(int16_t)reg[r0],getRegister(r0));
                    }
                    else
                    {
                        uint16_t r2 = instr & 0x7;
                        printf("The second operand is in %s, containing the value %d.\n",getRegister(r2),(int16_t)reg[r2]);
                        printf("A bitwise and is performed on %d and %d to get ",(int16_t)reg[r2],(int16_t)reg[r1]);
                        reg[r0] = reg[r1] & reg[r2];
                        printf("%d and the result is stored in %s.\n",(int16_t)reg[r0],getRegister(r0));
                    }
                    update_flags(r0);
                    /*getFlag(r0);*/
                }
                break;
            case OP_NOT:
                {
                    printf("NOT operation.\nProgram counter is at 0x%04x.\n",reg[R_PC]);
                    uint16_t r0 = (instr >> 9) & 0x7;
                    printf("Destination register is %s.\n",getRegister(r0));
                    uint16_t r1 = (instr >> 6) & 0x7;
                    printf("A bitwise not is performed on %d in %s to get ",(int16_t)reg[r1],getRegister(r1));
                    reg[r0] = ~reg[r1];
                    printf("%d.\n",(int16_t)reg[r0]);
                    update_flags(r0);
                    /*getFlag(r0);*/
                }
                break;
            case OP_BR:
                {
                    /*printf("BRANCH operation.\nProgram counter is at 0x%04x.\n",reg[R_PC]);*/
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    /*printf("Program counter offset is %d.\n",(int16_t)pc_offset);*/
                    uint16_t cond_flag = (instr >> 9) & 0x7;
                    if (cond_flag & reg[R_COND])
                    {
                        reg[R_PC] += pc_offset;
                        /*printf("Condition flag is set, branching to 0x%04x.\n",reg[R_PC]);*/
                    }
                }
                break;
            case OP_JMP:
                {
                    /* Also handles RET */
                    uint16_t r1 = (instr >> 6) & 0x7;
                    reg[R_PC] = reg[r1];
                }
                break;
            case OP_JSR:
                {
                    uint16_t long_flag = (instr >> 11) & 1;
                    reg[R_R7] = reg[R_PC];
                    if (long_flag)
                    {
                        uint16_t long_pc_offset = sign_extend(instr & 0x7FF, 11);
                        reg[R_PC] += long_pc_offset;  /* JSR */
                    }
                    else
                    {
                        uint16_t r1 = (instr >> 6) & 0x7;
                        reg[R_PC] = reg[r1]; /* JSRR */
                    }
                }
                break;
            case OP_LD:
                {
                    printf("The current operation is a LOAD operation:\nThe assembler formats are:\nLD DR,LABEL :Contents in memory location label are loaded into DR(Destination register)\nProgram counter is at memory location 0x%04x.\n",reg[R_PC]);
                    uint16_t r0 = (instr >> 9) & 0x7;
                    printf("Destination register is %s and contains the value %d.\n",getRegister(r0),(int16_t)reg[r0]);
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    printf("Value to be loaded is at memory location 0x%04x and contains the value ",(reg[R_PC] + pc_offset));
                    reg[r0] = mem_read(reg[R_PC] + pc_offset);
                    printf("%d.\n",(int16_t)reg[r0]);
                    printf("Value in %s is now %d.\n",getRegister(r0),(int16_t)reg[r0]);
                    update_flags(r0);
                    /*getFlag(r0);*/
                }
                break;
            case OP_LDI:
                {
                    /* destination register (DR) */
                    uint16_t r0 = (instr >> 9) & 0x7;
                    /* PCoffset 9*/
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    /* add pc_offset to the current PC, look at that memory location to get the final address */
                    reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
                    update_flags(r0);
                }
                break;
            case OP_LDR:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t r1 = (instr >> 6) & 0x7;
                    uint16_t offset = sign_extend(instr & 0x3F, 6);
                    reg[r0] = mem_read(reg[r1] + offset);
                    update_flags(r0);
                }
                break;
            case OP_LEA:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    reg[r0] = reg[R_PC] + pc_offset;
                    update_flags(r0);
                }
                break;
            case OP_ST:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    mem_write(reg[R_PC] + pc_offset, reg[r0]);
                }
                break;
            case OP_STI:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                    mem_write(mem_read(reg[R_PC] + pc_offset), reg[r0]);
                }
                break;
            case OP_STR:
                {
                    uint16_t r0 = (instr >> 9) & 0x7;
                    uint16_t r1 = (instr >> 6) & 0x7;
                    uint16_t offset = sign_extend(instr & 0x3F, 6);
                    mem_write(reg[r1] + offset, reg[r0]);
                }
                break;
            case OP_TRAP:
                reg[R_R7] = reg[R_PC];
                
                switch (instr & 0xFF)
                {
                    case TRAP_GETC:
                        /* read a single ASCII char */
                        reg[R_R0] = (uint16_t)getchar();
                        update_flags(R_R0);
                        break;
                    case TRAP_OUT:
                        putc((char)reg[R_R0], stdout);
                        fflush(stdout);
                        break;
                    case TRAP_PUTS:
                        {
                            /* one char per word */
                            uint16_t* c = memory + reg[R_R0];
                            while (*c)
                            {
                                putc((char)*c, stdout);
                                ++c;
                            }
                            fflush(stdout);
                        }
                        break;
                    case TRAP_IN:
                        {
                            printf("Enter a character: ");
                            char c = getchar();
                            putc(c, stdout);
                            fflush(stdout);
                            reg[R_R0] = (uint16_t)c;
                            update_flags(R_R0);
                        }
                        break;
                    case TRAP_PUTSP:
                        {
                            /* one char per byte (two bytes per word)
                               here we need to swap back to
                               big endian format */
                            uint16_t* c = memory + reg[R_R0];
                            while (*c)
                            {
                                char char1 = (*c) & 0xFF;
                                putc(char1, stdout);
                                char char2 = (*c) >> 8;
                                if (char2) putc(char2, stdout);
                                ++c;
                            }
                            fflush(stdout);
                        }
                        break;
                    case TRAP_HALT:
                        puts("HALT TRAP, program ends.");
                        fflush(stdout);
                        running = 0;
                        break;
                }
                break;
            case OP_RES:
            case OP_RTI:
            default:
                abort();
                break;
        }
    }
    restore_input_buffering();
}
