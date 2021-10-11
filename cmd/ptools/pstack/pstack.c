#ident	"@(#)pstack.c	1.2	95/06/27 SMI"

#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <libelf.h>
#include <link.h>
#include <elf.h>

#include "pcontrol.h"
#include "ramdata.h"

#if defined(i386)
#	define	MAX_ARGS 8	/* be generous here */
	typedef struct {
		greg_t	fr_savfp;
		greg_t	fr_savpc;
		greg_t	fr_args[MAX_ARGS];
		long	fr_argc;
		long	fr_argv;
	} frame_t;
#endif	/* i386 */

typedef struct cache {
	Elf32_Shdr *	c_shdr;
	Elf_Data *	c_data;
	char *		c_name;
} Cache;

typedef struct sym_tbl {
	Elf32_Sym *	syms;	/* start of table	*/
	char *		strs;	/* ptr to strings	*/
	int		symn;	/* number of entries	*/
} sym_tbl_t;

typedef struct map_info {
	int		mapfd;	/* file descriptor for mapping	*/
	Elf *		elf;	/* elf handle so we can close	*/
	Elf32_Ehdr *	ehdr;	/* need the header for type	*/
	sym_tbl_t	symtab;	/* symbol table			*/
	sym_tbl_t	dynsym;	/* dynamic symbol table		*/
} map_info_t;

typedef struct proc_info {
	int		proc_fd;	/* /proc fd		*/
	int		num_mappings;	/* number of mappings	*/
	prmap_t *	mappings;	/* extant mappings	*/
	map_info_t *	map_info;	/* per mapping info	*/
} proc_info_t;

extern proc_info_t * get_proc_info(int pfd);
extern void destroy_proc_info(proc_info_t *);
extern char * find_sym_name(caddr_t addr,
		proc_info_t *pip,
		char *func_name,
		u_int *size,
		caddr_t *start);

#define	TRUE	1
#define	FALSE	0

static	void	alrm(int);
static	pid_t	getproc(char *, char **);
static	int	AllCallStacks(process_t *, pid_t, proc_info_t *);
static	void	CallStack(int, prstatus_t *, pid_t, proc_info_t *);
static	int	grabit(process_t *, pid_t);

#if defined(sparc)

static	void	PrintFirstFrame(prgregset_t, id_t, proc_info_t *, prstatus_t *);
static	void	PrintFrame(prgregset_t, id_t, int, proc_info_t *);
static	int	PrevFrame(int,	prgregset_t);

#elif defined(i386)

static	void	PrintFirstFrame(prgreg_t, frame_t *, id_t, int, proc_info_t *);
static	void	PrintFrame(prgreg_t, frame_t *, id_t, int, proc_info_t *);
static	int	PrevFrame(int,	frame_t *);
static	long	argcount(int, long);

#endif

