/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#ifndef _KVM_IMPL_H
#define	_KVM_IMPL_H

#pragma ident	"@(#)kvm_impl.h	2.30	94/06/08 SMI"

#include <kvm.h>
#include <kvm_kbi.h>
#include <unistd.h>
#include <sys/dumphdr.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* missing from unistd.h at the moment */
offset_t llseek(int fildes, offset_t offset, int whence);

/* libkvm library debugging */
#ifdef _KVM_DEBUG
/* no varargs for macros, unfortunately */
#define	KVM_ERROR_1(s)		_kvm_error((s))
#define	KVM_ERROR_2(s, t)	_kvm_error((s), (t))
#define	KVM_PERROR_1(s)		_kvm_perror((s))
#define	KVM_PERROR_2(s, t)	_kvm_perror((s), (t))
extern void _kvm_error(), _kvm_perror();
#else
#define	KVM_ERROR_1(s)		/* do nothing */
#define	KVM_ERROR_2(s, t)	/* do nothing */
#define	KVM_PERROR_1(s)		/* do nothing */
#define	KVM_PERROR_2(s, t)	/* do nothing */
#endif _KVM_DEBUG

#define	LIVE_NAMELIST	"/dev/ksyms"
#define	LIVE_COREFILE	"/dev/mem"
#define	LIVE_VIRTMEM	"/dev/kmem"
#define	LIVE_SWAPFILE	"/dev/drum"

#define	pagesz		kvm_param->p_pagesize
#define	pageoffset	kvm_param->p_pageoffset
#define	pageshift	kvm_param->p_pageshift
#define	pagemask	kvm_param->p_pagemask
#define	kernelbase	kvm_param->p_kernelbase
#define	usrstack	kvm_param->p_usrstack

/*
 * XXX-Declaration of struct _kvmd moved to kvm.h
 * Necessary to make the contents of the _kvmd structure visible
 * to lkvm_pd.so
 */

#ifdef	__cplusplus
}
#endif

#endif	/* _KVM_IMPL_H */
