/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)gcore.c	1.12	94/12/30 SMI"	/* SVr4.0 1.1	*/

/*******************************************************************

		PROPRIETARY NOTICE (Combined)

This source code is unpublished proprietary information
constituting, or derived under license from AT&T's UNIX(r) System V.
In addition, portions of such source code were derived from Berkeley
4.3 BSD under license from the Regents of the University of
California.



		Copyright Notice

Notice of copyright on this source code product does not indicate
publication.

	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
	          All rights reserved.
********************************************************************/

#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/sysmacros.h>
#include <sys/procfs.h>
#include <sys/elf.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <sys/systeminfo.h>
#include <sys/auxv.h>

#define	TRUE	1
#define	FALSE	0

/* Error returns from Pgrab() */
#define	G_NOPROC	(-1)	/* No such process */
#define	G_ZOMB		(-2)	/* Zombie process */
#define	G_PERM		(-3)	/* No permission */
#define	G_BUSY		(-4)	/* Another process has control */
#define	G_SYS		(-5)	/* System process */
#define	G_SELF		(-6)	/* Process is self */
#define	G_STRANGE	(-7)	/* Unanticipated error, perror() was called */
#define	G_INTR		(-8)	/* Interrupt received while grabbing */

typedef struct {
	Elf32_Nhdr	nhdr;
	char		name[8];
} Elf32_Note;

static	void	alrm(int sig);
static	pid_t	getproc(char *path, char **pdirp);
static	int	dumpcore(int pfd, pid_t pid);
static	int	grabit(char *dir, pid_t pid);
static	void	elfnote(int dfd, int type, char *ptr, int size);
static	int	isprocdir(char *dir);
static	int	Pgrab(char *pdir, pid_t pid);
static	int	Ioctl(int fd, int request, void *arg);

static	char *	command = NULL;		/* name of command ("gcore") */
static	char *	filename = "core";	/* default filename prefix */
static	char *	procdir = "/proc";	/* default PROC directory */
static	int	timeout = FALSE;	/* set TRUE by SIGALRM catcher */
static	long	buf[8*1024];	/* big buffer, used for almost everything */

main(argc, argv)
	int argc;
	char **argv;
{
	int retc = 0;
	int opt;
	int errflg = FALSE;

	command = argv[0];

	/* options */
	while ((opt = getopt(argc, argv, "o:p:")) != EOF) {
		switch (opt) {
		case 'o':		/* filename prefix (default "core") */
			filename = optarg;
			break;
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
		(void) fprintf(stderr,
			"usage:\t%s [-o filename] [-p procdir] pid ...\n",
			command);
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

		/* get the specified pid and its /proc directory */
		pid = getproc(*argv++, &pdir);

		if (pid < 0 || (pfd = grabit(pdir, pid)) < 0) {
			retc++;
			continue;
		}

		if (dumpcore(pfd, pid) != 0)
			retc++;

		(void) close(pfd);
	}

	return(retc);
}

/*
 * get process id and /proc directory.
 * return pid on success, -1 on failure.
 */
static pid_t
getproc(register char * path,	/* number or /proc/nnn */
	char ** pdirp)		/* points to /proc directory on success */
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
	return(pid);
}

static int
grabit(char *dir, pid_t pid)	/* take control of an existing process */
{
	int gcode;

	gcode = Pgrab(dir, pid);

	if (gcode >= 0)
		return(gcode);

	if (gcode == G_INTR)
		return(-1);

	(void) fprintf(stderr, "%s: %s.%ld not dumped", command, filename, pid);
	switch (gcode) {
	case G_NOPROC:
		(void) fprintf(stderr, ": %ld: No such process", pid);
		break;
	case G_ZOMB:
		(void) fprintf(stderr, ": %ld: Zombie process", pid);
		break;
	case G_PERM:
		(void) fprintf(stderr, ": %ld: Permission denied", pid);
		break;
	case G_BUSY:
		(void) fprintf(stderr, ": %ld: Process is traced", pid);
		break;
	case G_SYS:
		(void) fprintf(stderr, ": %ld: System process", pid);
		break;
	case G_SELF:
		(void) fprintf(stderr, ": %ld: Cannot dump self", pid);
		break;
	}
	(void) fputc('\n', stderr);

	return(-1);
}

/*ARGSUSED*/
static void
alrm(int sig)
{
	timeout = TRUE;
}


