/*
 * Copyright (c) 1992-1993, by Sun Microsystems Inc.
 */

#ifndef	_SYS_CPR_IMPL_H
#define	_SYS_CPR_IMPL_H

#pragma ident	"@(#)cpr_impl.h	1.8	93/09/15 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * This file contains machine dependent information for CPR
 */

/*
 * CPR Machine Dependent Information Format:
 *
 *	Machdep Info: Anything needed to support a particular platform,
 *		things like the thread ptr and other registers, also
 *		for the mmu. Examples are the pmeg allocation for 4c or
 *		the PTE table for 4m, the internal structure of this block
 *		is only understood by the PSM since it could be different
 *		for differnt machines, all we require is a header telling
 *		us the size of the structure.
 */

#define	CPR_MACHDEP_MAGIC	'MaDp'

/*
 * machdep header used by platform specific code to resume the
 * state of the machine, usually contains the thread pointer
 * followed by MMU type of info such as pmeg or PTE.
 */
struct cpr_machdep_desc {
    u_int md_magic;	/* paranoia check */
    u_int md_size;	/* the size of the "opaque" data following */
};
typedef struct cpr_machdep_desc cmd_t;

#define	PATOPTP_SHIFT	4
#define	PN_TO_ADDR(pn)	(((pn) << MMU_STD_PAGESHIFT) & MMU_STD_PAGEMASK)
#define	ADDR_TO_PN(a)	(((a) >> MMU_STD_PAGESHIFT) & MMU_STD_ROOTMASK)

extern u_int i_cpr_va_to_pfn(u_int);
extern int i_cpr_write_machdep(vnode_t *);
extern void i_cpr_machdep_setup(void);
extern void i_cpr_enable_intr(void);
extern void i_cpr_enable_clkintr(void);
extern void i_cpr_set_tbr(void);
extern void i_cpr_stop_intr(void);
extern void i_cpr_vac_flushall(void);
extern timestruc_t i_cpr_todget();
extern void cpr_get_bootopt(char *default_bootfile);
extern void cpr_set_bootopt(char *boot_file, char *silent);
extern void cpr_reset_bootopt(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CPR_IMPL_H */