main(argc, argv)
	int argc;
	char **argv;
{
	int retc = 0;
	int opt;
	int errflg = FALSE;
	register process_t *Pr = &Proc;

	command = strrchr(argv[0], '/');
	if (command++ == NULL)
		command = argv[0];

#if 1
	/* allow all accesses for setuid version */
	(void) setuid((int)geteuid());
#endif

	/* options */
	while ((opt = getopt(argc, argv, "p:")) != EOF) {
		switch (opt) {
		case 'p':		/* alternate /proc directory */
			procdir = optarg;
			break;
		default:
			errflg = TRUE;
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (errflg || argc <= 0) {
		(void) fprintf(stderr, "usage:\t%s pid ...\n", command);
		(void) fprintf(stderr, "  (show process call stack)\n");
		exit(2);
	}

	if (!isprocdir(procdir)) {
		(void) fprintf(stderr,
			"%s: %s is not a PROC directory\n",
			command, procdir);
		exit(2);
	}

	/* catch alarms */
	(void) sigset(SIGALRM, alrm);

	while (--argc >= 0) {
		int pfd;
		pid_t pid;
		char *pdir;
		proc_info_t * proc;

		/* get the specified pid and its /proc directory */
		pid = getproc(*argv++, &pdir);

		if (pid < 0 || (pfd = grabit(Pr, pid)) < 0) {
			retc++;
			continue;
		}

		pfd = Pr->pfd;
		proc = get_proc_info(pfd);

		if (AllCallStacks(Pr, pid, proc) != 0)
			retc++;

		(void) close(pfd);
		destroy_proc_info(proc);
	}

	return retc;
}

static pid_t		/* get process id and /proc directory */
getproc(path, pdirp)	/* return pid on success, -1 on failure */
	register char * path;	/* number or /proc/nnn */
	char ** pdirp;		/* points to /proc directory on success */
{
	register char * name;
	register pid_t pid;
	char *next;

	if ((name = strrchr(path, '/')) != NULL)	/* last component */
		*name++ = '\0';
	else {
		name = path;
		path = procdir;
	}

	pid = strtol(name, &next, 10);
	if (isdigit(*name) && pid >= 0 && *next == '\0') {
		if (strcmp(procdir, path) != 0
		 && !isprocdir(path)) {
			(void) fprintf(stderr,
				"%s: %s is not a PROC directory\n",
				command, path);
			pid = -1;
		}
	} else {
		(void) fprintf(stderr, "%s: invalid process id: %s\n",
			command, name);
		pid = -1;
	}

	if (pid >= 0)
		*pdirp = path;
	return pid;
}

static int
grabit(Pr, pid)		/* take control of an existing process */
register process_t *Pr;
pid_t pid;
{
	int gcode;
	int Fflag = 0;

	gcode = Pgrab(Pr, pid, Fflag);

	if (gcode >= 0)
		return gcode;

	if (gcode == G_INTR)
		return -1;

	(void) fprintf(stderr, "%s: cannot grab %d", command, pid);
	switch (gcode) {
	case G_NOPROC:
		(void) fprintf(stderr, ": No such process");
		break;
	case G_ZOMB:
		(void) fprintf(stderr, ": Zombie process");
		break;
	case G_PERM:
		(void) fprintf(stderr, ": Permission denied");
		break;
	case G_BUSY:
		(void) fprintf(stderr, ": Process is traced");
		break;
	case G_SYS:
		(void) fprintf(stderr, ": System process");
		break;
	case G_SELF:
		(void) fprintf(stderr, ": Cannot dump self");
		break;
	}
	(void) fputc('\n', stderr);

	return -1;
}

/*ARGSUSED*/
static void
alrm(sig)
	int sig;
{
	timeout = TRUE;
}

static int
AllCallStacks(Pr, pid, proc)
	process_t *Pr;
	pid_t pid;		/* process-id */
	proc_info_t * proc;
{
	int pfd = Pr->pfd;	/* process file descriptor */
	prstatus_t status;

	if (Ioctl(pfd, PIOCSTATUS, (int)&status) != 0) {
		perror("AllCallStacks(): PIOCSTATUS");
		return -1;
	}
	(void) printf("%d:\t%.70s\n", pid, Pr->psinfo.pr_psargs);
	if (status.pr_nlwp <= 1)
		CallStack(pfd, &status, 0, proc);
	else {
		int nlwp = status.pr_nlwp;
		id_t * lwpid = (id_t *)malloc((nlwp+1)*sizeof(id_t));
		int lwpfd;
		int i;

		if (lwpid == NULL) {
			perror("AllCallStacks(): malloc()");
			return -1;
		}
		if (Ioctl(pfd, PIOCLWPIDS, (int)lwpid) != 0) {
			perror("AllCallStacks(): PIOCLWPIDS");
			free(lwpid);
			return -1;
		}
		for (i = 0; i < nlwp; i++) {
			if ((lwpfd = Ioctl(pfd, PIOCOPENLWP, (int)&lwpid[i])) < 0) {
				perror("AllCallStacks(): PIOCOPENLWP");
				break;
			}
			if (Ioctl(lwpfd, PIOCSTATUS, (int)&status) != 0) {
				perror("AllCallStacks(): lwp PIOCSTATUS");
				(void) close(lwpfd);
				break;
			}
			(void) close(lwpfd);
			CallStack(pfd, &status, lwpid[i], proc);
		}
		free(lwpid);
	}
	return 0;
}

#if defined(sparc)

static void
CallStack(pfd, psp, lwpid, proc)
	int pfd;		/* process file descriptor */
	prstatus_t * psp;
	id_t lwpid;		/* lwp-id */
	proc_info_t * proc;
{
	prgreg_t fp;
	int nfp;
	prgreg_t *prevfp;
	u_int size;
	int i;
	int first = TRUE;
	prgregset_t reg;

	(void) memcpy(reg, psp->pr_reg, sizeof(reg));

	if (psp->pr_flags & PR_ASLEEP) {
		PrintFirstFrame(reg, lwpid, proc, psp);
		first = FALSE;
		reg[R_PC] = reg[R_O7];
		reg[R_nPC] = reg[R_PC] + 4;
	}

	size = 16;
	prevfp = malloc(size * sizeof(prgreg_t));
	nfp = 0;
	do {
		/* prevent stack recursion from running on forever */
		fp = reg[R_FP];
		for (i = 0; i < nfp; i++) {
			if (fp == prevfp[i]) {
				free(prevfp);
				return;
			}
		}
		if (nfp == size) {
			size *= 2;
			prevfp = realloc(prevfp, size * sizeof(prgreg_t));
		}
		prevfp[nfp++] = fp;

		PrintFrame(reg, lwpid, first, proc);
		first = FALSE;
	} while (PrevFrame(pfd, reg) == 0);

	free(prevfp);
}

static void
PrintFirstFrame(reg, lwpid, proc, psp)
	prgregset_t reg;
	id_t lwpid;
	proc_info_t * proc;
	prstatus_t * psp;
{
	char buff[255];
	u_int size;
	caddr_t start;
	char *sname;
	int match = TRUE;
	int i;

	if (lwpid)
		printf("lwp#%d ----------\n", lwpid);

	sname = sysname(psp->pr_syscall);
	printf(" %.8x %-8s (", reg[R_PC], sname);
	for (i = 0; i < psp->pr_nsysarg; i++) {
		if (i != 0)
			printf(", ");
		printf("%x", psp->pr_sysarg[i]);
		if (psp->pr_sysarg[i] != reg[R_O0+i])
			match = FALSE;
	}
	printf(")\n");

	sprintf(buff, "%.8x", reg[R_PC]);
	start = (caddr_t)reg[R_PC];
	strcpy(buff+8, " ????????");
	find_sym_name((caddr_t)reg[R_PC], proc, buff+9, &size, &start);
	if (match) {
		register char *s = buff+9;

		while (*s == '_')
			s++;
		if (strcmp(sname, s) != 0)
			match = FALSE;
	}

	if (!match) {
		printf((start != (caddr_t)reg[R_PC])?
			" %-17s (%x, %x, %x, %x, %x, %x) + %x\n" :
			" %-17s (%x, %x, %x, %x, %x, %x)\n",
			buff,
			reg[R_O0],
			reg[R_O1],
			reg[R_O2],
			reg[R_O3],
			reg[R_O4],
			reg[R_O5],
			(u_int)reg[R_PC] - (u_int)start);
	}
}

static void
PrintFrame(reg, lwpid, first, proc)
	prgregset_t reg;
	id_t lwpid;
	int first;
	proc_info_t * proc;
{
	char buff[255];
	u_int size;
	caddr_t start;

	sprintf(buff, "%.8x", reg[R_PC]);
	start = (caddr_t)reg[R_PC];
	strcpy(buff+8, " ????????");
	find_sym_name((caddr_t)reg[R_PC], proc, buff+9, &size, &start);

	if (lwpid && first)
		printf("lwp#%d ----------\n", lwpid);
	printf((start != (caddr_t)reg[R_PC])?
		" %-17s (%x, %x, %x, %x, %x, %x) + %x\n" :
		" %-17s (%x, %x, %x, %x, %x, %x)\n",
		buff,
		reg[R_I0],
		reg[R_I1],
		reg[R_I2],
		reg[R_I3],
		reg[R_I4],
		reg[R_I5],
		(u_int)reg[R_PC] - (u_int)start);
}

static int
PrevFrame(pfd, reg)
	int pfd;
	prgregset_t reg;
{
	long sp;

	if ((sp = reg[R_I6]) == 0)
		return -1;
	reg[R_PC] = reg[R_I7];
	reg[R_nPC] = reg[R_PC] + 4;
	(void) memcpy(&reg[R_O0], &reg[R_I0], 8*sizeof(prgreg_t));
	if (lseek(pfd, sp, 0) != sp
	 || read(pfd, (char *)&reg[R_L0], 16*sizeof(prgreg_t)) <= 0)
		return -1;
	return 0;
}

#endif	/* sparc */

#if defined(i386)

static void
CallStack(pfd, psp, lwpid, proc)
	int pfd;		/* process file descriptor */
	prstatus_t * psp;
	id_t lwpid;		/* lwp-id */
	proc_info_t * proc;
{
	prgreg_t fp;
	int nfp;
	prgreg_t *prevfp;
	u_int size;
	int first = TRUE;
	prgregset_t reg;
	frame_t frame;
	prgreg_t pc;
	int i;

	(void) memcpy(reg, psp->pr_reg, sizeof(reg));

	if (psp->pr_flags & PR_ASLEEP) {
		(void) memset((char *)&frame, 0, sizeof(frame));
		frame.fr_savfp = reg[R_SP];
		frame.fr_savpc = reg[R_PC];
		for (i = 0; i < psp->pr_nsysarg && i < MAX_ARGS; i++)
			frame.fr_args[i] = psp->pr_sysarg[i];
		frame.fr_argc = psp->pr_nsysarg;
		frame.fr_argv = NULL;
		PrintFirstFrame(reg[R_PC], &frame, lwpid, psp->pr_syscall, proc);
		first = FALSE;
		if (pread(pfd, (char *)&reg[R_PC], sizeof(prgreg_t),
		    (long)reg[R_SP]) != sizeof(prgreg_t))
			return;
		reg[R_SP] += 4;
	}

	if (pread(pfd, (char *)&frame, sizeof(frame), (long)reg[R_FP])
	    != sizeof(frame))
		return;
	frame.fr_argc = argcount(pfd, (long)frame.fr_savpc);
	frame.fr_argv = (long)reg[R_FP] + 2 * sizeof(long);

	size = 16;
	prevfp = malloc(size * sizeof(prgreg_t));
	nfp = 0;
	pc = reg[R_PC];
	do {
		/* prevent stack recursion from running on forever */
		fp = frame.fr_savfp;
		for (i = 0; i < nfp; i++) {
			if (fp == prevfp[i]) {
				free(prevfp);
				return;
			}
		}
		if (nfp == size) {
			size *= 2;
			prevfp = realloc(prevfp, size * sizeof(prgreg_t));
		}
		prevfp[nfp++] = fp;

		PrintFrame(pc, &frame, lwpid, first, proc);
		first = FALSE;
		pc = frame.fr_savpc;
	} while (PrevFrame(pfd, &frame) == 0);

	free(prevfp);
}

static void
PrintFirstFrame(pc, frame, lwpid, sys, proc)
	prgreg_t pc;
	frame_t *frame;
	id_t lwpid;
	int sys;
	proc_info_t * proc;
{
	char buff[255];
	u_int size;
	caddr_t start;
	int i;

	sprintf(buff, "%.8x ", pc);
	start = (caddr_t)pc;
	strcpy(buff+9, sysname(sys));

	if (lwpid)
		printf("lwp#%d ----------\n", lwpid);

	printf(" %-17s (", buff);
	for (i = 0; i < frame->fr_argc && i < MAX_ARGS; i++)
		printf((i+1 == frame->fr_argc)? "%x" : "%x, ",
			frame->fr_args[i]);
	printf(")\n");
}

static void
PrintFrame(pc, frame, lwpid, first, proc)
	prgreg_t pc;
	frame_t *frame;
	id_t lwpid;
	int first;
	proc_info_t * proc;
{
	char buff[255];
	u_int size;
	caddr_t start;
	int i;

	sprintf(buff, "%.8x ", pc);
	start = (caddr_t)pc;
	strcpy(buff+9, "????????");
	find_sym_name((caddr_t)pc, proc, buff+9, &size, &start);

	if (lwpid && first)
		printf("lwp#%d ----------\n", lwpid);
	printf(" %-17s (", buff);
	for (i = 0; i < frame->fr_argc && i < MAX_ARGS; i++)
		printf((i+1 == frame->fr_argc)? "%x" : "%x, ",
			frame->fr_args[i]);
	if (i != frame->fr_argc)
		printf("...");
	putchar(')');
	printf((start != (caddr_t)pc)?
		" + %x\n" : "\n", (u_int)pc - (u_int)start);
}

static int
PrevFrame(pfd, frame)
	int pfd;
	frame_t *frame;
{
	prgreg_t fp = frame->fr_savfp;

	if (fp == 0
	 || pread(pfd, (char *)frame, sizeof(*frame), (long)fp)
	    != sizeof(*frame))
		return -1;
	frame->fr_argc = argcount(pfd, (long)frame->fr_savpc);
	frame->fr_argv = (long)fp + 2 * sizeof(long);

	return 0;
}

/*
 * Given the return PC, return the number of arguments.
 */
static long
argcount(int pfd, long pc)
{
	unsigned char instr[6];
	int count;

	if (pread(pfd, (char *)instr, sizeof(instr), pc) != sizeof(instr)
	 || instr[1] != 0xc4)
		return 0;

	switch (instr[0]) {
	case 0x81:	/* count is a longword */
		count = instr[2]+(instr[3]<<8)+(instr[4]<<16)+(instr[5]<<24);
		break;
	case 0x83:	/* count is a bytes */
		count = instr[2];
		break;
	default:
		count = 0;
		break;
	}

	return count / sizeof(long);
}

#endif	/* i386 */


/*
 * Routines to generate a symbolic traceback from an ELF
 * executable.  It works w/ shared libraries as well.
 * This is a first cut.
 */

/*
 * Define our own standard error routine.
 */
static void
failure(const char * name)
{
	(void) fprintf(stderr, "%s failed: %s\n", name,
	    elf_errmsg(elf_errno()));
	exit(1);
}

void
destroy_proc_info(proc_info_t * ptr)
{
	map_info_t * mptr = ptr->map_info;
	int i;

	if (ptr) {
		for (i = ptr->num_mappings; i; i--, mptr++) {
			if (mptr->mapfd > 0)
				(void) close(mptr->mapfd);
			if (mptr->elf)
				elf_end(mptr->elf);
		}
		if (ptr->mappings)
			free(ptr->mappings);
		if (ptr->map_info)
			free(ptr->map_info);
		free(ptr);
	}
}

proc_info_t *
get_proc_info(int fd)
{
	int i, n;

	map_info_t * map_info, * mptr;

	proc_info_t * ptr = malloc(sizeof(*ptr));

	if (ioctl(fd, PIOCNMAP, &n) < 0) {
		perror("PIONCMAP ioctl");
		return NULL;
	}

	ptr->num_mappings = n;
	ptr->proc_fd = fd;
	ptr->mappings = (prmap_t *) malloc(sizeof(prmap_t) * (n+1));

	if (ioctl(fd, PIOCMAP, ptr->mappings) < 0) {
		perror("PIOCMAP ioctl");
		return NULL;
	}

	ptr->map_info = mptr = map_info =
		(map_info_t *) malloc(sizeof(map_info_t) * (n+1));

	memset(mptr, 0, sizeof(map_info_t) * (n+1));

	for (i = 0; i < n ; i++) {
		mptr = &map_info[i];
		mptr->mapfd = 0;
	}

	return ptr;
}

static int
build_sym_tab(proc_info_t * ptr, int map_index)
{
	int fd = ptr->proc_fd;

	map_info_t * mptr = ptr->map_info + map_index;
	prmap_t * pptr = ptr->mappings + map_index;
	Elf32_Shdr *    shdr;
	Elf *           elf;
	Elf_Scn *       scn;
	Elf32_Ehdr *    ehdr;
	Elf_Data *      data;
	sym_tbl_t *     which;
	Cache *     cache;
	Cache *     _cache;
	char *    names;
	u_int    cnt;

	(void) elf_version(EV_CURRENT);

	if (mptr->mapfd < 0)	/* failed here before */
		return -1;

	if ((mptr->mapfd = ioctl(fd, PIOCOPENM, &pptr->pr_vaddr)) < 0) {
#ifdef DEBUG
		perror("PIOCOPENM ioctl");
		fprintf(stderr, "Cannot get fd for following mapping:\n");
		fprintf(stderr, "\tpr_vaddr    = 0x%x\n", pptr->pr_vaddr);
		fprintf(stderr, "\tpr_size     = %d\n", pptr->pr_size);
		fprintf(stderr, "\tpr_pagesize = %d\n", pptr->pr_pagesize);
		fprintf(stderr, "\tpr_off      = %d\n", pptr->pr_off);
#endif
		return -1;
	}

	if ((elf = elf_begin(mptr->mapfd, ELF_C_READ, NULL)) == NULL
	 || elf_kind(elf) != ELF_K_ELF) {
#ifdef DEBUG
		(void) fprintf(stderr, "file is not elf??\n");
#endif
		(void) close(mptr->mapfd);
		mptr->mapfd = -1;
		return -1;
	}
	mptr->elf = elf;

	if ((mptr->ehdr = ehdr = elf32_getehdr(elf)) == NULL)
		failure("elf_getehdr");

	/*
	 * Obtain the .shstrtab data buffer to provide the required section
	 * name strings.
	 */
	if ((scn = elf_getscn(elf, ehdr->e_shstrndx)) == NULL)
		failure("elf_getscn");

	if ((data = elf_getdata(scn, NULL)) == NULL)
		failure("elf_getdata");

	names = data->d_buf;

	/*
	 * Fill in the cache descriptor with information for each section.
	 */

	cache = (Cache *)malloc(ehdr->e_shnum * sizeof (Cache));

	_cache = cache;
	_cache++;

	for (scn = NULL; scn = elf_nextscn(elf, scn); _cache++) {
		if ((_cache->c_shdr = elf32_getshdr(scn)) == NULL)
			failure("elf32_getshdr");

		if ((_cache->c_data = elf_getdata(scn, NULL)) == NULL)
			failure("elf_getdata");

		_cache->c_name = names + _cache->c_shdr->sh_name;
	}

	for (cnt = 1; cnt < ehdr->e_shnum; cnt++) {
		_cache = &cache[cnt];
		shdr = _cache->c_shdr;

		if (shdr->sh_type == SHT_SYMTAB)
			which = & mptr->symtab;
		else if (shdr->sh_type == SHT_DYNSYM)
			which = & mptr->dynsym;
		else
			continue;

		/*
		 * Determine the symbol data and number.
		 */
		which->syms = (Elf32_Sym *)_cache->c_data->d_buf;
		which->symn = shdr->sh_size / shdr->sh_entsize;
		which->strs = (char *)cache[shdr->sh_link].c_data->d_buf;
	}

	free(cache);

	return 0;
}

char *
find_sym_name(caddr_t addr, proc_info_t * proc, char * func_name, u_int * size, caddr_t * start)
{
	int i;
	int n = proc->num_mappings;
	u_int offset;

	prmap_t * ptr;

	for (i = 0, ptr = proc->mappings; i < n; i++, ptr++) {
		map_info_t * 	mptr;
		Elf32_Sym *     syms;
		int             symn;
		char *		strs;
		int		_cnt;

		if (addr < ptr->pr_vaddr
		 || addr >= (ptr->pr_vaddr + ptr->pr_size))
			continue;

		/* found it */
		mptr = proc->map_info + i;

		if (mptr->ehdr == NULL) {
			if (build_sym_tab(proc, i)) {
#ifdef DEBUG
				fprintf(stderr
			"failed to build symbol table for address 0x%x\n",
					addr);
#endif
				return NULL;
			}
		}

		if (mptr->ehdr->e_type != ET_DYN)
			offset = 0;
		else  {
			offset = (u_int) proc->mappings[i].pr_vaddr
				- (u_int) proc->mappings[i].pr_off;
			addr -= (u_int)offset;
		}

		syms = mptr->symtab.syms;
		symn = mptr->symtab.symn;
		strs = mptr->symtab.strs;

		for (_cnt = 0; _cnt < symn; _cnt++, syms++) {
			if (ELF32_ST_TYPE(syms->st_info) != STT_FUNC
			 || (u_int)addr < syms->st_value
			 || (u_int)addr >= (syms->st_value+syms->st_size))
				continue;

			strcpy(func_name, (strs + syms->st_name));
			*size = syms->st_size;
			*start = (caddr_t)syms->st_value + offset;
			return func_name;
		}

		syms = mptr->dynsym.syms;
		symn = mptr->dynsym.symn;
		strs = mptr->dynsym.strs;

		for (_cnt = 0; _cnt < symn; _cnt++, syms++) {
			if (ELF32_ST_TYPE(syms->st_info) != STT_FUNC
			 || (u_int)addr < syms->st_value
			 || (u_int)addr >= (syms->st_value+syms->st_size))
				continue;
			strcpy(func_name, (strs + syms->st_name));
			*size = syms->st_size;
			*start = (caddr_t)syms->st_value + offset;
			return func_name;
		}

		return NULL;
	}

	return NULL;
}
