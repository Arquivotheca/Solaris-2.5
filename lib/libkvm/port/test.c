#pragma ident "@(#)test.c	1.11	92/09/24 SMI"
#include <stdio.h>
#include "kvm.h"
#include <nlist.h>
#include <sys/fcntl.h>
#include <sys/param.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/elf.h>

#define TEST_SEGKP 1 		/* turn on if want to test segkp reading */

extern int optind;
extern char *optarg;

kvm_t *cookie;

struct proc *tst_getproc();
struct proc *tst_nextproc();
struct user *tst_getu();

char *name;
char *core;
char *swap;
int wflag;

struct nlist nl[] = {
	{"free"},
	{"fragtbl"},
	{"freemem"},
#ifdef TEST_SEGKP
	{"allthreads"},
#endif
	{"nbuckets"},
	{"cputype"},
	{0}
};

main(argc, argv, envp)
int argc;
char *argv[];
char *envp[];
{
	int c, errflg = 0;
	long xx;
	struct nlist *nlp;
	struct proc *proc;
	struct user *u;
	register int envc, ccnt;

	for (envc = 0; *envp++ != NULL; envc++);
	envp -= 2;
	ccnt = (*envp - *argv) + strlen(*envp) + 1;
	printf("pid %d:: %d args; %d envs; %d chars (%x - %x)\n",
	    getpid(), argc, envc, ccnt,
	    &argv[0], *envp + strlen(*envp));

	while ((c = getopt(argc, argv, "w")) != EOF)
		switch (c) {
		case 'w':
			wflag++;
			break;
		case '?':
			errflg++;
		}
	if (errflg) {
		fprintf(stderr, "usage: %s [-w] [name] [core] [swap]\n",
		    argv[0]);
		exit(1);
	}
	if (optind < argc) {
		name = argv[optind++];
		if (*name == '\0')
			name = NULL;
	} else
		name = NULL;
	if (optind < argc) {
		core = argv[optind++];
		if (*core == '\0')
			core = NULL;
	} else
		core = NULL;
	if (optind < argc) {
		swap = argv[optind++];
		if (*swap == '\0')
			swap = NULL;
	} else
		swap = NULL;

	tst_open(name, core, swap, (wflag ? O_RDWR : O_RDONLY));
	if (cookie == NULL)
		exit(1);

	tst_nlist(nl);

	for (nlp=nl; nlp[0].n_type != 0; nlp++)
		tst_read(nlp[0].n_value, &xx, sizeof(xx));

	while ((proc = (struct proc *)tst_nextproc()) != NULL) {
		struct pid pid;
		if (kvm_read(cookie, (u_long)proc->p_pidp, (char *)&pid,
		   sizeof (struct pid)) != sizeof (struct pid)) {
			local_printf("couldn't get pid\n");
			break;
		}
		tst_getproc(pid.pid_id);
	}

	tst_setproc();

	while ((proc = (struct proc *)tst_nextproc()) != NULL) {
		if ((u = (struct user *)tst_getu(proc)) != NULL)
			tst_getcmd(proc, u);
	}

#ifdef TEST_SEGKP
	tst_segkp();
#endif /* TEST_SEGKP */
	tst_close();
	exit(0);
}

local_printf(a1,a2,a3,a4,a5,a6,a7,a8,a9)
	char *a1;
{
	fflush(stdout);
	fflush(stderr);
	fprintf(stderr, a1,a2,a3,a4,a5,a6,a7,a8,a9);
	fflush(stderr);
}


tst_open(namelist, corefile, swapfile, flag)
char *namelist;
char *corefile;
char *swapfile;
int flag;
{
	local_printf("kvm_open(%s, %s, %s, %s)\n",
	    (namelist == NULL) ? "LIVE_KERNEL" : namelist,
	    (corefile == NULL) ? "LIVE_KERNEL" : corefile,
	    (swapfile == NULL) ?
		((corefile == NULL) ? "LIVE_KERNEL" : "(none)") : swapfile,
	    (flag == O_RDONLY) ? "O_RDONLY" : ((flag == O_RDWR) ?
							"O_RDWR" : "???"));
	if ((cookie =
	    kvm_open(namelist, corefile, swapfile, flag, "libkvm test")) == NULL)
		local_printf("kvm_open returned %d\n", cookie);
}

tst_close()
{
	register int i;

	local_printf("kvm_close()\n");
	if ((i = kvm_close(cookie)) != 0)
		local_printf("kvm_close returned %d\n", i);
}