static int
dumpcore(int pfd,		/* process file descriptor */
	pid_t pid)		/* process-id */
{
	int lwpfd;			/* lwp file descriptor */
	int dfd;			/* dump file descriptor */
	int nsegments;			/* current number of segments */
	Elf32_Ehdr ehdr;		/* ELF header */
	Elf32_Phdr *v = NULL;		/* ELF program header */
	prmap_t *pdp = NULL;
	int nlwp;
	id_t *lwpid = NULL;
	prstatus_t piocstat;
	prpsinfo_t psstat;
	prfpregset_t fpregs;
	ulong hdrsz;
	off_t poffset;
	int nhdrs, i;
	int size, count, ncount;
	int has_fp;
	int has_xrs;
	int xregs_size = 0;
	char *xregs = (char *)NULL;
	int has_platname;
	char platform_name[SYS_NMLN];
	char cname[MAXPATHLEN];
	int nauxv = 0;
	auxv_t *auxv = (auxv_t *)NULL;

	/*
	 * Fetch the memory map and look for text, data, and stack.
	 */
	if (Ioctl(pfd, PIOCNMAP, &nsegments) != 0 || nsegments <= 0) {
		perror("dumpcore(): PIOCNMAP");
		goto bad;
	}
	if ((pdp = (prmap_t *)malloc((nsegments+1)*sizeof(prmap_t))) == NULL)
		goto nomalloc;
	if (Ioctl(pfd, PIOCMAP, pdp) != 0) {
		perror("dumpcore(): PIOCMAP");
		goto bad;
	}

	if (Ioctl(pfd, PIOCSTATUS, &piocstat) != 0) {
		perror("dumpcore(): PIOCSTATUS");
		goto bad;
	}
	if ((nlwp = piocstat.pr_nlwp) == 0)
		nlwp = 1;
	if ((lwpid = (id_t *)malloc((nlwp+1)*sizeof(id_t))) == NULL)
		goto nomalloc;
	if (Ioctl(pfd, PIOCLWPIDS, lwpid) != 0) {
		perror("dumpcore(): PIOCLWPIDS");
		goto bad;
	}

	nhdrs = nsegments + 1;
	hdrsz = nhdrs * sizeof(Elf32_Phdr);

	if ((v = (Elf32_Phdr *)malloc(hdrsz)) == NULL)
		goto nomalloc;
	(void) memset((char *)v, 0, hdrsz);

	(void) memset((char *)&ehdr, 0, sizeof(Elf32_Ehdr));
	ehdr.e_ident[EI_MAG0] = ELFMAG0;
	ehdr.e_ident[EI_MAG1] = ELFMAG1;
	ehdr.e_ident[EI_MAG2] = ELFMAG2;
	ehdr.e_ident[EI_MAG3] = ELFMAG3;
	ehdr.e_ident[EI_CLASS] = ELFCLASS32;
#if defined(sparc)
	ehdr.e_ident[EI_DATA] = ELFDATA2MSB;
#elif defined(i386)
	ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
#endif
	ehdr.e_type = ET_CORE;
#if defined(sparc)
	ehdr.e_machine = EM_SPARC;
#elif defined(i386)
	ehdr.e_machine = EM_386;
#endif
        ehdr.e_version = EV_CURRENT;
        ehdr.e_phoff = sizeof(Elf32_Ehdr);
        ehdr.e_ehsize = sizeof(Elf32_Ehdr);
        ehdr.e_phentsize = sizeof(Elf32_Phdr);
        ehdr.e_phnum = (Elf32_Half)nhdrs;

	/*
	 * Create the core dump file.
	 */
	(void) sprintf(cname, "%s.%ld", filename, pid);
	if ((dfd = creat(cname, 0666)) < 0) {
		perror(cname);
		goto bad;
	}

	if (write(dfd, &ehdr, sizeof(Elf32_Ehdr)) != sizeof(Elf32_Ehdr)) {
		perror("dumpcore(): write");
		goto bad;
	}

	poffset = sizeof(Elf32_Ehdr) + hdrsz;
	v[0].p_type = PT_NOTE;
        v[0].p_flags = PF_R;
        v[0].p_offset = poffset;
	v[0].p_filesz = (sizeof(Elf32_Note) * (1+nlwp) ) +
		roundup(sizeof(prpsinfo_t), sizeof(Elf32_Word)) +
		nlwp*roundup(sizeof(prstatus_t), sizeof(Elf32_Word));

	if (Ioctl(pfd, PIOCNAUXV, &nauxv) != 0) {
		perror("dumpcore(): PIOCNAUXV");
		goto bad;
	} else if (nauxv > 0) {
		v[0].p_filesz += sizeof(Elf32_Note) +
			roundup(nauxv * sizeof (auxv_t), sizeof (Elf32_Word));
		if ((auxv = (auxv_t *)malloc(nauxv * sizeof (auxv_t))) == NULL)
			goto nomalloc;
	}
	has_platname = FALSE;
	if (sysinfo(SI_PLATFORM, platform_name, sizeof (platform_name)) != -1) {
		has_platname = TRUE;
		v[0].p_filesz += sizeof(Elf32_Note) +
			roundup(strlen(platform_name) + 1, sizeof (Elf32_Word));
	}
	has_fp = FALSE;
	if (Ioctl(pfd, PIOCGFPREG, &fpregs) == 0) {
		has_fp = TRUE;
		v[0].p_filesz += nlwp*sizeof(Elf32_Note) +
			nlwp*roundup(sizeof(prfpregset_t), sizeof(Elf32_Word));
	}
	has_xrs = FALSE;
	if ((Ioctl(pfd, PIOCGXREGSIZE, &xregs_size) == 0) && (xregs_size > 0)) {
		has_xrs = TRUE;
		v[0].p_filesz += nlwp*sizeof (Elf32_Note) +
			nlwp*roundup(xregs_size, sizeof (Elf32_Word));
		if ((xregs = (char *)malloc(xregs_size)) == NULL)
			goto nomalloc;
	}
	poffset += v[0].p_filesz;

	for (i = 1; i < nhdrs; i++, pdp++) {
		v[i].p_type = PT_LOAD;
		v[i].p_vaddr = (Elf32_Word) pdp->pr_vaddr;
		size = pdp->pr_size;
		v[i].p_memsz = size;
		if (pdp->pr_mflags & MA_WRITE)
			v[i].p_flags |= PF_W;
		if (pdp->pr_mflags & MA_READ)
			v[i].p_flags |= PF_R;
		if (pdp->pr_mflags & MA_EXEC)
			v[i].p_flags |= PF_X;
		if ((pdp->pr_mflags & MA_READ)
		 && (pdp->pr_mflags & (MA_WRITE|MA_EXEC)) != MA_EXEC
		 && !(pdp->pr_mflags & MA_SHARED)) {
			v[i].p_offset = poffset;
			v[i].p_filesz = size;
			poffset += size;
		}
	}

	if (write(dfd, v, hdrsz) != hdrsz) {
		perror("dumpcore(): write");
		goto bad;
	}

	if (Ioctl(pfd, PIOCPSINFO, &psstat) != 0) {
		perror("dumpcore(): PIOCPSINFO");
		goto bad;
	}
	elfnote(dfd, NT_PRPSINFO, (char *)&psstat, sizeof(prpsinfo_t));

	if (has_platname) {
		elfnote(dfd, NT_PLATFORM, platform_name,
			strlen(platform_name) + 1);
	}

	if (nauxv > 0) {
		if (Ioctl(pfd, PIOCAUXV, auxv) != 0) {
			perror("dumpcore(): PIOCAUXV");
			goto bad;
		}
		elfnote(dfd, NT_AUXV, (char *)auxv, nauxv * sizeof (auxv_t));
	}

	for (i = 0; i < nlwp; i++) {
		if ((lwpfd = Ioctl(pfd, PIOCOPENLWP, &lwpid[i])) < 0) {
			perror("dumpcore(): PIOCOPENLWP");
			goto bad;
		}

		if (Ioctl(lwpfd, PIOCSTATUS, &piocstat) != 0) {
			perror("dumpcore(): PIOCSTATUS");
			goto bad;
		}
		elfnote(dfd, NT_PRSTATUS, (char *)&piocstat,
				sizeof(prstatus_t));

		if (has_fp) {
			if (Ioctl(lwpfd, PIOCGFPREG, &fpregs) != 0)
				(void) memset((char *)&fpregs, 0,
						sizeof(prfpregset_t));
			elfnote(dfd, NT_PRFPREG, (char *)&fpregs,
					sizeof(prfpregset_t));
		}

		if (has_xrs) {
			if (Ioctl(lwpfd, PIOCGXREG, xregs) != 0)
				(void) memset(xregs, 0, xregs_size);
			elfnote(dfd, NT_PRXREG, xregs, xregs_size);
		}

		(void) close(lwpfd);
	}

	/*
	 * Dump data and stack
	 */
	for (i = 1; i < nhdrs; i++) {
		if (v[i].p_filesz == 0)
			continue;
		(void) lseek(pfd, v[i].p_vaddr, 0);
		count = v[i].p_filesz;
		while (count > 0) {
			ncount = (count < sizeof(buf)) ?
					count : sizeof(buf);
			if ((ncount = read(pfd, buf, ncount)) <= 0)
				break;
			(void) write(dfd, buf, ncount);
			count -= ncount;
		}
	}

	(void) fprintf(stderr,"%s: %s.%ld dumped\n", command, filename, pid);
	(void) close(dfd);
	free((char *)v);
	if (xregs != NULL)
		free(xregs);
	if (auxv != NULL)
		free((char *)auxv);
	return(0);
nomalloc:
	(void) fprintf(stderr, "dumpcore(): malloc() failed\n");
bad:
	if (pdp != NULL)
		free((char *)pdp);
	if (lwpid != NULL)
		free((char *)lwpid);
	if (v != NULL)
		free((char *)v);
	if (xregs != NULL)
		free(xregs);
	if (auxv != NULL)
		free((char *)auxv);
	return(-1);
}


