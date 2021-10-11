/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_ASYNC_H
#define	_SYS_ASYNC_H

#pragma ident	"@(#)async.h	1.5	95/06/06 SMI"

#include <sys/privregs.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	_ASM
typedef	u_int	(*afunc)();

struct ecc_flt {
	u_longlong_t	flt_stat;	/* async. fault stat. reg. */
	u_longlong_t	flt_addr;	/* async. fault addr. reg. */
	u_char		flt_in_proc;	/* fault being handled */
	u_char		flt_synd;	/* ECC syndrome (CE only) */
	u_char		flt_size;	/* size of failed transfer */
	u_char		flt_offset;	/* offset of fault failed transfer */
	u_short		flt_upa_id;	/* upa id# of cpu/sysio/pci */
	u_short		flt_inst;	/* instance of cpu/sysio/pci */
	afunc		flt_func;	/* logging func for fault */
};

struct  upa_func {
	u_short ftype;			/* function type */
	afunc 	func;			/* function to run */
	caddr_t	farg;			/* argument (pointer to soft state) */
};

extern void error_init(void);
extern void error_disable(void);
extern void register_upa_func();
extern int read_ecc_data(u_int aligned_addr, short loop,
		short ce_err, short verbose);

extern int ce_error(u_longlong_t *afsr, u_longlong_t *afar, u_char ecc_synd,
	u_char size, u_char offset, u_short id, u_short inst, afunc log_func);
extern u_int handle_ce_error(struct ecc_flt *pce);
extern int ue_error(u_longlong_t *afsr, u_longlong_t *afar, u_char ecc_synd,
	u_char size, u_char offset, u_short id, u_short inst, afunc log_func);
extern u_int handle_ue_error(struct ecc_flt *pce);
extern u_int handle_kill_proc(u_longlong_t *afar, u_longlong_t *afsr,
				char *unum);

extern void cpu_ce_error(struct regs *rp, u_int p_afsr1, u_int p_afar1,
	u_int dp_err, u_int p_afar0);
extern void cpu_async_error(struct regs *rp, u_int p_afsr1, u_int p_afar1,
	u_int p_afsr0, u_int p_afar0);

extern u_int cpu_check_cp(struct ecc_flt *ecc);
extern u_int cpu_log_ue_err(struct ecc_flt *ecc, char *unum);
extern u_int cpu_log_ce_err(struct ecc_flt *ecc, char *unum);

#endif	/* !_ASM */

/*
 * Types of error functions (for ftype field)
 */
#define	UE_ECC_FTYPE	0x0001		/* UnCorrectable ECC Error */
#define	RESET_TID_FTYPE	0x0002		/* Reset interrupts to tid */
#define	DIS_ERR_FTYPE	0x0004		/* Disable errors */

/*
 * Bits of Sun5 Asynchronous Fault Status Register
 */
#define	P_AFSR_STICKY	0xC0000001FEE00000ULL /* mask for sticky bits, not CP */
#define	P_AFSR_ERRS	0x000000001EE00000ULL /* mask for remaining errors */
#define	P_AFSR_ME	0x0000000100000000ULL /* errors > 1, same type!=CE */
#define	P_AFSR_PRIV	0x0000000080000000ULL /* priv/supervisor access */
#define	P_AFSR_ISAP	0x0000000040000000ULL /* incoming system addr. parity */
#define	P_AFSR_ETP	0x0000000020000000ULL /* ecache tag parity */
#define	P_AFSR_IVUE	0x0000000010000000ULL /* interrupt vector with UE */
#define	P_AFSR_TO	0x0000000008000000ULL /* bus timeout */
#define	P_AFSR_BERR	0x0000000004000000ULL /* bus error */
#define	P_AFSR_LDP	0x0000000002000000ULL /* data parity error from SDB */
#define	P_AFSR_CP	0x0000000001000000ULL /* copyout parity error */
#define	P_AFSR_WP	0x0000000000800000ULL /* writeback ecache data parity */
#define	P_AFSR_EDP	0x0000000000400000ULL /* ecache data parity */
#define	P_AFSR_UE	0x0000000000200000ULL /* uncorrectable ECC error */
#define	P_AFSR_CE	0x0000000000100000ULL /* correctable ECC error */
#define	P_AFSR_ETS	0x00000000000F0000ULL /* cache tag parity syndrome */
#define	P_AFSR_P_SYND	0x000000000000FFFFULL /* data parity syndrome */

/*
 * Shifts for Sun5 Asynchronous Fault Status Register
 */
#define	P_AFSR_D_SIZE_SHIFT	(57)
#define	P_AFSR_ETS_SHIFT	(16)

/*
 * Bits of Spitfire Asynchronous Fault Address Register
 */
#define	S_AFAR_PA	0x000001FFFFFFFFF0ULL /* PA<40:4>: physical address */

/*
 * Bits of Sun5 Error Enable Register
 */
#define	EER_ISAPEN	0x00000000000000004ULL /* enable ISAP */
#define	EER_NCEEN	0x00000000000000002ULL /* enable the other errors */
#define	EER_CEEN	0x00000000000000001ULL /* enable CE */

/*
 * Bits and vaddrs of Sun5 Datapath Error Registers
 */
#define	P_DER_UE	0x00000000000000200ULL /* UE has occurred */
#define	P_DER_CE	0x00000000000000100ULL /* CE has occurred */
#define	P_DER_E_SYND	0x000000000000000FFULL /* SYND<7:0>: ECC syndrome */
#define	P_DER_H		0x0			/* datapath error reg upper */
#define	P_DER_L		0x18			/* datapath error reg upper */

/*
 * Bits of Sun5 Datapath Control Register
 */
#define	P_DCR_VER	0x00000000000001E00ULL /* datapath version */
#define	P_DCR_F_MODE	0x00000000000000100ULL /* send FCB<7:0> */
#define	P_DCR_FCB	0x000000000000000FFULL /* ECC check bits to be forced */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ASYNC_H */
