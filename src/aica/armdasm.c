/*
 * armdasm.c    21 Aug 2004  - ARM7tdmi (ARMv4) disassembler
 *
 * Copyright (c) 2004 Nathan Keynes. Distribution and modification permitted
 * under the terms of the GNU General Public License version 2 or later.
 */

#include "aica/armcore.h"
#include <stdlib.h>

#define COND(ir) (ir>>28)
#define OPCODE(ir) ((ir>>20)&0x1F)
#define GRP(ir) ((ir>>26)&0x03)
#define IFLAG(ir) (ir&0x02000000)
#define SFLAG(ir) (ir&0x00100000)
#define PFLAG(ir) (ir&0x01000000)
#define UFLAG(ir) (ir&0x00800000)
#define BFLAG(ir) (ir&0x00400000)
#define WFLAG(ir) (ir&0x00200000)
#define LFLAG(ir) SFLAG(ir)
#define RN(ir) ((ir>>16)&0x0F)
#define RD(ir) ((ir>>12)&0x0F)
#define RS(ir) ((ir>>8)&0x0F)
#define RM(ir) (ir&0x0F)

#define IMM8(ir) (ir&0xFF)
#define IMM12(ir) (ir&0xFFF)
#define SHIFTIMM(ir) ((ir>>7)&0x1F)
#define IMMROT(ir) ((ir>>7)&0x1E)
#define SHIFT(ir) ((ir>>4)&0x07)
#define DISP24(ir) ((ir&0x00FFFFFF))
#define FSXC(ir) msrFieldMask[RN(ir)]
#define ROTIMM12(ir) ROTATE_RIGHT_LONG(IMM8(ir),IMMROT(ir))

char *conditionNames[] = { "EQ", "NE", "CS", "CC", "MI", "PL", "VS", "VC", 
                           "HI", "LS", "GE", "LT", "GT", "LE", "  " /*AL*/, "NV" };
                           
                         /* fsxc */
char *msrFieldMask[] = { "", "c", "x", "xc", "s", "sc", "sx", "sxc",
	                     "f", "fc", "fx", "fxc", "fs", "fsc", "fsx", "fsxc" };
char *ldmModes[] = { "DA", "IA", "DB", "IB" };

#define UNIMP(ir) snprintf( buf, len, "???     " )

int arm_disasm_shift_operand( uint32_t ir, char *buf, int len )
{
	uint32_t operand, tmp;
	if( IFLAG(ir) == 0 ) {
		switch(SHIFT(ir)) {
		case 0: /* (Rm << imm) */
			return snprintf(buf, len, "R%d << %d", RM(ir), SHIFTIMM(ir) );
		case 1: /* (Rm << Rs) */
			return snprintf(buf, len, "R%d << R%d", RM(ir), RS(ir) );
		case 2: /* (Rm >> imm) */
			return snprintf(buf, len, "R%d >> %d", RM(ir), SHIFTIMM(ir) );
		case 3: /* (Rm >> Rs) */
			return snprintf(buf, len, "R%d >> R%d", RM(ir), RS(ir) );
		case 4: /* (Rm >>> imm) */
			return snprintf(buf, len, "R%d >>> %d", RM(ir), SHIFTIMM(ir) );
		case 5: /* (Rm >>> Rs) */
			return snprintf(buf, len, "R%d >>> R%d", RM(ir), RS(ir) );
		case 6:
			tmp = SHIFTIMM(ir);
			if( tmp == 0 ) /* RRX aka rotate with carry */
				return snprintf(buf, len, "R%d roc 1", RM(ir) );
			else
				return snprintf(buf, len, "R%d rot %d", RM(ir), SHIFTIMM(ir) );
		case 7:
			return snprintf(buf, len, "R%d rot R%d", RM(ir), RS(ir) );
		}
	} else {
		operand = IMM8(ir);
		tmp = IMMROT(ir);
		operand = ROTATE_RIGHT_LONG(operand, tmp);
		return snprintf(buf, len, "%08X", operand );
	}
}

static int arm_disasm_address_index( uint32_t ir, char *buf, int len )
{
	uint32_t tmp;
	
	switch(SHIFT(ir)) {
	case 0: /* (Rm << imm) */
		return snprintf( buf, len, "R%d << %d", RM(ir), SHIFTIMM(ir) );
	case 2: /* (Rm >> imm) */
		return snprintf( buf, len, "R%d >> %d", RM(ir), SHIFTIMM(ir) );
	case 4: /* (Rm >>> imm) */
		return snprintf( buf, len, "R%d >>> %d", RM(ir), SHIFTIMM(ir) );
	case 6:
		tmp = SHIFTIMM(ir);
		if( tmp == 0 ) /* RRX aka rotate with carry */
			return snprintf( buf, len, "R%d roc 1", RM(ir) );
		else
			return snprintf( buf, len, "R%d rot %d", RM(ir), tmp );
	default: 
		return UNIMP(ir);
	}
}

