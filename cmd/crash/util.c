/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 *		Copyright (C) 1991  Sun Microsystems, Inc
 *			All rights reserved.
 *		Notice of copyright on this source code
 *		product does not indicate publication.
 *
 *		RESTRICTED RIGHTS LEGEND:
 *   Use, duplication, or disclosure by the Government is subject
 *   to restrictions as set forth in subparagraph (c)(1)(ii) of
 *   the Rights in Technical Data and Computer Software clause at
 *   DFARS 52.227-7013 and in similar clauses in the FAR and NASA
 *   FAR Supplement.
 */

#pragma	ident	"@(#)util.c	1.15	95/06/01 SMI"	/* SVr4.0 1.11.6.1 */

/*
 * This file contains code for utilities used by more than one crash
 * function.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/param.h>
#include <setjmp.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/cpuvar.h>
#include <sys/var.h>
#include <sys/proc.h>
#include <sys/elf.h>
#include <vm/as.h>
#include <sys/vmparam.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/kmem_impl.h>
#include "crash.h"
#if i386
#include <sys/dumphdr.h>
#endif /* i386 */

struct procslot *slottab;
void exit();
void free();

/* close pipe and reset file pointers */
int
resetfp(void)
{
	if (opipe == 1) {
		pclose(fp);
		signal(SIGPIPE, pipesig);
		opipe = 0;
		fp = stdout;
	} else {
		if ((fp != stdout) && (fp != rp) && (fp != NULL)) {
			fclose(fp);
			fp = stdout;
		}
	}
	return (0);
}

/* signal handling */
/*ARGSUSED*/
void
sigint(int sig)
{
	extern int *stk_bptr;

	signal(SIGINT, sigint);

	if (stk_bptr)
		free((char *)stk_bptr);
	fflush(fp);
	resetfp();
	fprintf(fp, "\n");
	longjmp(jmp, 0);
}

/* used in init.c to exit program */
/* VARARGS1 */
int
fatal(string, arg1, arg2, arg3)
char *string;
int arg1, arg2, arg3;
{
#if i386
	extern struct dumphdr *dhp;
#endif /* i386 */

	fprintf(stderr, "crash: ");
	fprintf(stderr, string, arg1, arg2, arg3);
#if i386
	if (dhp)
		free(dhp);	/* malloc'd in get_dump_info() */
#endif /* i386 */
	exit(1);
	/* NOTREACHED */
}

/* string to hexadecimal long conversion */
long
hextol(char *s)
{
	int i, j;

	for (j = 0; s[j] != '\0'; j++)
		if ((s[j] < '0' || s[j] > '9') && (s[j] < 'a' || s[j] > 'f') &&
			(s[j] < 'A' || s[j] > 'F'))
			break;
	if (s[j] != '\0' || sscanf(s, "%x", &i) != 1) {
		prerrmes("%c is not a digit or letter a - f\n", s[j]);
		return (-1);
	}
	return (i);
}


/* string to decimal long conversion */
long
stol(char *s)
{
	int i, j;

	for (j = 0; s[j] != '\0'; j++)
		if ((s[j] < '0' || s[j] > '9'))
			break;
	if (s[j] != '\0' || sscanf(s, "%d", &i) != 1) {
		prerrmes("%c is not a digit 0 - 9\n", s[j]);
		return (-1);
	}
	return (i);
}


/* string to octal long conversion */
long
octol(char *s)
{
	int i, j;

	for (j = 0; s[j] != '\0'; j++)
		if ((s[j] < '0' || s[j] > '7'))
			break;
	if (s[j] != '\0' || sscanf(s, "%o", &i) != 1) {
		prerrmes("%c is not a digit 0 - 7\n", s[j]);
		return (-1);
	}
	return (i);
}


/* string to binary long conversion */
long
btol(char *s)
{
	int i, j;

	i = 0;
	for (j = 0; s[j] != '\0'; j++)
		switch (s[j]) {
			case '0':
				i = i << 1;
				break;
			case '1':
				i = (i << 1) + 1;
				break;
			default:
				prerrmes("%c is not a 0 or 1\n", s[j]);
				return (-1);
		}
	return (i);
}

/* string to number conversion */
long
strcon(char *string, char format)
{
	char *s;

	s = string;
	if (*s == '0') {
		if (strlen(s) == 1)
			return (0);
		switch (*++s) {
			case 'X':
			case 'x':
				format = 'h';
				s++;
				break;
			case 'B':
			case 'b':
				format = 'b';
				s++;
				break;
			case 'D':
			case 'd':
				format = 'd';
				s++;
				break;
			default:
				format = 'o';
		}
	}
	if (!format)
		format = 'd';
	switch (format) {
		case 'h':
			return (hextol(s));
		case 'd':
			return (stol(s));
		case 'o':
			return (octol(s));
		case 'b':
			return (btol(s));
		default:
			return (-1);
	}
}

