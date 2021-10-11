/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#ident	"@(#)process.h	1.26	94/06/15 SMI"

#if defined(_KERNEL)
#include <sys/sysmacros.h>
#else
#define	_KERNEL
#include <sys/sysmacros.h>
#undef _KERNEL
#endif

#ifdef __ELF
#include <sys/elf.h>
#endif	/* __ELF */

#include <sys/thread.h>

addr_t	userpc;
#if !defined(KADB)
enum	{NOT_KERNEL,	/* -k option not used.  Not kernel debugging */
	LIVE,		/* adb -k on a live kernel */
	TRAPPED_PANIC,	/* adb -k on a dump got thru trap() (valid panic_reg) */
	CMN_ERR_PANIC	/* adb -k on a dump which has bypassed trap() */
	} kernel;
#else
int	kernel;
#endif
int	kcore;
int	slr;
/* leading 'C' in these names avoids similarly named system macros. */
proc_t	*Curproc;
kthread_id_t Curthread;
unsigned upage;
int	physmem;
int	dot;
int	dotinc;
pid_t	pid;
int	executing;
int	fcor;
int	fsym;
int	signo;
int	sigcode;

#ifdef __ELF
Elf32_Ehdr	filhdr;
Elf32_Phdr	*proghdr;
#else	/* __ELF */
struct	exec filhdr;
#endif	/* __ELF */

#ifndef KADB
#ifdef u
#undef u
#endif
struct	user u;
#endif	/* !KADB */

char	*corfil, *symfil;

/*
 * file address maps : there are two maps, ? and /. Each consists of a sequence
 * of ranges. If mpr_b <= address <= mpr_e the f(address) = file_offset
 * where f(x) = x + (mpr_f-mpr_b). mpr_fn and mpr_fd identify the file.
 * the first 2 ranges are always present - additional ranges are added
 * if inspection of a core file indicates that some of the program text
 * is in shared libraries - one range per lib.
 */
struct map_range {
	int			mpr_b, mpr_e, mpr_f;
	char			*mpr_fn;
	int			mpr_fd;
	struct map_range	*mpr_next;
};

struct map {
	struct map_range	*map_head, *map_tail;
} txtmap, datmap;

#define	BKPTSET		0x1
#define	BKPT_TEMP	0x2		/* volatile bkpt flag */
#define	BKPT_ERR	0x8000		/* breakpoint not successfully set */
					/* */
#if defined(KADB) && defined(i386)
#define	BKPTEXEC	0x3

#define	BPINST	 0
#define	BPWRITE	 1
#define	BPACCESS 3
#define	BPDBINS	 4

#define	NDEBUG	4

#define	PTRACE_CLRBKPT PTRACE_CLRDR7 /* clear all hardware break points */
#define	PTRACE_SETWR   PTRACE_SETWRBKPT
#define	PTRACE_SETAC   PTRACE_SETACBKPT

#endif

#define	MAXCOM	64

struct	bkpt {
	addr_t	loc;
	addr_t	ins;
	int	count;
	int	initcnt;
	int	flag;
#if defined(KADB) && defined(i386)
	char	type;
	char	len;
#endif
	char	comm[MAXCOM];
	struct	bkpt *nxtbkpt;
};

struct	bkpt *bkptlookup();