tst_nlist(nl)
struct nlist nl[];
{
	register int i;
	char *t, *s;

	local_printf("kvm_nlist([nl])\n");
	if ((i = kvm_nlist(cookie, nl)) != 0)
		local_printf("kvm_nlist returned %d\n", i);
	for (i=0; (nl[i].n_name!=0) && (nl[i].n_name[0]!='\0'); i++) {
		/*
		 * Debug:
		 * n_value gets filled in with st_value,
		 * n_type gets filled in w/ELF32_ST_TYPE(sym->st_info)
		 * n_scnum gets filled in w/st_shndx
		 */

		switch (nl[i].n_type) {
		case STT_NOTYPE:
			t = "NOTYPE";
			break;
		case STT_OBJECT:
			t = "OBJECT";
			break;
		case STT_FUNC:
			t = "FUNC";
			break;
		case STT_SECTION:
			t = "SECTION";
			break;
		case STT_FILE:
			t = "FILE";
			break;
		case STT_NUM:
			t = "NUM";
			break;
		default:
			t = "???";
		}
		
		switch ((unsigned)nl[i].n_scnum) {
			static char strbuf[40];

		case SHN_UNDEF:
			s = "UNDEF";
			break;
		case SHN_LORESERVE:
			s = "LORESERVE";
			break;
		case SHN_ABS:
			s = "ABS";
			break;
		case SHN_COMMON:
			s = "COMMON";
			break;
		case SHN_HIRESERVE:
			s = "HIRESERVE";
			break;
		default:
			sprintf(strbuf, "unknown (%d)", nl[i].n_scnum);
			s = strbuf;
			break;
		}

		local_printf("%s: %x (%s, %s)\n",
			     nl[i].n_name, nl[i].n_value, s, t);
	}
}

tst_read(addr, buf, nbytes)
unsigned long addr;
char *buf;
unsigned nbytes;
{
	register int e;
	register int i;
	register char *b;

	local_printf("kvm_read(%x, [buf], %d)\n", addr, nbytes);
	if ((e = kvm_read(cookie, addr, buf, nbytes)) != nbytes)
		local_printf("kvm_read returned %d instead of %d\n", e, nbytes);
	for (b=buf,i=0; i<nbytes; b++,i++) {
		local_printf("%x: %02x (%04o)\n", addr+i, *b&0xff, *b&0xff);
	}
	return (e);
}

tst_write(addr, buf, nbytes)
unsigned long addr;
char *buf;
unsigned nbytes;
{
	register int e;
	register int i;
	register char *b;

	local_printf("kvm_write(%x, [buf], %d)\n", addr, nbytes);
	if ((e = kvm_write(cookie, addr, buf, nbytes)) != nbytes)
		local_printf("kvm_write returned %d instead of %d\n", e, nbytes);
	if ((b = (char*)malloc(nbytes)) == 0)
		local_printf("malloc for readback failed\n");
	else {
		if ((i = kvm_read(cookie, addr, b, nbytes)) != nbytes)
			local_printf("readback returned %d\n", i);
		else if (memcmp(b, buf, nbytes))
			local_printf("write check failed!\n");
		(void) free(b);
	}
	return (e);
}

struct proc *
tst_getproc(pid)
int pid;
{
	struct proc *proc;
	struct pid pidbuf;

	local_printf("kvm_getproc(%d)\n", pid);
	if ((proc = kvm_getproc(cookie, pid)) == NULL) {
		local_printf("kvm_getproc returned NULL\n");
		return (proc);
	}

	if (kvm_read(cookie, (u_long)proc->p_pidp, (char *)&pidbuf,
	    sizeof (struct pid)) != sizeof (struct pid)) {
		local_printf("couldn't get pid\n");
	}

	local_printf("p_pid: %d\n", pidbuf.pid_id);
	return (proc);
}

struct proc *
tst_nextproc()
{
	struct proc *proc;
	struct pid pidbuf;

	local_printf("kvm_nextproc()\n");
	if ((proc = kvm_nextproc(cookie)) == NULL) {
		local_printf("kvm_nextproc returned NULL\n");
		return (proc);
	}

	/*
	 * p_pid is now a macro which turns into a ptr dereference;
	 * must do a kvm_read to get contents.
	 */
	if (kvm_read(cookie, (u_long)proc->p_pidp, (char *)&pidbuf,
	    sizeof (struct pid)) != sizeof (struct pid)) {
		local_printf("couldn't get pid\n");
	}
	local_printf("p_pid: %d\n", pidbuf.pid_id);

	return (proc);
}

