#ident "@(#)kagent.h	1.2	94/09/29 SMI"

/*
 * Copyright (c) 1985 by Sun Microsystems, Inc.
 */

#define	KA_NULL		0	/* */
#define	KA_SET_BPT	1	/* vaddr */
#define	KA_CLR_BPT	2	/* vaddr */
#define	KA_SET_WPT	3	/* vaddr */
#define	KA_CLR_WPT	4	/* vaddr */
#define	KA_RD_MEM	5	/* vaddr, size, count */
#define	KA_WRT_MEM	6	/* vaddr, size, count, value */
#define	KA_RD_ASIMEM	7	/* cpu_id, asi, paddr, size, count */
#define	KA_WRT_ASIMEM	8	/* cpu_id, asi, paddr, size, count, value */
#define	KA_RD_PHYSMEM	9	/* paddr-hi, paddr-lo, size, count */
#define	KA_WRT_PHYSMEM	0xA	/* paddr-hi, paddr-lo, size, count, value */
#define	KA_RD_REG	0xB	/* cpu_id, type, regno */
#define	KA_WRT_REG	0xC	/* cpu_id, type, regno, value */
#define	KA_CONTINUE	0xD	/* cpu_id */
#define	KA_STEP		0xE
#define	KA_STOP		0xF
#define	KA_SWITCH	0x10	/* cpu_id */
#define	KA_NCPUS	0x11
#define	KA_EXIT		0x12
#define	KA_INTERACTIVE	0x13	/* Debugging hook for interactive testing */
#define	KA_CURCPU	0x14
#define	KA_CPULIST	0x15
#define	KA_NCOMMANDS	0x16

#define	KA_OK		(char)0
#define	KA_ERROR	(char)1

#define	KA_ERROR_ADDR	(char)1
#define	KA_ERROR_SIZE	(char)2
#define	KA_ERROR_PKT	(char)3
#define	KA_ERROR_ARGS	(char)4
#define	KA_ERROR_CMD	(char)5
#define	KA_ERROR_LIMIT	(char)6

#define	KA_MAXARGS 8
#define	KA_MAXPKT  1024