/* lseek and read */
int
readmem(u_int addr, int mode, int proc, void *buffer, u_int size, char *name)
{
	if ((proc != -1) && (addr < USRSTACK)) {
		struct proc procbuf;
		struct as asbuf;

		readbuf(-1, procv + proc * sizeof (procbuf), 0, -1,
			(char *)&procbuf, sizeof (procbuf), "proc table");
		if (!procbuf.p_stat)
			error("process slot %d doesn't exist\n", proc);
		if (procbuf.p_as == NULL)
			error("no context on slot %d\n", proc);
		readmem((unsigned)procbuf.p_as, 1, -1, (char *)&asbuf,
			sizeof (asbuf), "address space");
		if (kvm_as_read(kd, &asbuf, addr, buffer, size) != size)
			error("read error on %s at %x\n", name, addr);
	} else if (mode) {
		if (kvm_read(kd, addr, buffer, size) != size)
			error("read error on %s at %x\n", name, addr);
	} else {
		if (lseek(mem, addr, 0) == -1)
			error("seek error on address %x\n", addr);
		if (read(mem, buffer, size) != size)
			error("read error on %s\n", name);
	}
}

/* lseek to symbol name and read */
int
readsym(char *sym, void *buffer, unsigned size)
{
	register unsigned addr;
	register Elf32_Sym *np;

	if (!(np = symsrch(sym)))
		error("%s not found in symbol table\n", sym);
	readmem(np->st_value, 1, -1, buffer, size, sym);
}

/* lseek and read into given buffer */
int
readbuf(u_int addr, u_int offset, int phys, int proc,
	void *buffer, u_int size, char *name)
{
	if ((phys || !Virtmode) && (addr != -1))
		readmem(addr, 0, proc, buffer, size, name);
	else if (addr != -1)
		readmem(addr, 1, proc, buffer, size, name);
	else
		readmem(offset, 1, proc, buffer, size, name);
	return (0);
}

/* error handling */
/* VARARGS1 */
int
error(string, arg1, arg2, arg3)
char *string;
int arg1, arg2, arg3;
{
#if i386
	extern struct dumphdr *dhp;
#endif /* i386 */

	if (rp)
		fprintf(stdout, string, arg1, arg2, arg3);
	fprintf(fp, string, arg1, arg2, arg3);
	fflush(fp);
	resetfp();
#if i386
	if (dhp)
		free(dhp);	/* malloc'd in get_dump_info() */
#endif /* i386 */
	longjmp(jmp, 0);
}


/* print error message */
/* VARARGS1 */
int
prerrmes(string, arg1, arg2, arg3)
char *string;
int arg1, arg2, arg3;
{

	if ((rp && (rp != stdout)) || (fp != stdout))
		fprintf(stdout, string, arg1, arg2, arg3);
	fprintf(fp, string, arg1, arg2, arg3);
	fflush(fp);
}


/* simple arithmetic expression evaluation ( +  - & | * /) */
long
eval(char *string)
{
	int j, i;
	char rand1[ARGLEN];
	char rand2[ARGLEN];
	char *op;
	long addr1, addr2;
	Elf32_Sym *sp;
	extern char *strpbrk();

	if (string[strlen(string) - 1] != ')') {
		prerrmes("(%s is not a well-formed expression\n", string);
		return (-1);
	}
	if (!(op = strpbrk(string, "+-&|*/"))) {
		prerrmes("(%s is not an expression\n", string);
		return (-1);
	}
	for (j = 0, i = 0; string[j] != *op; j++, i++) {
		if (string[j] == ' ')
			--i;
		else
			rand1[i] = string[j];
	}
	rand1[i] = '\0';
	j++;
	for (i = 0; string[j] != ')'; j++, i++) {
		if (string[j] == ' ')
			--i;
		else
			rand2[i] = string[j];
	}
	rand2[i] = '\0';
	if (!strlen(rand2) || strpbrk(rand2, "+-&|*/")) {
		prerrmes("(%s is not a well-formed expression\n", string);
		return (-1);
	}
	if (sp = symsrch(rand1))
		addr1 = sp->st_value;
	else if ((addr1 = strcon(rand1, NULL)) == -1)
		return (-1);
	if (sp = symsrch(rand2))
		addr2 = sp->st_value;
	else if ((addr2 = strcon(rand2, NULL)) == -1)
		return (-1);
	switch (*op) {
		case '+':
			return (addr1 + addr2);
		case '-':
			return (addr1 - addr2);
		case '&':
			return (addr1 & addr2);
		case '|':
			return (addr1 | addr2);
		case '*':
			return (addr1 * addr2);
		case '/':
			if (addr2 == 0) {
				prerrmes("cannot divide by 0\n");
				return (-1);
			}
			return (addr1 / addr2);
	}
	return (-1);
}

