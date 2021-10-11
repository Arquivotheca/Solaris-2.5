/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#ident	"@(#)kvmgetu.c	2.16	93/05/04 SMI"

#include "kvm_impl.h"
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>

struct user *
kvm_getu(kd, proc)
	kvm_t *kd;
	struct proc *proc;
{

	if (proc->p_stat == SZOMB)	/* zombies don't have u-areas */
		return (NULL);

	/* u area now lives in proc struct */
	
	kd->uarea = (char *)&(proc->p_user);

/* this is like the getkvm() macro, but returns NULL on error instead of -1 */
#define getkvmnull(a,b,m)						\
	if (kvm_read(kd, (u_long)(a), (caddr_t)(b), sizeof (*b)) 	\
						!= sizeof (*b)) {	\
		KVM_ERROR_2("error reading %s", m);			\
		return (NULL);						\
	}

	/*
	 * As a side-effect for adb -k, initialize the user address space
	 * description (if there is one; proc 0 and proc 2 don't have
	 * address spaces).
	 */
	if (proc->p_as) {
		getkvmnull(proc->p_as, &kd->Uas,
			"user address space descriptor");
	}

	return ((struct user *)kd->uarea);
}

/*
 * XXX - do we also need a kvm_getkernelstack now that it is not part of the
 * u-area proper any more?
 */