tst_setproc()
{
	register int i;

	local_printf("kvm_setproc()\n");
	if ((i = kvm_setproc(cookie)) != 0)
		local_printf("kvm_setproc returned %d\n", i);
	return (i);
}

struct user *
tst_getu(proc)
struct proc *proc;
{
	register int e;
	struct proc tp;
	struct user *u;
	struct pid pidbuf;


	if (kvm_read(cookie, (u_long)proc->p_pidp, (char *)&pidbuf,
	    sizeof (struct pid)) != sizeof (struct pid)) {
		local_printf("couldn't get pid\n");
	}

	local_printf("kvm_getu(pid:%d)\n", pidbuf.pid_id);
	if ((u = kvm_getu(cookie, proc)) == NULL) {
		local_printf("kvm_getu returned NULL\n");
		return (u);
	}
#ifdef obsolete
	/*
	 * There is no way to get from a 'struct user' to a 'struct proc'
	 * -- and no need, since 'struct proc' contains a 'struct user' i.e.
	 *
	 *	#define	u	(curproc->p_user)
	 */
	if ((e = kvm_read(cookie, u->u_procp, &tp, sizeof (tp)))
	    != sizeof (tp))
		local_printf("kvm_read returned %d instead of %d\n", e,
			     sizeof (tp));
	/* pid is now ptr */
	if (kvm_read(cookie, (u_long)tp.p_pidp, (char *)&pidbuf,
	    sizeof (struct pid)) != sizeof (struct pid)) {
		local_printf("couldn't get pid\n");
	}
	local_printf("u_procp: %x -> p_pid: %d\n", u->u_procp, pidbuf.pid_id);
#endif
	return (u);
}

tst_getcmd(proc, u)
struct proc *proc;
struct user *u;
{
	char **arg;
	char **env;
	register int i;
	char **p;
	struct pid pidbuf;


	if (kvm_read(cookie, (u_long)proc->p_pidp, (char *)&pidbuf,
	    sizeof (struct pid)) != sizeof (struct pid)) {
		local_printf("couldn't get pid\n");
	}

    local_printf("kvm_getcmd(pid:%d, [u], arg, env)\n", pidbuf.pid_id);
    if ((i = kvm_getcmd(cookie, proc, u, &arg, &env)) != 0) {
	    local_printf("kvm_getcmd returned %d\n", i);
	    return (i);
    }
    local_printf("Args:  ");
    for (p = arg; *p != NULL; p++)
	    local_printf("%s ", *p);
    local_printf("\nEnv:  ");
    for (p = env; *p != NULL; p++)
	    local_printf("%s ", *p);
    local_printf("\n");
    (void) free(arg);
    (void) free(env);
    return (i);
}

#ifdef TEST_SEGKP
#include <sys/thread.h>

tst_segkp()
{
	kthread_t t;
	caddr_t tp, alltp;
	u_int stk[16];
	register int i;

	if (kvm_read(cookie, nl[3].n_value, (char *)&alltp, sizeof alltp)
	    != sizeof alltp) {
		local_printf("couldn't read allthread, addr 0x%x\n",
			     nl[3].n_value);
		return;
	}
	local_printf("allthreads 0x%x\n", nl[3].n_value);
	local_printf("next offset 0x%x\n", (u_int)&(t.t_next) - (u_int)&t);

	for (tp = alltp; tp; tp = (caddr_t)(t.t_next)) {
		if (kvm_read(cookie, (u_long)tp, (char *)&t,
		    sizeof (t)) != sizeof (t)) {
			local_printf("couldn't read thread, addr 0x%x\n", tp);
			return;
		}

		local_printf("thread 0x%x stk 0x%x stksz 0x%x sp 0x%x tid %d",
			     tp, t.t_stk, t.t_stksize, t.t_pcb.val[1],
			     t.t_tid);
		local_printf(" next 0x%x prev 0x%x\n", t.t_next, t.t_prev);
		
		if (kvm_read(cookie, t.t_pcb.val[1], (char *)stk, sizeof (stk))
		    != sizeof (stk)) {
			local_printf("couldn't read stk, addr 0x%x\n",
				     tp);
			/* return; */ continue;
		}
		for (i = 0; i < 16; i++) {
			local_printf("%x\t", stk[i]);
			if (i == 7)
				local_printf("\n");
		}
		local_printf("\n");
	}
}
#endif