static maketab = 1;

void
makeslottab(void)
{
	proc_t *prp, pr;
	struct pid pid;
	Elf32_Sym *practive;
	register i;

	if (maketab) {
		maketab = 0;
		if (!(practive = symsrch("practive")))
			fatal("practive not found in symbol table\n");
		slottab = (struct procslot *)
			malloc(vbuf.v_proc * sizeof (struct procslot));
		for (i = 0; i < vbuf.v_proc; i++) {
			slottab[i].p = 0;
			slottab[i].pid = -1;
		}
		readmem((long)practive->st_value, 1, -1, (char *)&prp,
			sizeof (proc_t *), "practive");
		for (; prp != NULL; prp = pr.p_next) {
			readmem((unsigned)prp, 1, -1, (char *)&pr,
				sizeof (struct proc), "proc table");
			readmem((unsigned)pr.p_pidp, 1, -1, (char *)&pid,
				sizeof (struct pid), "pid table");
			i = pid.pid_prslot;
			if (i < 0 || i >= vbuf.v_proc)
				fatal("process slot out of bounds\n");
			slottab[i].p = prp;
			slottab[i].pid = pid.pid_id;
		}
	}
}

pid_t
slot_to_pid(int slot)
{
	if (slot < 0 || slot >= vbuf.v_proc)
		return (NULL);
	if (maketab)
		makeslottab();
	return (slottab[slot].pid);
}

proc_t *
slot_to_proc(int slot)
{
	if (slot < 0 || slot >= vbuf.v_proc)
		return (NULL);
	if (maketab)
		makeslottab();
	return (slottab[slot].p);
}

/* convert proc address to proc slot */
int
proc_to_slot(long addr)
{
	int i;

	if (addr == NULL)
		return (-1);

	if (maketab)
		makeslottab();

	for (i = 0; i < vbuf.v_proc; i++)
		if (slottab[i].p == (proc_t *)addr)
			return (i);

	return (-1);

}

/* get current process slot number */
int
getcurproc(void)
{
	struct _kthread  thread;

	/* Read the current thread structure */
	readmem((unsigned)Curthread, 1, -1, (char *)&thread, sizeof (thread),
		"current thread");

	return (proc_to_slot((long)thread.t_procp));
}

kthread_id_t
getcurthread(void)
{
	uint cpu_addr = 0;
	struct cpu cpu;
	struct cpu **cpup;
	static Elf32_Sym *panic_thread_sym;
	kthread_id_t panic_thread;

	if (!cpu_sym)
		if ((cpu_sym = symsrch("cpu")) == NULL)
			error("Could not find symbol cpu\n");

	if (!panic_thread_sym)
		if ((panic_thread_sym = symsrch("panic_thread")) == NULL)
			error("Could not find symbol panic_thread\n");
	/*
	 * if panic_thread is set, that's our current thread.
	 */
	readmem(panic_thread_sym->st_value, 1, -1, (char *)&panic_thread,
		sizeof (panic_thread), "panic_thread address");

	if (panic_thread)
		return (panic_thread);

	/*
	 * if panic_thread is not set, look through the cpu array until
	 * we find a non-null entry (the cpu array may be sparse (e.g. sun4d)).
	 * read the cpu struct and return the cpu's current thread pointer.
	 */
	for (cpup = (struct cpu **)cpu_sym->st_value; !cpu_addr; cpup++)
		readmem((unsigned)cpup, 1, -1, (char *)&cpu_addr,
		    sizeof (uint), "cpu address");

	readmem(cpu_addr, 1, -1, (char *)&cpu, sizeof (cpu), "cpu structure");

	return (cpu.cpu_thread);

}


/* determine valid process table entry */
int
procntry(int slot, struct proc *prbuf)
{
	long addr;

	if (slot == -1)
		slot = getcurproc();

	if (slot_to_proc(slot) == NULL)
		error(" %d is not a valid process\n", slot);
}