static int arm_disasm_address_operand( uint32_t ir, char *buf, int len )
{
    char  shift[32];

	char sign = UFLAG(ir) ? '-' : '+';
	/* I P U . W */
	switch( (ir>>21)&0x19 ) {
	case 0: /* Rn -= imm offset (post-indexed) [5.2.8 A5-28] */
	case 1:
		return snprintf( buf, len, "[R%d], R%d %c= %04X", RN(ir), RN(ir), sign, IMM12(ir) );
	case 8: /* Rn - imm offset  [5.2.2 A5-20] */
		return snprintf( buf, len, "[R%d %c %04X]", RN(ir), sign, IMM12(ir) );
	case 9: /* Rn -= imm offset (pre-indexed)  [5.2.5 A5-24] */
		return snprintf( buf, len, "[R%d %c= %04X]", RN(ir), sign, IMM12(ir) );
	case 16: /* Rn -= Rm (post-indexed)  [5.2.10 A5-32 ] */
	case 17:
		arm_disasm_address_index( ir, shift, sizeof(shift) );
		return snprintf( buf, len, "[R%d], R%d %c= %s", RN(ir), RN(ir), sign, shift );
	case 24: /* Rn - Rm  [5.2.4 A5-23] */
		arm_disasm_address_index( ir, shift, sizeof(shift) );
		return snprintf( buf, len, "[R%d %c %s]", RN(ir), sign, shift );
	case 25: /* RN -= Rm (pre-indexed)  [5.2.7 A5-26] */
		arm_disasm_address_index( ir, shift, sizeof(shift) );
		return snprintf( buf, len, "[R%d %c= %s]", RN(ir), sign, shift );
	default:
		return UNIMP(ir); /* Unreachable */
	}
}

