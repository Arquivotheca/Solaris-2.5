/*
 *      Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _PROC_SERVICE_H
#define	_PROC_SERVICE_H

#pragma ident	"@(#)proc_service.h	1.3	94/10/27 SMI"

/*
 *
 *  Description:
 *	Types, global variables, and function definitions for provider
 * of import functions for user of libthread_db.
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/procfs.h>
#include <sys/lwp.h>

	typedef enum {
		PS_OK,		/* generic "call succeeded" */
		PS_ERR,		/* generic. */
		PS_BADPID,	/* bad process handle */
		PS_BADLID,	/* bad lwp identifier */
		PS_BADADDR,	/* bad address */
		PS_NOSYM,	/* p_lookup() could not find given symbol */
		PS_NOFREGS
				/*
				 * FPU register set not available for given
				 * lwp
				 */
	}	ps_err_e;

	struct ps_prochandle;

	extern ps_err_e ps_pstop(const struct ps_prochandle * ph);
	extern ps_err_e ps_pcontinue(const struct ps_prochandle * ph);
	extern ps_err_e ps_lstop(const struct ps_prochandle * ph,
		lwpid_t lwpid);
	extern ps_err_e ps_lcontinue(const struct ps_prochandle * ph,
		lwpid_t lwpid);
	extern ps_err_e ps_pglobal_lookup(const struct ps_prochandle * ph,
		const char *ld_object_name, const char *ld_symbol_name,
		paddr_t * ld_symbol_addr);
	extern ps_err_e ps_pdread(const struct ps_prochandle * ph,
		paddr_t addr, char *buf, int size);
	extern ps_err_e ps_pdwrite(const struct ps_prochandle * ph,
		paddr_t addr, char *buf, int size);
	extern ps_err_e ps_ptread(const struct ps_prochandle * ph,
		paddr_t addr, char *buf, int size);
	extern ps_err_e ps_ptwrite(const struct ps_prochandle * ph,
		paddr_t addr, char *buf, int size);
	extern ps_err_e ps_lgetregs(const struct ps_prochandle * ph,
		lwpid_t lid, prgregset_t gregset);
	extern ps_err_e ps_lsetregs(const struct ps_prochandle * ph,
		lwpid_t lid, const prgregset_t gregset);
	extern void ps_plog(const char *fmt, ...);
	extern ps_err_e ps_lgetxregsize(const struct ps_prochandle * ph,
		lwpid_t lid, int *xregsize);
	extern ps_err_e ps_lgetxregs(const struct ps_prochandle * ph,
		lwpid_t lid, caddr_t xregset);
	extern ps_err_e ps_lsetxregs(const struct ps_prochandle * ph,
		lwpid_t lid, caddr_t xregset);

/*  ximport.c */

	extern ps_err_e ps_lgetfpregs(const struct ps_prochandle * ph,
		lwpid_t lid, prfpregset_t * fpregset);
	extern ps_err_e ps_lsetfpregs(const struct ps_prochandle * ph,
		lwpid_t lid, const prfpregset_t * fpregset);

#ifdef __cplusplus
}
#endif

#endif	/* _PROC_SERVICE_H */