/* argument processing from **args */
long
getargs(int max, long *arg1, long *arg2, int phys)
{
	Elf32_Sym *sp;
	long slot;

	if (maketab)
		makeslottab();

	/* range */
	if (strpbrk(args[optind], "-")) {
		range(max, arg1, arg2);
		return;
	}
	/* expression */
	if (*args[optind] == '(') {
		*arg1 = (eval(++args[optind]));
		return;
	}
	/* symbol */
	if ((sp = symsrch(args[optind])) != NULL) {
		*arg1 = (sp->st_value);
		return;
	}
	if (isasymbol(args[optind])) {
		prerrmes("%s not found in symbol table\n", args[optind]);
		*arg1 = -1;
		return;
	}
	/* slot number */
	if ((slot = strcon(args[optind], 'h')) == -1) {
		*arg1 = -1;
		return;
	}
	if ((slot >= Start->st_value) || ((phys || !Virtmode) &&
	    ((longlong_t)slot >= vtop(Start->st_value, Procslot)))) {
		/* address */
		*arg1 = slot;
	} else {
		if ((slot = strcon(args[optind], 'd')) == -1) {
			*arg1 = -1;
			return;
		}
		if ((slot < max) && (slot >= 0)) {
			*arg1 = slot;
			return;
		} else {
			prerrmes("%d is out of range\n", slot);
			*arg1 = -1;
			return;
		}
	}
}

/* get slot number in table from address */
int
getslot(long addr, long base, int size, int phys, long max)
{
	longlong_t pbase;
	int slot;

	if (phys || !Virtmode) {
		pbase = vtop(base, Procslot);
		if (pbase == -1LL)
			error("%x is an invalid address\n", base);
		if (pbase > (longlong_t)0xffffffff)
			error("%x has a physical address >32 bits\n", base);
		slot = ((longlong_t)addr - pbase) / size;
	} else
		slot = (addr - base) / size;
	if ((slot >= 0) && (slot < max))
		return (slot);
	else
		return (-1);
}

/*
 * fopen a file for output.  use process's real group id so that
 * crash users cannot write to files writable by group sys.
 */
FILE *
fopen_output(char *filename)
{
	FILE	*fp;
	gid_t	saved_gid = getegid();

	if (setgid(getgid()) < 0)
		error("unable to set gid to real gid\n");

	fp = fopen(filename, "a");

	if (setgid(saved_gid) < 0)
		fprintf(stdout, "unable to reset gid to effective gid\n");

	return (fp);
}


/* file redirection */
void
redirect(void)
{
	int i;
	FILE *ofp;

	ofp = fp;
	if (opipe == 1) {
		fprintf(stdout, "file redirection (-w) option ignored\n");
		return;
	}
	if (fp = fopen_output(optarg)) {
		fprintf(fp, "\n> ");
		for (i = 0; i < argcnt; i++)
			fprintf(fp, "%s ", args[i]);
		fprintf(fp, "\n");
	} else {
		fp = ofp;
		error("unable to open %s\n", optarg);
	}
}


/*
 * putch() recognizes escape sequences as well as characters and prints the
 * character or equivalent action of the sequence.
 */
int
putch(char c)
{
	c &= 0377;
	if (c < 040 || c > 0176) {
		fprintf(fp, "\\");
		switch (c) {
			case '\0':
				c = '0';
				break;
			case '\t':
				c = 't';
				break;
			case '\n':
				c = 'n';
				break;
			case '\r':
				c = 'r';
				break;
			case '\b':
				c = 'b';
				break;
			default:
				c = '?';
				break;
		}
	} else
		fprintf(fp, " ");
	fprintf(fp, "%c ", c);
}

/* sets process to input argument */
int
setproc(void)
{
	int slot;

	if ((slot = strcon(optarg, 'd')) == -1)
		error("\n");
	if ((slot > vbuf.v_proc) || (slot < 0))
		error("%d out of range\n", slot);
	return (slot);
}

/* check to see if string is a symbol or a hexadecimal number */
int
isasymbol(char *string)
{
	int i;

	for (i = (int)strlen(string); i > 0; i--)
		if (!isxdigit(*string++))
			return (1);
	return (0);
}


/* convert a string into a range of slot numbers */
int
range(int max, long *begin, long *end)
{
	int i, j, len, pos;
	char string[ARGLEN];
	char temp1[ARGLEN];
	char temp2[ARGLEN];

	(void) strcpy(string, args[optind]);
	len = (int)strlen(string);
	if ((*string == '-') || (string[len - 1] == '-')) {
		fprintf(fp, "%s is an invalid range\n", string);
		*begin = -1;
		return;
	}
	pos = strcspn(string, "-");
	for (i = 0; i < pos; i++)
		temp1[i] = string[i];
	temp1[i] = '\0';
	for (j = 0, i = pos + 1; i < len; j++, i++)
		temp2[j] = string[i];
	temp2[j] = '\0';
	if ((*begin = (int)stol(temp1)) == -1)
		return;
	if ((*end = (int)stol(temp2)) == -1) {
		*begin = -1;
		return;
	}
	if (*begin > *end) {
		fprintf(fp, "%d-%d is an invalid range\n", *begin, *end);
		*begin = -1;
		return;
	}
	if (*end >= max)
		*end = max - 1;
}

