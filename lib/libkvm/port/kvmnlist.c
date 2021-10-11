/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

#ident	"@(#)kvmnlist.c	2.13	93/10/11 SMI"

#include "kvm_impl.h"
#include <nlist.h>

#ifdef sparc
#define	ELF_TARGET_SPARC
#endif
#include <libelf.h>

/*
 * Look up a set of symbols in the running system namelist (default: ksyms)
 */
int
kvm_nlist(kd, nl)
	kvm_t *kd;
	struct nlist nl[];
{
	register int e;

	if (kd->namefd == -1) {
		KVM_ERROR_1("kvm_nlist: no namelist descriptor");
		return (-1);
	}

	e = nlist(kd->name, nl);

	if (e == -1) {
		KVM_ERROR_2("bad namelist file %s", kd->name);
	}

	return (e);
}