static void
elfnote(int dfd, int type, char *ptr, int size)
{
	Elf32_Note note;		/* ELF note */

	(void) memset((char *)&note, 0, sizeof(Elf32_Note));
	(void) memcpy(note.name, "CORE", 4);
	note.nhdr.n_type = type;
	note.nhdr.n_namesz = 5;
	note.nhdr.n_descsz = roundup(size, sizeof(Elf32_Word));
	(void) write(dfd, &note, sizeof(Elf32_Note));
	(void) write(dfd, ptr, roundup(size, sizeof(Elf32_Word)));
}



static int
isprocdir(char *dir)	/* return TRUE iff dir is a PROC directory */
{
	/* This is based on the fact that "/proc/0" and "/proc/00" are the */
	/* same file, namely process 0, and are not linked to each other. */

	struct stat stat1;	/* dir/0  */
	struct stat stat2;	/* dir/00 */
	char * path = (char *)&buf[0];
	register char * p;

	/* make a copy of the directory name without trailing '/'s */
	if (dir == NULL)
		(void) strcpy(path, ".");
	else {
		(void) strcpy(path, dir);
		p = path + strlen(path);
		while (p > path && *--p == '/')
			*p = '\0';
		if (*path == '\0')
			(void) strcpy(path, ".");
	}

	/* append "/0" to the directory path and stat() the file */
	p = path + strlen(path);
	*p++ = '/';
	*p++ = '0';
	*p = '\0';
	if (stat(path, &stat1) != 0)
		return(FALSE);

	/* append "/00" to the directory path and stat() the file */
	*p++ = '0';
	*p = '\0';
	if (stat(path, &stat2) != 0)
		return(FALSE);

	/* see if we ended up with the same file */
	if (stat1.st_dev   != stat2.st_dev
	 || stat1.st_ino   != stat2.st_ino
	 || stat1.st_mode  != stat2.st_mode
	 || stat1.st_nlink != stat2.st_nlink
	 || stat1.st_uid   != stat2.st_uid
	 || stat1.st_gid   != stat2.st_gid
	 || stat1.st_size  != stat2.st_size)
		return(FALSE);

	return(TRUE);
}