/*
 * Find the kernel address of the cache with the specified name
 */
kmem_cache_t *
kmem_cache_find(char *name)
{
	Elf32_Sym *kmem_null_cache_sym;
	u_int kmem_null_cache_addr;
	kmem_cache_t c, *cp;

	if ((kmem_null_cache_sym = symsrch("kmem_null_cache")) == 0)
		(void) error("kmem_null_cache not in symbol table\n");
	kmem_null_cache_addr = kmem_null_cache_sym->st_value;
	if (kvm_read(kd, kmem_null_cache_addr, (char *)&c, sizeof (c)) == -1)
		return (NULL);
	for (cp = c.cache_next; cp != (kmem_cache_t *)kmem_null_cache_addr;
	    cp = c.cache_next) {
		if (kvm_read(kd, (u_int)cp, (char *)&c, sizeof (c)) == -1)
			return (NULL);
		if (strcmp(c.cache_name, name) == 0)
			return (cp);
	}
	return (NULL);
}

/*
 * Find the kernel addresses of all caches with the specified name prefix
 */
int
kmem_cache_find_all(char *name, kmem_cache_t **cache_list, int max_caches)
{
	Elf32_Sym *kmem_null_cache_sym;
	u_int kmem_null_cache_addr;
	kmem_cache_t c, *cp;
	int ncaches = 0;
	int len = strlen(name);

	if ((kmem_null_cache_sym = symsrch("kmem_null_cache")) == 0)
		(void) error("kmem_null_cache not in symbol table\n");
	kmem_null_cache_addr = kmem_null_cache_sym->st_value;
	if (kvm_read(kd, kmem_null_cache_addr, (char *)&c, sizeof (c)) == -1)
		return (0);
	for (cp = c.cache_next; cp != (kmem_cache_t *)kmem_null_cache_addr;
	    cp = c.cache_next) {
		if (kvm_read(kd, (u_int)cp, (char *)&c, sizeof (c)) == -1)
			return (NULL);
		if (strncmp(c.cache_name, name, len) == 0) {
			if (ncaches >= max_caches)
				return (ncaches);
			cache_list[ncaches++] = cp;
		}
	}
	return (ncaches);
}

/*
 * Find the kernel addresses of all caches with the specified name prefix
 * that does not match a name prefix in the exclude list.
 * excludelist is a NULL terminated array of strings.
 */
int
kmem_cache_find_all_excl(char *name, char **excludelist,
		    kmem_cache_t **cache_list, int max_caches)
{
	Elf32_Sym *kmem_null_cache_sym;
	u_int kmem_null_cache_addr;
	kmem_cache_t c, *cp;
	int ncaches = 0;
	int namelen = strlen(name);

	if ((kmem_null_cache_sym = symsrch("kmem_null_cache")) == 0)
		(void) error("kmem_null_cache not in symbol table\n");
	kmem_null_cache_addr = kmem_null_cache_sym->st_value;
	if (kvm_read(kd, kmem_null_cache_addr, (char *)&c, sizeof (c)) == -1)
		return (0);
	for (cp = c.cache_next; cp != (kmem_cache_t *)kmem_null_cache_addr;
	    cp = c.cache_next) {
		if (kvm_read(kd, (u_int)cp, (char *)&c, sizeof (c)) == -1)
			return (NULL);
		/*
		 * If it matches name but does not match exclude add it to
		 * the list.
		 */
		if (strncmp(c.cache_name, name, namelen) == 0) {
			int i;
			char *excl;

			for (i = 0; excludelist[i] != NULL; i++) {
				excl = excludelist[i];

				if (strncmp(c.cache_name, excl,
					    strlen(excl)) == 0) {
					i = -1;
					break;
				}
			}
			if (i == -1)
				continue;

			if (ncaches >= max_caches)
				return (ncaches);
			cache_list[ncaches++] = cp;
		}
	}
	return (ncaches);
}

/*
 * Apply func to each allocated object in the specified kmem cache
 */
