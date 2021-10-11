/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

#ident	"@(#)kvmnextproc.c	2.11	93/10/11 SMI"

#include "kvm_impl.h"
#include <sys/param.h>
#include <sys/time.h>
#include <sys/proc.h>

struct proc *
kvm_nextproc(kd)
	kvm_t *kd;
{
	register int i;

	/* if no proc buf, allocate one */
	if (kd->pbuf == NULL) {
		kd->pbuf = (struct proc *)malloc(sizeof(struct proc));
		if (kd->pbuf == NULL) {
			KVM_PERROR_1("can't allocate proc cache");
			return ((struct proc *)-1);
		}
	}

	if (kd->pnext != NULL) {
		if ((i = kvm_read(kd, (u_long)kd->pnext, (char *)kd->pbuf,
		    sizeof (struct proc))) == -1)
			return (NULL);
		kd->pnext = (int)kd->pbuf->p_next;
		return (kd->pbuf);
	}
	return (NULL);
}

struct proc *
kvm_getproc(kd, pid)
	kvm_t *kd;
	int pid;
{
	register int n;
	register int i;
	struct pid pidbuf;
	register u_long procp;

	/* if no proc buf, allocate one */
	if (kd->pbuf == NULL) {
		kd->pbuf = (struct proc *)malloc(sizeof(struct proc));
		if (kd->pbuf == NULL) {
			KVM_PERROR_1("can't allocate proc cache");
			return ((struct proc *)-1);
		}
	}

	for (n = 0, procp = kd->proc; n < kd->nproc && procp != NULL;
	     procp = (u_long)kd->pbuf->p_next) {
		if ((i = kvm_read(kd, (u_long)procp, (char *)kd->pbuf,
		    sizeof(struct proc))) == -1)
			return (NULL);

		/*
		 * pid is now in separate struct.
		 */
		if ((i = kvm_read(kd, (u_long)kd->pbuf->p_pidp,
		    (char *)&pidbuf, sizeof (struct pid))) == -1)
			return (NULL);
		if ((kd->pbuf->p_stat != 0) && (pidbuf.pid_id == pid))
			return (kd->pbuf);
	}
	return (NULL);
}