/* grab existing process */
static int
Pgrab(	char *pdir,			/* /proc directory */
	register pid_t pid)		/* UNIX process ID */
{
	register int pfd = -1;
	int err;
	prstatus_t prstat;
	char * procname = (char *)&buf[0];

again:	/* Come back here if we lose it in the Window of Vulnerability */
	if (pfd >= 0) {
		(void) close(pfd);
		pfd = -1;
	}

	/* generate the /proc/pid filename */
	(void) sprintf(procname, "%s/%ld", pdir, pid);

	/* Request exclusive open to avoid grabbing someone else's	*/
	/* process and to prevent others from interfering afterwards.	*/
	if ((pfd = open(procname, (O_RDWR|O_EXCL))) < 0) {
		switch (errno) {
		case EBUSY:
			return(G_BUSY);
		case ENOENT:
			return(G_NOPROC);
		case EACCES:
		case EPERM:
			return(G_PERM);
		default:
			perror("Pgrab open()");
			return(G_STRANGE);
		}
	}

	/* Make sure the filedescriptor is not one of 0, 1, or 2 */
	if (0 <= pfd && pfd <= 2) {
		int dfd = fcntl(pfd, F_DUPFD, 3);

		(void) close(pfd);
		if (dfd < 0) {
			perror("Pgrab fcntl()");
			return(G_STRANGE);
		}
		pfd = dfd;
	}

	/* ---------------------------------------------------- */
	/* We are now in the Window of Vulnerability (WoV).	*/
	/* The process may exec() a setuid/setgid or unreadable	*/
	/* object file between the open() and the PIOCSTOP.	*/
	/* We will get EAGAIN in this case and must start over.	*/
	/* ---------------------------------------------------- */

	/*
	 * Get the process's status.
	 */
	if (Ioctl(pfd, PIOCSTATUS, &prstat) != 0) {
		int rc;

		if (errno == EAGAIN)	/* WoV */
			goto again;

		if (errno == ENOENT)	/* Don't complain about zombies */
			rc = G_ZOMB;
		else {
			perror("Pgrab PIOCSTATUS");
			rc = G_STRANGE;
		}
		(void) close(pfd);
		return(rc);
	}

	/*
	 * If the process is a system process, we can't dump it.
	 */
	if (prstat.pr_flags & PR_ISSYS) {
		(void) close(pfd);
		return(G_SYS);
	}

	/*
	 * We can't dump ourself.
	 */
	if (pid == getpid()) {
		/*
		 * Verify that the process is really ourself:
		 * Set a magic number, read it through the
		 * /proc file and see if the results match.
		 */
		long magic1 = 0;
		long magic2 = 2;

		if (lseek(pfd, (long)&magic1, 0) == (long)&magic1
		 && read(pfd, (char *)&magic2, sizeof(magic2)) == sizeof(magic2)
		 && magic2 == 0
		 && (magic1 = 0xfeedbeef)
		 && lseek(pfd, (long)&magic1, 0) == (long)&magic1
		 && read(pfd, (char *)&magic2, sizeof(magic2)) == sizeof(magic2)
		 && magic2 == 0xfeedbeef) {
			(void) close(pfd);
			return(G_SELF);
		}
	}

	/*
	 * If the process is already stopped or has been directed
	 * to stop via /proc, there is nothing more to do.
	 */
	if (prstat.pr_flags & (PR_ISTOP|PR_DSTOP))
		return(pfd);

	/*
	 * Mark the process run-on-last-close so
	 * it runs even if we die from SIGKILL.
	 */
	if (Ioctl(pfd, PIOCSRLC, NULL) != 0) {
		int rc;

		if (errno == EAGAIN)	/* WoV */
			goto again;

		if (errno == ENOENT)	/* Don't complain about zombies */
			rc = G_ZOMB;
		else {
			perror("Pgrab PIOCSRLC");
			rc = G_STRANGE;
		}
		(void) close(pfd);
		return(rc);
	}

	/*
	 * Direct the process to stop.
	 * Set an alarm to avoid waiting forever.
	 */
	timeout = FALSE;
	err = 0;
	(void) alarm(2);
	if (Ioctl(pfd, PIOCSTOP, &prstat) == 0)
		(void) alarm(0);
	else {
		err = errno;
		(void) alarm(0);
		if (err == EINTR
		 && timeout
		 && Ioctl(pfd, PIOCSTATUS, &prstat) != 0) {
			timeout = FALSE;
			err = errno;
		}
	}

	if (err) {
		int rc;

		switch (err) {
		case EAGAIN:		/* we lost control of the the process */
			goto again;
		case EINTR:		/* timeout or user typed DEL */
			rc = G_INTR;
			break;
		case ENOENT:
			rc = G_ZOMB;
			break;
		default:
			perror("Pgrab PIOCSTOP");
			rc = G_STRANGE;
			break;
		}
		if (!timeout || err != EINTR) {
			(void) close(pfd);
			return(rc);
		}
	}

	/*
	 * Process should either be stopped via /proc or
	 * there should be an outstanding stop directive.
	 */
	if ((prstat.pr_flags & (PR_ISTOP|PR_DSTOP)) == 0) {
		(void) fprintf(stderr, "Pgrab: process is not stopped\n");
		(void) close(pfd);
		return(G_STRANGE);
	}

	return(pfd);
}

static int
Ioctl(int fd, int request, void *arg)	/* deal with RFS congestion */
{
	register int rc;

	for(;;) {
		if ((rc = ioctl(fd, request, arg)) != -1
		 || errno != ENOMEM)
			return(rc);
	}
}