int arm_disasm_instruction( uint32_t pc, char *buf, int len )
{
	char operand[32];
    uint32_t ir = arm_mem_read_long(pc);
	int i,j;
	
    if( COND(ir) == 0x0F ) {
    	UNIMP(ir);
    	return pc+4;
    }
    char *cond = conditionNames[COND(ir)];

	switch( GRP(ir) ) {
	case 0:
		if( (ir & 0x0D900000) == 0x01000000 ) {
			/* Instructions that aren't actual data processing */
			switch( ir & 0x0FF000F0 ) {
			case 0x01200010: /* BXcc */
				snprintf(buf, len, "BX%s     R%d", cond, RM(ir));
				break;
			case 0x01000000: /* MRS Rd, CPSR */
				snprintf(buf, len, "MRS%s    R%d, CPSR", cond, RD(ir));
				break;
			case 0x01400000: /* MRS Rd, SPSR */
				snprintf(buf, len, "MRS%s    R%d, SPSR", cond, RD(ir));
				break;
			case 0x01200000: /* MSR CPSR, Rm */
				snprintf(buf, len, "MSR%s    CPSR_%s, R%d", cond, FSXC(ir), RM(ir));
				break;
			case 0x01600000: /* MSR SPSR, Rm */
				snprintf(buf, len, "MSR%s    SPSR_%s, R%d", cond, FSXC(ir), RM(ir));
				break;
			case 0x03200000: /* MSR CPSR, imm */
				snprintf(buf, len, "MSR%s    CPSR_%s, #%08X", cond, FSXC(ir), ROTIMM12(ir));
				break;
			case 0x03600000: /* MSR SPSR, imm */
				snprintf(buf, len, "MSR%s    SPSR_%s, #%08X", cond, FSXC(ir), ROTIMM12(ir));
				break;
			default:
				UNIMP();
			}
		} else if( (ir & 0x0E000090) == 0x00000090 ) {
			/* Neither are these */
			switch( (ir>>5)&0x03 ) {
			case 0:
				/* Arithmetic extension area */
				switch(OPCODE(ir)) {
				case 0: /* MUL */
					snprintf(buf,len, "MUL%s    R%d, R%d, R%d", cond, RN(ir), RM(ir), RS(ir) );
					break;
				case 1: /* MULS */
					break;
				case 2: /* MLA */
					snprintf(buf,len, "MLA%s    R%d, R%d, R%d, R%d", cond, RN(ir), RM(ir), RS(ir), RD(ir) );
					break;
				case 3: /* MLAS */
					break;
				case 8: /* UMULL */
					snprintf(buf,len, "UMULL%s  R%d, R%d, R%d, R%d", cond, RD(ir), RN(ir), RM(ir), RS(ir) );
					break;
				case 9: /* UMULLS */
					break;
				case 10: /* UMLAL */
					snprintf(buf,len, "UMLAL%s  R%d, R%d, R%d, R%d", cond, RD(ir), RN(ir), RM(ir), RS(ir) );
					break;
				case 11: /* UMLALS */
					break;
				case 12: /* SMULL */
					snprintf(buf,len, "SMULL%s  R%d, R%d, R%d, R%d", cond, RD(ir), RN(ir), RM(ir), RS(ir) );
					break;
				case 13: /* SMULLS */
					break;
				case 14: /* SMLAL */
					snprintf(buf,len, "SMLAL%s  R%d, R%d, R%d, R%d", cond, RD(ir), RN(ir), RM(ir), RS(ir) );
					break;
				case 15: /* SMLALS */

					break;
				case 16: /* SWP */
					snprintf(buf,len, "SWP%s    R%d, R%d, [R%d]", cond, RD(ir), RN(ir), RM(ir) );
					break;
				case 20: /* SWPB */
					snprintf(buf,len, "SWPB%s   R%d, R%d, [R%d]", cond, RD(ir), RN(ir), RM(ir) );
					break;
				default:
					UNIMP(ir);
				}
				break;
			case 1:
				if( LFLAG(ir) ) {
					/* LDRH */
				} else {
					/* STRH */
				}
				break;
			case 2:
				if( LFLAG(ir) ) {
					/* LDRSB */
				} else {
					UNIMP(ir);
				}
				break;
			case 3:
				if( LFLAG(ir) ) {
					/* LDRSH */
				} else {
					UNIMP(ir);
				}
				break;
			}
		} else {
			/* Data processing */

			switch(OPCODE(ir)) {
			case 0: /* AND Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "AND%s    R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 1: /* ANDS Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "ANDS%s   R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 2: /* EOR Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "EOR%s    R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 3: /* EORS Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "EORS%s   R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 4: /* SUB Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "SUB%s    R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 5: /* SUBS Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "SUBS%s   R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 6: /* RSB Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "RSB%s    R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 7: /* RSBS Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "RSBS%s   R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 8: /* ADD Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "ADD%s    R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 9: /* ADDS Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "ADDS%s   R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 10: /* ADC Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "ADC%s    R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 11: /* ADCS Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "ADCS%s   R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 12: /* SBC Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "SBC%s    R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 13: /* SBCS Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "SBCS%s   R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 14: /* RSC Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "RSC%s    R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 15: /* RSCS Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "RSCS%s   R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 16: /* TST Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "TST%s    R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 18: /* TEQ Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "TEQ%s    R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 20: /* CMP Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "CMP%s    R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 22: /* CMN Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "CMN%s    R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 24: /* ORR Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "ORR%s    R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 25: /* ORRS Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "ORRS%s   R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 26: /* MOV Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "MOV%s    R%d, %s", cond, RD(ir), operand);
				break;
			case 27: /* MOVS Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "MOVS%s   R%d, %s", cond, RD(ir), operand);
				break;
			case 28: /* BIC Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "BIC%s    R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 29: /* BICS Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "BICS%s   R%d, R%d, %s", cond, RD(ir), RN(ir), operand);
				break;
			case 30: /* MVN Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "MVN%s    R%d, %s", cond, RD(ir), operand);
				break;
			case 31: /* MVNS Rd, Rn, operand */
				arm_disasm_shift_operand(ir, operand, sizeof(operand));
				snprintf(buf, len, "MVNS%s   R%d, %s", cond, RD(ir), operand);
				break;
			default:
				UNIMP(ir);
			}
		}
		break;
	case 1: /* Load/store */
		arm_disasm_address_operand( ir, operand, sizeof(operand) );
		switch( (ir>>20)&0x17 ) {
			case 0:
			case 16:
			case 18:
				snprintf(buf, len, "STR%s    R%d, %s", cond, RD(ir), operand );
				break;
			case 1:
			case 17:
			case 19:
				snprintf(buf, len, "LDR%s    R%d, %s", cond, RD(ir), operand );
				break;
			case 2:
				snprintf(buf, len, "STRT%s   R%d, %s", cond, RD(ir), operand );
				break;
			case 3:
				snprintf(buf, len, "LDRT%s   R%d, %s", cond, RD(ir), operand );
				break;
			case 4:
			case 20:
			case 22:
				snprintf(buf, len, "STRB%s   R%d, %s", cond, RD(ir), operand );
				break;
			case 5:
			case 21:
			case 23:
				snprintf(buf, len, "LDRB%s   R%d, %s", cond, RD(ir), operand );
				break;
			case 6:
				snprintf(buf, len, "STRBT%s  R%d, %s", cond, RD(ir), operand );
				break;
			case 7: 
				snprintf(buf, len, "LDRBT%s  R%d, %s", cond, RD(ir), operand );
				break;
		}
		break;
	case 2: /* Load/store multiple, branch*/
		j = snprintf( buf, len, LFLAG(ir) ? "LDM%s%s  R%d%c,":"STM%s%s  R%d%c,", 
	              ldmModes[(ir>>23)&0x03], cond, RN(ir), WFLAG(ir)?'!':' ' );
		buf += j;
		len -= j;
		for( i = 0; i<16 && len > 2; i++ ) {
			if( (ir >> i)&1 ) {
				j = snprintf( buf, len, "R%d", i );
				buf+=j;
				len-=j;
			}
		}
		if( SFLAG(ir) && len > 0 ) {
			buf[0] = '^';
			buf[1] = '\0';
		}
		break;
	case 3: /* Copro */
		UNIMP(ir);
		break;
	}
	
	
	
	return pc+4;
}