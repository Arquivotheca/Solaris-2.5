/*
 * Copyright (c) 1987-1993, by Sun Microsystems, Inc.
 */

#ifndef	_KVM_H
#define	_KVM_H

#pragma ident	"@(#)kvm.h	2.16	95/07/11 SMI"

#include <sys/types.h>
#include <nlist.h>
#include <sys/user.h>
#include <sys/proc.h>

#ifdef __cplusplus
extern "C" {
#endif

/* define a 'cookie' to pass around between user code and the library */
typedef struct _kvmd kvm_t;

/* libkvm routine definitions */

#ifdef __STDC__
extern kvm_t		*kvm_open(char *, char *, char *, int, char *);
extern int		 kvm_close(kvm_t *);
extern int		 kvm_nlist(kvm_t *, struct nlist []);
extern int		 kvm_read(kvm_t *, u_long, char *, u_int);
extern int		 kvm_kread(kvm_t *, u_long, char *, u_int);
extern int		 kvm_uread(kvm_t *, u_long, char *, u_int);
extern int		 kvm_write(kvm_t *, u_long, char *, u_int);
extern int		 kvm_kwrite(kvm_t *, u_long, char *, u_int);
extern int		 kvm_uwrite(kvm_t *, u_long, char *, u_int);
extern struct proc	*kvm_getproc(kvm_t *, int);
extern struct proc	*kvm_nextproc(kvm_t *);
extern int		 kvm_setproc(kvm_t *);
extern struct user	*kvm_getu(kvm_t *, struct proc *);
extern int		 kvm_getcmd(kvm_t *, struct proc *, struct user *,
			    char ***, char ***);

#else

extern kvm_t		*kvm_open();
extern int		 kvm_close();
extern int		 kvm_nlist();
extern int		 kvm_read();
extern int		 kvm_kread();
extern int		 kvm_uread();
extern int		 kvm_write();
extern int		 kvm_kwrite();
extern int		 kvm_uwrite();
extern struct proc	*kvm_getproc();
extern struct proc	*kvm_nextproc();
extern int		 kvm_setproc();
extern struct user	*kvm_getu();
extern int		 kvm_getcmd();
#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _KVM_H */