int
kmem_cache_apply(kmem_cache_t *kcp, void (*func)(void *kaddr, void *buf))
{
	kmem_cache_t *cp;
	kmem_magazine_t *kmp, *mp;
	kmem_slab_t s, *sp;
	kmem_bufctl_t bc, *bcp;
	void *buf, *ubase, *kbase, **maglist;
	int magsize, magbsize, magcnt, magmax;
	int chunks, refcnt, flags, bufsize, chunksize, i, cpu_seqid;
	char *valid;
	int errcode = -1;
	Elf32_Sym *ncpus_sym;
	u_int ncpus_addr;
	int ncpus, csize;

	if ((ncpus_sym = symsrch("ncpus")) == 0)
		(void) error("ncpus not in symbol table\n");
	ncpus_addr = ncpus_sym->st_value;
	if (kvm_read(kd, ncpus_addr, (char *)&ncpus, sizeof (int)) == -1)
		return (-1);

	csize = KMEM_CACHE_SIZE(ncpus);
	cp = malloc(csize);

	if (kvm_read(kd, (u_long)kcp, (void *)cp, csize) == -1)
		goto out1;

	magsize = cp->cache_magazine_size;
	magbsize = sizeof (kmem_magazine_t) + (magsize - 1) * sizeof (void *);
	mp = malloc(magbsize);

	magmax = (cp->cache_fmag_total + 2 * ncpus + 100) * magsize;
	maglist = malloc(magmax * sizeof (void *));
	magcnt = 0;

	kmp = cp->cache_fmag_list;
	while (kmp != NULL) {
		if (kvm_read(kd, (u_long)kmp, (void *)mp, magbsize) == -1)
			goto out2;
		for (i = 0; i < magsize; i++) {
			maglist[magcnt] = mp->mag_round[i];
			if (++magcnt > magmax)
				goto out2;
		}
		kmp = mp->mag_next;
	}

	for (cpu_seqid = 0; cpu_seqid < ncpus; cpu_seqid++) {
		kmem_cpu_cache_t *ccp = &cp->cache_cpu[cpu_seqid];
		if (ccp->cc_rounds <= 0)
			break;
		if ((kmp = ccp->cc_loaded_mag) == NULL)
			break;
		if (kvm_read(kd, (u_long)kmp, (void *)mp, magbsize) == -1)
			goto out2;
		for (i = 0; i < ccp->cc_rounds; i++) {
			maglist[magcnt] = mp->mag_round[i];
			if (++magcnt > magmax)
				goto out2;
		}
		if ((kmp = ccp->cc_full_mag) == NULL)
			break;
		if (kvm_read(kd, (u_long)kmp, (void *)mp, magbsize) == -1)
			goto out2;
		for (i = 0; i < magsize; i++) {
			maglist[magcnt] = mp->mag_round[i];
			if (++magcnt > magmax)
				goto out2;
		}
	}

	flags = cp->cache_flags;
	bufsize = cp->cache_bufsize;
	chunksize = cp->cache_chunksize;
	valid = malloc(cp->cache_slabsize / bufsize);
	ubase = malloc(cp->cache_slabsize + sizeof (kmem_bufctl_t));

	sp = cp->cache_nullslab.slab_next;
	while (sp != &kcp->cache_nullslab) {
		if (kvm_read(kd, (u_long)sp, (void *)&s, sizeof (s)) == -1 ||
		    s.slab_cache != kcp)
			goto out3;
		chunks = s.slab_chunks;
		refcnt = s.slab_refcnt;
		kbase = s.slab_base;
		if (kvm_read(kd, (u_long)kbase, (void *)ubase,
		    chunks * chunksize) == -1)
			goto out3;
		memset(valid, 1, chunks);
		bcp = s.slab_head;
		for (i = refcnt; i < chunks; i++) {
			if (flags & KMF_HASH) {
				if (kvm_read(kd, (u_long)bcp, (void *)&bc,
				    sizeof (bc)) == -1)
					goto out3;
				buf = bc.bc_addr;
			} else {
				bc = *((kmem_bufctl_t *)
					((int)bcp - (int)kbase + (int)ubase));
				buf = (void *)((int)bcp - cp->cache_offset);
			}
			valid[((int)buf - (int)kbase) / chunksize] = 0;
			bcp = bc.bc_next;
		}
		for (i = 0; i < chunks; i++) {
			void *kbuf = (char *)kbase + i * chunksize;
			void *ubuf = (char *)ubase + i * chunksize;
			int m;
			if (!valid[i])
				continue;
			for (m = 0; m < magcnt; m++)
				if (kbuf == maglist[m])
					break;
			if (m == magcnt)
				(*func)(kbuf, ubuf);
		}
		sp = s.slab_next;
	}
	errcode = 0;
out3:
	free(valid);
	free(ubase);
out2:
	free(mp);
	free(maglist);
out1:
	free(cp);
	return (errcode);
}

