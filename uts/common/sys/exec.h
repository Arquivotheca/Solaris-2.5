/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_EXEC_H
#define	_SYS_EXEC_H

#pragma ident	"@(#)exec.h	1.26	94/09/26 SMI"	/* from SVr4.0 1.15 */

#include <sys/systm.h>
#include <vm/seg.h>
#include <vm/seg_vn.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	getexmag(x)	(x[0] << 8) + x[1]

struct execa {
	char    *fname;
	char    **argp;
	char    **envp;
};

typedef struct execenv {
	caddr_t ex_brkbase;
	u_int	ex_brksize;
	short   ex_magic;
	vnode_t *ex_vp;
} execenv_t;

#define	LOADABLE_EXEC(e)	((e)->exec_lock)
#define	LOADED_EXEC(e)		((e)->exec_func)

extern int nexectype;		/* number of elements in execsw */
extern struct execsw execsw[];
extern kmutex_t execsw_lock;

#if defined(sparc) || defined(__sparc)
#include <sys/stack.h>
#define	GET_NARGC(nc, na) \
	(int)(SA(nc + (na + 4) * NBPW) + sizeof (struct rwindow))
#else
#define	GET_NARGC(nc, na) (int)(nc + (na + 4) * NBPW)
#endif

#ifdef _KERNEL

/*
 * User argument structure for stack image management
 */
struct uarg {
	int	na;
	int	ne;
	int	nc;
	char	*fname;
	int	auxsize;
	caddr_t	stackend;
	struct	as *as;
	caddr_t	hunk_base;
	u_int	hunk_size;
	struct	anon_map *amp;
	int	traceinval;
};

/*
 * The following macro is a machine dependent encapsulation of
 * postfix processing to hide the stack direction from elf.c
 * thereby making the elf.c code machine independent.
 */
#define	execpoststack(ARGS, ARRAYADDR, BYTESIZE)  \
	(copyout((caddr_t)ARRAYADDR, ARGS->stackend, BYTESIZE) ? EFAULT \
		: ((ARGS->stackend += BYTESIZE), 0))


#define	INTPSZ	MAXPATHLEN
struct intpdata {
	char	*intp;
	char	*intp_name;
	char	*intp_arg;
};

struct execsw {
	short	*exec_magic;
	int	(*exec_func)(struct vnode *, struct execa *, struct uarg *,
		    struct intpdata *, int, long *, int, caddr_t,
		    struct cred *);
	int	(*exec_core)(struct vnode *, struct proc *, struct cred *,
		    rlim_t, int);
	krwlock_t *exec_lock;
};

extern short aout_zmagic;
extern short aout_nmagic;
extern short aout_omagic;

extern int exec_args(struct execa *, struct uarg *, struct intpdata *,
	int **);

extern int exec(struct execa *, rval_t *);
extern int exece(struct execa *, rval_t *);
extern int gexec(vnode_t *, struct execa *, struct uarg *,
    struct intpdata *, int, long *, caddr_t, struct cred *);
extern struct execsw *allocate_execsw(char *, short);
extern struct execsw *findexecsw(short);
extern struct execsw *findexectype(short);
extern int execpermissions(struct vnode *, struct vattr *, struct uarg *);
extern int execmap(vnode_t *, caddr_t, size_t, size_t, off_t, int, int);
extern void setexecenv(struct execenv *);
extern int execopen(struct vnode **, int *);
extern int execclose(int);
extern void setregs(void);
extern int core_seg(proc_t *, vnode_t *, off_t, caddr_t,
    size_t, rlim_t, cred_t *);

/* a.out stuff */

struct exec;

extern caddr_t	gettmem(struct exec *);
extern caddr_t	getdmem(struct exec *);
extern u_int	getdfile(struct exec *);
extern u_int	gettfile(struct exec *);
extern int chkaout(struct exdata *);
extern void getexinfo(struct exdata *, struct exdata *, int *, int *);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_EXEC_H */
