/*
 * Copyright (c) 1984-1991, by Sun Microsystems, Inc.
 */

#ifndef _SYS_XTREG_H
#define	_SYS_XTREG_H

#pragma ident	"@(#)xtreg.h	1.7	93/03/10 SMI"	/* From SunOS-4.1 1.9 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Xylogics 472 multibus tape controller
 * IOPB definition.
 */

struct xtiopb {
	/* Byte 1 */
	u_char	xt_swab	   : 1; /* do byte swapping */
	u_char	xt_retry   : 1; /* enable retries */
	u_char	xt_subfunc : 6; /* sub-function code */
	/* Byte 0 */
	u_char	xt_autoup  : 1; /* auto update of IOPB */
	u_char	xt_reloc   : 1; /* use relocation */
	u_char	xt_chain   : 1; /* command chaining */
	u_char	xt_ie	   : 1; /* interrupt enable */
	u_char	xt_cmd	   : 4; /* command */
	/* Byte 3 */
	u_char	xt_errno;	/* error number */
	/* Byte 2 */
	u_char	xt_iserr   : 1; /* error indicator */
	u_char		   : 2;
	u_char	xt_ctype   : 3; /* controller type */
	u_char		   : 1;
	u_char	xt_complete: 1; /* completion code valid  */
	u_short xt_status;	/* 5, 4: various status bits */
	/* Byte 7 */
	u_char		   : 5;
	u_char	xt_unit	   : 3; /* unit number */
	/* Byte 6 */
	u_char	xt_bytebus : 1; /* use byte transfers */
	u_char		   : 4;
	u_char	xt_throttle: 3; /* throttle control */
	u_short xt_cnt;		/* 9, 8: requested count */
	u_short xt_bufoff;	/* b, a: buffer offset */
	u_short xt_bufrel;	/* d, c: buffer offset */
	u_short xt_nxtoff;	/* f, e: next iopb offset */
	u_short xt_acnt;	/* 11, 10: actual count */
};

/* commands */
#define	XT_NOP		0x00	/* no operation */
#define	XT_WRITE	0x01	/* write */
#define	XT_READ		0x02	/* read */
#define	XT_SEEK		0x05	/* position */
#define	XT_DRESET	0x06	/* drive reset */
#define	XT_FMARK	0x07	/* write or erase File Mark (Tape Mark) */
#define	XT_DSTAT	0x09	/* read drive status */
#define	XT_PARAM	0x0B	/* set drive parameters */
#define	XT_TEST		0x0C	/* self test */

/* Subfunction codes */
#define	XT_BACK_REC	0x20	/* Back position x number of records */
#define	XT_SKIP_REC	0x00	/* Frwd position x number of records */
#define	XT_SKIP_MARK	0x01	/* Frwd position x number of tape marks */
#define	XT_BACK_MARK	0x21	/* Back position x number of tape marks */
#define	XT_REWIND	0x02	/* rewind */
#define	XT_UNLOAD	0x03	/* unload */

#define	XT_ERASE	0x01	/* erase, when used with FMARK */
#define	XT_LO_DENSITY	0x00	/* low density */
#define	XT_HI_DENSITY	0x01	/* high density */
#define	XT_LO_SPEED	0x02	/* low speed */
#define	XT_HI_SPEED	0x03	/* high speed */

/* status codes */
#define	XTS_HER		0x8000		/* hard error */
#define	XTS_IEI		0x4000		/* interrupt on each iopb */
#define	XTS_GCR		0x2000		/* group code recording */
#define	XTS_FMK		0x1000		/* Tape(or File) Mark encountered */
#define	XTS_DLD		0x0800		/* data late detected */
#define	XTS_RLL		0x0400		/* record length long */
#define	XTS_RLS		0x0200		/* record length short */
#define	XTS_CER		0x0100		/* corrected error */
#define	XTS_EOT		0x0080		/* end of tape */
#define	XTS_BOT		0x0040		/* beginning of tape */
#define	XTS_FPT		0x0020		/* write protected */
#define	XTS_REW		0x0010		/* rewinding */
#define	XTS_ONL		0x0008		/* on line */
#define	XTS_RDY		0x0004		/* drive ready */
#define	XTS_DBY		0x0002		/* data busy */
#define	XTS_FBY		0x0001		/* formatter busy */
#define	XTS_BITS "\1FBSY\2DBSY\3RDY\4ONL\5REW\6PROT\7BOT\10EOT\11CER\12RLS\
\13RLL\14DLD\15FMK\16GCR\17IEI\20HER"

/* error codes */
#define	XTE_NO_ERROR		0x00
#define	XTE_INTERRUPT_PENDING	0x01
#define	XTE_BUSY_CONFLICT	0x03
#define	XTE_OPERATION_TIMEOUT	0x04
#define	XTE_HARD_ERROR		0x06
#define	XTE_PARITY_ERROR	0x07
#define	XTE_SLAVE_ACK_ERROR	0x0E
#define	XTE_WRITE_PROTECT_ERR	0x14
#define	XTE_DRIVE_OFF_LINE	0x16
#define	XTE_SELF_TEST_A_FAILED	0x1A
#define	XTE_SELF_TEST_B_FAILED	0x1B
#define	XTE_SELF_TEST_C_FAILED	0x1C
#define	XTE_TAPE_MARK_FAILURE	0x1D
#define	XTE_TAPE_MARK_ON_READ	0x1E
#define	XTE_CORRECTED_DATA	0x1F
#define	XTE_REC_LENGTH_SHORT	0x22
#define	XTE_REC_LENGTH_LONG	0x23
#define	XTE_REVERSE_INTO_BOT	0x30
#define	XTE_EOT_DETECTED	0x31
#define	XTE_ID_BURST_DETECTED	0x32
#define	XTE_DATA_LATE_DETECTED	0x33

/*
 * format [xxxx xxxx xxxx xxxx Dxaa cccc ssss ssss]
 * D = 1 if DMA involved
 * x = available
 * aa = distinction between commands for retry
 * cccc = command
 * ssssssss = subfunction
 */

#define	CMD_DONE		0xFFFD
#define	CMD_ABORT		0xFFFA
#define	CMD_RETRY		0xFFFE
#define	CMD_NOP			0x0000
#define	CMD_WRITE		0x8100
#define	CMD_READ		0x8200
#define	CMD_SKIP_REC		0x0500
#define	CMD_BACK_REC		0x0520
#define	CMD_SKIP_MARK		0x0501
#define	CMD_BACK_MARK		0x0521
#define	CMD_REWIND		0x0502
#define	CMD_UNLOAD		0x0503
#define	CMD_DRIVE_RESET		0x0600
#define	CMD_LOAD_ONLINE		0x0601
#define	CMD_WRITE_MARK		0x0700
#define	CMD_SHORT_ERASE		0x0701
#define	CMD_STATUS		0x0900
#define	CMD_LO_DENSITY		0x0B00
#define	CMD_HI_DENSITY		0x0B01
#define	CMD_LO_SPEED		0x0B02
#define	CMD_HI_SPEED		0x0B03

/* commands including intermediate state for retry or EOT/EOM */
#define	CMD_BACK_REC_R		0x2520
#define	CMD_BACK_REC_W		0x1520
#define	CMD_BACK_REC_W_EOT	0x3520
#define	CMD_SHORT_ERASE_W	0x1701
#define	CMD_BACK_MARK_R_EOF	0x1521
#define	CMD_SKIP_MARK_BSR_EOF	0x1501
#define	CMD_BACK_MARK_FSR_EOF	0x2521

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_XTREG_H */
