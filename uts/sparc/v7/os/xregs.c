/*
 * Copyright (c) 1994, Sun Microsystems, Inc.
 */

#pragma ident	"@(#)xregs.c	1.3	94/11/08 SMI"

#include <sys/t_lock.h>
#include <sys/klwp.h>
#include <sys/ucontext.h>

/*
 * Association of extra register state with a struct ucontext is
 * done by placing an xrs_t within the uc_mcontext filler area.
 *
 * The following routines provide an interface for this association.
 */

/*
 * clear the struct ucontext extra register state pointer
 */
void
xregs_clrptr(struct ucontext *uc)
{
	uc->uc_mcontext.xrs.xrs_id = 0;
	uc->uc_mcontext.xrs.xrs_ptr = (caddr_t)NULL;
}

/*
 * indicate whether or not an extra register state
 * pointer is associated with a struct ucontext
 */
int
xregs_hasptr(struct ucontext *uc)
{
	return (uc->uc_mcontext.xrs.xrs_id == XRS_ID);
}

/*
 * get the struct ucontext extra register state pointer field
 */
caddr_t
xregs_getptr(struct ucontext *uc)
{
	if (uc->uc_mcontext.xrs.xrs_id == XRS_ID)
		return (uc->uc_mcontext.xrs.xrs_ptr);
	return ((caddr_t)NULL);
}

/*
 * set the struct ucontext extra register state pointer field
 */
void
xregs_setptr(struct ucontext *uc, caddr_t xrp)
{
	uc->uc_mcontext.xrs.xrs_id = XRS_ID;
	uc->uc_mcontext.xrs.xrs_ptr = xrp;
}

/*
 * extra register state manipulation routines
 */

int xregs_exists = 0;

/*
 * fill in the extra register state area specified with
 * the specified lwp's extra register state information
 */
/*ARGSUSED*/
void
xregs_get(struct _klwp *lwp, caddr_t xrp)
{
}

/*
 * fill in the extra register state area specified with the
 * specified lwp's non-floating-point extra register state
 * information
 */
/*ARGSUSED*/
void
xregs_getgregs(struct _klwp *lwp, caddr_t xrp)
{
}

/*
 * fill in the extra register state area specified with the
 * specified lwp's floating-point extra register state information
 */
/*ARGSUSED*/
void
xregs_getfpregs(struct _klwp *lwp, caddr_t xrp)
{
}

/*
 * set the specified lwp's extra register
 * state based on the specified input
 */
/*ARGSUSED*/
void
xregs_set(struct _klwp *lwp, caddr_t xrp)
{
}

/*
 * set the specified lwp's non-floating-point extra
 * register state based on the specified input
 */
/*ARGSUSED*/
void
xregs_setgregs(struct _klwp *lwp, caddr_t xrp)
{
}

/*
 * set the specified lwp's floating-point extra
 * register state based on the specified input
 */
/*ARGSUSED*/
void
xregs_setfpregs(struct _klwp *lwp, caddr_t xrp)
{
}

/*
 * return the size of the extra register state
 */
int
xregs_getsize(void)
{
	return (0);
}