/*
 * Apply func to each allocated object in the specified kmem cache.
 * Assumes that KMF_AUDIT is set which implies that KMF_HASH is set.
 */
int
kmem_cache_audit_apply(kmem_cache_t *cp,
		void (*func)(void *kaddr, void *buf, u_int size,
		kmem_bufctl_audit_t *bcp))
{
	kmem_bufctl_audit_t *bcp;
	kmem_cache_t c;
	kmem_slab_t *sp, s;
	kmem_buftag_t *btp;
	int i, chunks, chunksize, slabsize;
	void *kbase, *ubase;
	int bufsize;

	if (kvm_read(kd, (u_long)cp, (void *)&c, sizeof (c)) == -1) {
		perror("kvm_read kmem_cache");
		return (-1);
	}
	if (!(c.cache_flags & KMF_AUDIT)) {
		printf("Not KMF_AUDIT for cache %s\n", c.cache_name);
		return (-1);
	}
	bufsize = c.cache_bufsize;
	chunksize = c.cache_chunksize;
	slabsize = c.cache_slabsize;
	ubase = malloc(slabsize);
	bcp = malloc((c.cache_buftotal + 100) * sizeof (kmem_bufctl_audit_t));
	if (ubase == NULL || bcp == NULL) {
		perror("malloc");
		if (ubase != NULL)
			free(ubase);
		if (bcp != NULL)
			free(bcp);
		return (-1);
	}

	sp = c.cache_nullslab.slab_next;
	while (sp != &cp->cache_nullslab) {
		if (kvm_read(kd, (u_long)sp, (void *)&s, sizeof (s)) == -1 ||
		    s.slab_cache != cp) {
			free(ubase);
			free(bcp);
			error("error reading slab list\n");
		}
		kbase = s.slab_base;
		chunks = s.slab_chunks;
		if (kvm_read(kd, (u_long)kbase, ubase,
		    chunks * chunksize) == -1) {
			free(ubase);
			free(bcp);
			error("error reading slab %x\n", sp);
		}
		for (i = 0; i < chunks; i++) {
			void *buf = (void *)((int)ubase + i * chunksize);
			btp = (kmem_buftag_t *)((int)buf + c.cache_offset);
			if ((btp->bt_bxstat ^ KMEM_BUFTAG_ALLOC) !=
			    (int)btp->bt_bufctl)
				continue;
			if (kvm_read(kd, (u_long)btp->bt_bufctl, (void *)bcp,
			    sizeof (kmem_bufctl_audit_t)) == -1) {
				free(ubase);
				free(bcp);
				error("error reading bufctl %x\n",
					btp->bt_bufctl);
			}
			(*func)(bcp->bc_addr, NULL, bufsize, bcp);
			bcp++;
		}
		sp = s.slab_next;
	}
	free(bcp);
	free(ubase);
	return (0);
}

/*
 * Get statistics for the specified kmem cache
 */
int
kmem_cache_getstats(kmem_cache_t *kcp, kmem_cache_stat_t *kcsp)
{
	kmem_cache_t *cp;
	kmem_slab_t s, *sp;
	Elf32_Sym *ncpus_sym;
	u_int ncpus_addr;
	int ncpus, csize, cpu_seqid;
	int errcode = -1;

	if ((ncpus_sym = symsrch("ncpus")) == 0)
		(void) error("ncpus not in symbol table\n");
	ncpus_addr = ncpus_sym->st_value;
	if (kvm_read(kd, ncpus_addr, (char *)&ncpus, sizeof (int)) == -1)
		return (-1);

	csize = KMEM_CACHE_SIZE(ncpus);
	cp = malloc(csize);

	if (kvm_read(kd, (u_long)kcp, (void *)cp, csize) == -1)
		goto out;

	kcsp->kcs_buf_size	= cp->cache_bufsize;
	kcsp->kcs_chunk_size	= cp->cache_chunksize;
	kcsp->kcs_slab_size	= cp->cache_slabsize;
	kcsp->kcs_align		= cp->cache_align;
	kcsp->kcs_alloc		= cp->cache_alloc + cp->cache_depot_alloc;
	kcsp->kcs_alloc_fail	= cp->cache_alloc_fail;
	kcsp->kcs_buf_avail = cp->cache_fmag_total * cp->cache_magazine_size;
	kcsp->kcs_buf_total	= cp->cache_buftotal;
	kcsp->kcs_buf_max	= cp->cache_bufmax;
	kcsp->kcs_slab_create	= cp->cache_slab_create;
	kcsp->kcs_slab_destroy	= cp->cache_slab_destroy;

	for (cpu_seqid = 0; cpu_seqid < ncpus; cpu_seqid++) {
		kmem_cpu_cache_t *ccp = &cp->cache_cpu[cpu_seqid];
		if (ccp->cc_rounds > 0)
			kcsp->kcs_buf_avail += ccp->cc_rounds;
		if (ccp->cc_full_mag)
			kcsp->kcs_buf_avail += ccp->cc_magsize;
		kcsp->kcs_alloc += ccp->cc_alloc;
	}

	for (sp = cp->cache_freelist; sp != &kcp->cache_nullslab;
	    sp = s.slab_next) {
		if (kvm_read(kd, (u_long)sp, (void *)&s, sizeof (s)) == -1 ||
		    s.slab_cache != kcp)
			goto out;
		kcsp->kcs_buf_avail += s.slab_chunks - s.slab_refcnt;
	}

	errcode = 0;
out:
	free(cp);
	return (errcode);
}

struct allocowner {
	u_int	pc[KMEM_STACK_DEPTH];
	u_int	depth;
	u_int	num;
	u_int	total_size;
	u_int	min_data_size;
	u_int	max_data_size;
};

#define	OWNER_ALLOCCHUNK 2048
struct allocowner *ownerlist = NULL;
int ownerlistsize = 0;	/* Number of elements */

static void
add_ownerspace(void)
{
	ownerlistsize += OWNER_ALLOCCHUNK;
	ownerlist = realloc(ownerlist,
			    ownerlistsize * sizeof (struct allocowner));
	if (ownerlist == NULL) {
		free(ownerlist);
		ownerlist = NULL;
		ownerlistsize = 0;
		error("Out of memory\n");
	}
	/* Clear out added memory */
	memset((char *)&ownerlist[ownerlistsize - OWNER_ALLOCCHUNK], 0,
		OWNER_ALLOCCHUNK * sizeof (struct allocowner));
}

void
init_owner(void)
{
	if (ownerlist == NULL)
		add_ownerspace();
	/* Clear the whole table */
	memset((char *)ownerlist, 0,
	    ownerlistsize * sizeof (struct allocowner));
}

void
add_owner(kmem_bufctl_audit_t *bcp, u_int size, u_int data_size)
{
	int	i, j, found;
	struct allocowner *ao;

	for (i = 0, ao = ownerlist; i < ownerlistsize; i++, ao++) {
		if (ao->num == 0) {
			/* Free entry */
			ao->depth = bcp->bc_depth;
			for (j = 0; j < bcp->bc_depth; j++)
				ao->pc[j] = bcp->bc_stack[j];
			ao->min_data_size = data_size;
			ao->max_data_size = data_size;
			goto found;
		}
		found = 1;
		if (ao->depth != bcp->bc_depth)
			found = 0;
		else {
			for (j = 0; j < bcp->bc_depth; j++) {
				if (ao->pc[j] != bcp->bc_stack[j]) {
					found = 0;
					break;
				}
			}
		}
		if (found) {
		found:
			ao->num++;
			ao->total_size += size;
			if (data_size > ao->max_data_size)
				ao->max_data_size = data_size;
			if (data_size < ao->min_data_size)
				ao->min_data_size = data_size;
			return;
		}
	}
	add_ownerspace();
	/* Try again */
	add_owner(bcp, size, data_size);
}

struct allocowner *
findmaxmemao(void)
{
	struct allocowner *ao;
	struct allocowner *maxao;
	int maxsize, i;

	maxao = NULL;
	maxsize = 0;
	for (i = 0, ao = ownerlist; i < ownerlistsize; i++, ao++) {
		if (ao->num == 0) {
			/* Free entry */
			continue;
		}
		if (ao->total_size > maxsize) {
			maxsize = ao->total_size;
			maxao = ao;
		}
	}
	return (maxao);
}

void
print_owner(char *itemstr, int mem_threshold, int cnt_threshold)
{
	int	j;
	struct allocowner *ao;

	while ((ao = findmaxmemao()) != NULL) {
		if (ao->total_size >= mem_threshold &&
		    ao->num >= cnt_threshold) {
			fprintf(fp, "%d bytes for %d %s with data size "
				"between %d and %d:\n",
				ao->total_size, ao->num, itemstr,
				ao->min_data_size, ao->max_data_size);
			for (j = 0; j < ao->depth; j++) {
				fprintf(fp, "\t ");
				prsymbol(NULL, ao->pc[j]);
			}
		}
		/* Ignore this entry next time around */
		ao->num = 0;
	}
}
