/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 */

/*
 * adb - subprocess running routines
 */

#ident	"@(#)runpcs.c	1.49	95/06/13 SMI"

#define BPT_INIT
#include "adb.h"
#include <stdio.h>
#include <fcntl.h>
#include <sys/procfs.h>
#include <sys/ptrace.h>
#include <sys/errno.h>
#include "fpascii.h"
#ifdef __ELF
#include <link.h>
#include <sys/auxv.h>
#else
#include "link_sunos.h"
#endif
addr_t rtld_state_addr;
addr_t exec_entry;

/*
 * On a sparc, we must simulate single-stepping.  The only ptrace
 * calls that might try to single-step are in this file.  I've replaced
 * those calls with calls to adb_ptrace.  The following #define allows
 * ptrace to be called directly if not on a sparc.
 *
 * NOTE: The above is true now only for kadb.  adb single-steps using
 * the PTRACE_SINGLESTEP option of the bogus ptrace() interface to /proc.
 */
#if !defined(sparc) || !defined(KADB)
#   define adb_ptrace ptrace
#endif

char *map();
static execbkpt();
struct	bkpt *bkpthead;
extern use_shlib;
#ifdef __ELF
extern struct r_debug *ld_debug_addr;
extern struct r_debug ld_debug_struct;
extern Elf32_Dyn *dynam_addr;
extern Elf32_Dyn dynam;
extern char *rtld_path;
#else
extern struct  ld_debug ld_debug_struct;
extern addr_t  ld_debug_addr;
extern struct  link_dynamic dynam;
addr_t *dynam_addr;
#endif
extern address_invalid;

#define SYM_LOADED ((addr_t)&(ld_debug_struct.ldd_sym_loaded) - (addr_t)&ld_debug_struct)
#define IN_DEBUGGER ((addr_t)&(ld_debug_struct.ldd_in_debugger) - (addr_t)&ld_debug_struct)

int	loopcnt;

#define BPOUT	0
#define BPIN	1
#define BPSTEP	2

int	bpstate = BPOUT;

addr_t userpc;
#ifdef sparc
addr_t usernpc;
#endif

/*
 * A breakpoint instruction is in the extern "bpt", defined in
 * the machine-dependent sun.h, sparc.h, or whatever.
 */

getsig(sig)
	int sig;
{

	return (expr(0) ? expv : sig);
}

runpcs(runmode, execsig)
	int runmode;
	int execsig;
{
	int rc = 0;
	register struct bkpt *bkpt;
	unsigned loc;
	int changed_pc = 0;

	if (hadaddress) {
#ifdef INSTR_ALIGN_ERROR
		if( dot & INSTR_ALIGN_MASK ) {
			error( INSTR_ALIGN_ERROR );
		}
#endif INSTR_ALIGN_ERROR
		userpc = (addr_t)dot;
#ifdef sparc
		usernpc = userpc + 4;
		changed_pc = 1;
#endif
	}
	while (--loopcnt >= 0) {
		db_printf(2, "COUNTDOWN = %D", loopcnt);
		if (runmode == PTRACE_SINGLESTEP) {
			ss_setup(userpc);
		} else {
			if (bkpt = bkptlookup(userpc)) {
				execbkpt(bkpt, execsig);
				execsig = 0;
			}
			setbp();
		}

		loc = (unsigned)userpc;
		(void) adb_ptrace(runmode, pid, (int)userpc,
					execsig, changed_pc);
#if	defined(sparc) && defined(KADB)
		if (runmode != PTRACE_SINGLESTEP) {
			bpwait(runmode);
			chkerr();
		}
#else
		bpwait(runmode);
		chkerr();
#endif
#ifndef	KADB
#ifndef	__ELF
		if (pid && use_shlib){
		    ld_debug_struct = *(struct ld_debug *)read_ld_debug();
		    if (ld_debug_struct.ldd_sym_loaded){
		    put(ld_debug_addr+SYM_LOADED, DSP, 0);
			if (userpc == (addr_t)ld_debug_struct.ldd_bp_addr){
			    read_in_shlib();
			    crt0_execbkpt(userpc, execsig);
			    setbp();
			    (void) adb_ptrace(runmode, pid, (int)userpc,
						execsig, changed_pc);
			    bpwait(runmode);
			} else {
			    error("rtld doesn't place the break point right\n");
			}
		    }
		}
#else
		if (pid && use_shlib && userpc == exec_entry) {
		    if ((bkpt = bkptlookup(userpc))) {
			scan_linkmap();
			execbkpt(bkpt, execsig);
			bkpt->flag = 0;
			setbp();
			(void) adb_ptrace(runmode, pid, (int)userpc,
					    execsig, changed_pc);
			bpwait(runmode);
		    }
		}
#endif	/* !__ELF */
		chkerr();
#endif KADB
		execsig = 0; delbp();
#ifndef KADB
#ifndef sparc
		/* Special case for FPA on sun-680x0 machines */
		if (runmode == PTRACE_SINGLESTEP
		    && signo == 0 && fpa_disasm && twomoveop(loc)) {
			(void) ptrace(runmode, pid, (int)userpc, execsig);
			bpwait(runmode); chkerr(); execsig = 0; delbp();
		}
#endif sparc
#endif !KADB
		/* If we've stopped at a ":u" breakpoint, check that we're
		 * in the frame where we want to stop.
		 */
		/*if ((bkpt = bkptlookup(userpc)) &&
			(bkpt->flag & BKPT_TEMP) &&
			(bkpt->count != 1) &&
			(bkpt->count != readreg(Reg_SP))) {
			execbkpt(bkpt, execsig);
			execsig = 0;
			loopcnt++;
			continue;
		}*/
		
		/* Clobber any apocryphal temp breakpoints.  Keep the
		 * one at pc, though, so print_dis() can find the real
		 * instruction without reading the child. (adb only!)
		 */
		for (bkpt = bkpthead; bkpt; bkpt = bkpt->nxtbkpt) {
#if	!defined(KADB)
			if (bkpt->flag & BKPT_TEMP && bkpt->loc != userpc)
#else
			if (bkpt->flag & BKPT_TEMP)
#endif
				bkpt->flag = 0;
		}
		if (signo == 0 && (bkpt = bkptlookup(userpc)) &&
			(!(bkpt->flag & BKPT_TEMP))) {
			dot = (int)bkpt->loc;
			if ((bkpt->comm[0] == '\n' || /* HACK HERE */
				command(bkpt->comm, ':')) && --bkpt->count) {
				execbkpt(bkpt, execsig);
				execsig = 0;
				loopcnt++;
			} else {
				bkpt->count = bkpt->initcnt;
				rc = 1;
			}
		} else {
			execsig = signo;
			rc = 0;
		}
		if (interrupted)
			break;
	}
	if ((bkpt = bkptlookup(userpc)) && (bkpt->flag & BKPT_TEMP))
		rc = 0;		/* So this doesn't look like a breakpoint. */
	return (rc);
}

endpcs()
{
	register struct bkpt *bp;

	if (pid) {
		exitproc();
#ifdef __ELF
		userpc = (addr_t)filhdr.e_entry;
#else	/* __ELF */
		userpc = (addr_t)filhdr.a_entry;
#endif	/* __ELF */

#ifdef sparc
		usernpc = userpc + 4;
#endif
		for (bp = bkpthead; bp; bp = bp->nxtbkpt)
			if (bp->flag)
				bp->flag = BKPTSET;
	}
	address_invalid = 1;
	bpstate = BPOUT;
}

#ifndef __ELF
/*
 * remove the secret bp in crt0 which was set by rtld.
 */
static
crt0_execbkpt(addr, execsig)
	addr_t addr;
{
	delbp();
	put(addr, ISP, ld_debug_struct.ldd_bp_inst);
	(void) adb_ptrace(PTRACE_SINGLESTEP, pid, addr, execsig, 0);
	bpwait(PTRACE_SINGLESTEP);
	chkerr();
}
#endif

/*
 * execute the instruction at a breakpoint, by deleting all breakpoints,
 * and then single stepping (called when we're about to continue from one).
 */
static
execbkpt(bp, execsig)
	struct bkpt *bp;
	int execsig;
{

	ss_setup(userpc);
	(void) adb_ptrace(PTRACE_SINGLESTEP, pid, (int)bp->loc, execsig, 0);
#ifndef KADB
	bpwait(PTRACE_SINGLESTEP);
#endif
	bp->flag = BKPTSET;
#ifdef KADB
#ifndef sparc
	bpwait(PTRACE_SINGLESTEP);
#endif
#ifdef sparc
	delbp();		/* to fix a kadb bug with multiple  */
				/* breakpoints  */
#endif
#endif
	chkerr();
}

#ifndef KADB
extern char	**environ;

static
doexec()
{
	char *argl[BUFSIZ/8];
	char args[BUFSIZ];
	char *p = args, **ap = argl, *filnam;

	*ap++ = symfil;
	do {
		if (rdc()=='\n')
			break;
		*ap = p;
		while (!isspace(lastc)) {
			*p++=lastc;
			(void) readchar();
		}
		*p++ = '\0';
		filnam = *ap + 1;
		if (**ap=='<') {
			(void) close(0);
			if (open(filnam, 0) < 0) {
				printf("%s: cannot open\n", filnam);
				exit(0);
			}
		} else if (**ap=='>') {
			(void) close(1);
			if (creat(filnam, 0666) < 0) {
				printf("%s: cannot create\n", filnam);
				exit(0);
			}
		} else {
	 		ap++;
			if (ap >= &argl[sizeof (argl)/sizeof (argl[0]) - 2]) {
				printf("too many arguments\n");
				exit(0);
			}
		}
	} while (lastc != '\n');
	*ap++ = 0;
	flushbuf();
	execve(symfil, argl, environ);
	perror(symfil);
}
#endif !KADB

struct bkpt *
bkptlookup(addr)
	unsigned addr;
{
	register struct bkpt *bp;

	for (bp = bkpthead; bp; bp = bp->nxtbkpt)
		if (bp->flag && (unsigned)bp->loc == addr)
			break;
	return (bp);
}

delbp()
{
	register struct bkpt *bp;

	if (bpstate == BPOUT)
		return;
	for (bp = bkpthead; bp; bp = bp->nxtbkpt)
		if (bp->flag && (bp->flag & BKPT_ERR) == 0)
			(void) writeproc(bp->loc, (char *)&bp->ins, SZBPT);
	bpstate = BPOUT;
}

setbrkpt(bp)
	register struct bkpt *bp;
{
	if (readproc(bp->loc, (char *)&bp->ins, SZBPT) != SZBPT
	    || writeproc(bp->loc, (char *)&bpt, SZBPT) != SZBPT) {
		bp->flag |= BKPT_ERR;   /* turn on err flag */
		if (use_shlib && address_invalid)
			return;
		prints("cannot set breakpoint: ");
		psymoff(bp->loc, ISYM, "\n");
	} else {
		bp->flag &= ~BKPT_ERR;  /* turn off err flag */
		db_printf(3, "setbrkpt: set breakpoint");
	}
}

static
setbp()
{
	register struct bkpt *bp;

	if (bpstate == BPIN) {
		return;
	}
	for (bp = bkpthead; bp; bp = bp->nxtbkpt) {
		if (bp->flag) {
			setbrkpt(bp);
		}
	}
	bpstate = BPIN;
}

/*
 * Called to set breakpoints up for executing a single step at addr
 */
ss_setup(addr)
	addr_t addr;
{
#ifdef KADB
	/*
	 * For kadb we need to set all the current breakpoints except
	 * for those at addr.  This is needed so that we can hit break
	 * points in interrupt and trap routines that we may run into
	 * during a signle step.
	 */
	register struct bkpt *bp;

	for (bp = bkpthead; bp; bp = bp->nxtbkpt)
		if (bp->flag && bp->loc != addr)
			setbrkpt(bp);
	bpstate = BPSTEP;       /* force not BPOUT or BPIN */
#else KADB
	/*
	 * In the UNIX case, we simply delete all breakpoints
	 * before doing the single step.  Don't have to worry
	 * about asynchronous hardware events here.
	 */
	delbp();
#endif KADB
}

#ifndef KADB
/*
 * If the child is in another process group, ptrace will not always inform
 * adb (via "wait") when a signal typed at the terminal has occurred.
 * (Actually, if the controlling tty for the child is different from
 * the adb's controlling tty the notification fails to happen.)  Thus,
 * adb should catch the signal and if the child is in the same process
 * group, simply return (ptrace will inform adb of the signal via "wait").
 * If the child is in a different process group, adb should issue a "kill"
 * of that signal to the child.  (This will cause ptrace to "wake up" and
 * inform adb that the child received a signal.)  [Note that if the
 * child's process group is different than that of adb, the controlling
 * tty's process group is also different.]
 */

/* getpgid() in SVr4 is what getpgrp() was in SunOS4.0 and before */
#define	getpgrp getpgid

void sigint_handler()
{

	if (getpgrp(pid) != adb_pgrpid) {
		kill(pid, SIGINT);
	}
}

void sigquit_handler()
{

	if (getpgrp(pid) != adb_pgrpid) {
		kill(pid, SIGQUIT);
	}
}

bpwait(mode)
int mode;
{
	register unsigned w;
	int stat;
	extern char *corfil;
	int pcfudge = 0;
	void (*oldisig)();
	void (*oldqsig)();

	tbia();
rewait:
	oldisig = (void (*)(int)) signal(SIGINT, sigint_handler);
	oldqsig = (void (*)(int)) signal(SIGQUIT, sigquit_handler);
#if obsolete_code
	while ((w = wait(&stat)) != pid && w != -1 )
		continue;
#else
	while ((w = proc_wait(pid, &stat)) != pid)
		if (w == -1 && errno != EINTR)
			break;
#endif
	signal(SIGINT, oldisig);
	signal(SIGQUIT, oldqsig);
	db_printf(2, "wait: %s", map(mode));

	if (w == -1) {
		pid = 0;
		errflg = "wait error: process disappeared";
		return;
	}
	if ((stat & 0177) != 0177) {
		sigcode = 0;
		if (signo = stat&0177)
			sigprint(signo);
		if (stat&0200) {
			prints(" - core dumped\n");
			(void) close(fcor);
			corfil = "core";
			pid = 0; 
			setcor();
		} else {
			pid = 0;
		}
		errflg = "process terminated";
	} else {
		signo = stat>>8;
		sigcode = (Prstatus.pr_why == PR_SIGNALLED) ?
			Prstatus.pr_info.si_code : TRAP_TRACE;

		/* Don't need to get the regs.  Our bogus ptrace()
		 * left them in a global struct.
		ptrace(PTRACE_GETREGS, pid, &core.c_regs, 0, 0); */
#ifdef sparc
		core_to_regs( );
		db_printf(1, "bpwait:  e.g., PC is %X",
			Prstatus.pr_reg[R_PC] );
		db_printf(1, "bpwait after rw_pt:  e.g., PC is %X",
			Prstatus.pr_reg[R_PC] );
		stat = ptrace(PTRACE_GETFPREGS, pid, &Prfpregs, 0, 0);
#ifdef DEBUG
		db_fprdump( stat );
		db_printf(2, "wait: ");
		sigprint(signo);
#endif
		if (signo != SIGTRAP) {
			if (mode == PTRACE_CONT && sigpass[signo]) {
				chk_ptrace(PTRACE_CONT, pid, (int *)1, signo);
				goto rewait;
			} /* else */
			sigprint(signo);
		} else {
			signo = 0;
#ifdef sun
			/*
			 * if pc was advanced during execution of the breakpoint
			 * (probably by the size of the breakpoint instruction)
			 * push it back to the address of the instruction, so
			 * we will later recognize it as a tripped trap.
			 */
			if (mode == PTRACE_CONT) {
				db_printf(1, "bpwait: BREAKPOINT");
				pcfudge = PCFUDGE; /* for sun this is -SZBPT */
			}
#endif sun
		}
	}
	flushbuf();
	userpc = (addr_t)readreg(Reg_PC);
#ifdef sparc
	usernpc = (addr_t)readreg(Reg_NPC);
#endif
	db_printf(1, "bpwait: user PC is %X, user NPC is %X; ",
	    userpc, usernpc);
#ifndef sparc
	if (pcfudge) {
		userpc += pcfudge;
		pc_cheat(userpc);
	}
#endif !sparc

}  /* end of NON-KADB version of bpwait */



/*
 * Workaround for sparc kernel bug -- this routine checks to
 * see whether PTRACE_CONT with a signo is working.  Naturally,
 * adb ignores the return value of _all_ ptrace calls!
 */
chk_ptrace ( mode, pid, upc, xsig ) int mode; {
  int rtn, *userpc;

	if( mode != PTRACE_CONT  ||  sigpass[ signo ] == 0 )
		return ptrace( mode, pid, upc, xsig );

	rtn = ptrace( mode, pid, upc, xsig );
	if( rtn >= 0 ) return rtn;

	db_printf(1, "chk_ptrace: adb attempted ptrace(%d,%d,%d,%d):",
	    mode, pid, upc, xsig);
	db_printf(1, "chk_ptrace: adb caught signal %d\n", xsig);
#ifdef DEBUG
	perror("attempt to continue subproc failed");
#endif
	/*
	 * Workaround:  use the pc explicitly
	 */
	userpc = (int *)readreg(Reg_PC);
	rtn = ptrace( mode, pid, userpc, xsig );
	if( rtn >= 0 ) return rtn;

#ifdef DEBUG
	printf( "adb attempted ptrace(%d,%d,%d,%d):\n",
		mode, pid, userpc, xsig );
	printf( "adb caught signal %d\n", xsig );
	perror( "attempt to continue subproc failed" );
#endif DEBUG

#endif sparc


	return rtn;
}

#endif !KADB



#ifdef KADB
/*
 * kadb's version - much simpler and fewer assumptions
 * bpwait should be in per-CPU and kadb/non-kadb directories, but
 * until I get time to do that, here it is #ifdef'd.
 */
# ifdef sparc

bpwait(mode)		/* sparc-kadb version */
int mode;
{
	extern struct allregs adb_regs;
	extern struct allregs_v9 adb_regs_v9;
	extern int v9flag;

	tbia();
	doswitch();	/* wait for something to happen */

	if (v9flag)
		ptrace(PTRACE_GETREGS, pid, &adb_regs_v9, 0, 0);
	else
		ptrace(PTRACE_GETREGS, pid, &adb_regs, 0, 0);

	signo = 0;
	regs_to_core( );
	flushbuf();
	userpc = (addr_t)readreg(Reg_PC);
	usernpc = (addr_t)readreg(Reg_NPC);
	db_printf(1, "bpwait: user PC is %X, user NPC is %X",
	    userpc, usernpc);
	tryabort(0);		/* set interrupt if user is trying to abort */
	ttysync();
}  /* end of sparc-kadb version of bpwait */
#endif sparc
#endif KADB

#ifdef VFORK
nullsig()
{
}
#endif

setup()
{
	extern int adb_debug;

#ifndef KADB
	(void) close(fsym); fsym = -1;
	flushbuf();
	newsubtty();
	endpcs();
#ifndef VFORK
	if ((pid = fork()) == 0)
#else
	if ((pid = vfork()) == 0)
#endif
	{
		(void) ptrace(PTRACE_TRACEME, 0, 0, 0);
		signal(SIGINT,  sigint);
		signal(SIGQUIT, sigqit);
		closettyfd();
		doexec();
		_exit(0);
	}
	if (pid == -1)
		error("try again");

	bpwait(PTRACE_TRACEME);
	lp[0] = '\n'; lp[1] = 0;
	fsym = open(symfil, wtflag);
	if (fsym < 0)
		perror(symfil);
	if (errflg) {
		printf("%s: cannot execute\n", symfil);
		endpcs();
		error((char *)0);
	}
#ifndef	__ELF
	if (use_shlib){
		read_ld_debug();
		put(ld_debug_addr+IN_DEBUGGER, DSP, 1);
	}
#else
	if (use_shlib){
	    if( adb_debug) {
		printf ("reading shared lib %s dynam_addr %X base %X\n", 
			    rtld_path, dynam_addr,find_rtldbase());
	    }
	    read_in_rtld(); /*userpc&pagemask*/
	}
#endif	/* !__ELF */
#endif !KADB
	bpstate = BPOUT;
}


char *
map(request)		 /* for debugging purposes only */
	int request;
{
	static char buffer[16];

	switch (request) {
	case PTRACE_TRACEME:	return "PTRACE_TRACEME";
	case PTRACE_PEEKTEXT:	return "PTRACE_PEEKTEXT";
	case PTRACE_PEEKDATA:	return "PTRACE_PEEKDATA";
	case PTRACE_PEEKUSER:	return "PTRACE_PEEKUSER";
	case PTRACE_POKETEXT:	return "PTRACE_POKETEXT";
	case PTRACE_POKEDATA:	return "PTRACE_POKEDATA";
	case PTRACE_POKEUSER:	return "PTRACE_POKEUSER";
	case PTRACE_CONT:	return "PTRACE_CONT";
	case PTRACE_KILL:	return "PTRACE_KILL";
	case PTRACE_SINGLESTEP:	return "PTRACE_SINGLESTEP";
	case PTRACE_ATTACH:	return "PTRACE_ATTACH";
	case PTRACE_DETACH:	return "PTRACE_DETACH";
	case PTRACE_GETREGS:	return "PTRACE_GETREGS";
	case PTRACE_SETREGS:	return "PTRACE_SETREGS";
	case PTRACE_GETFPREGS:	return "PTRACE_GETFPREGS";
	case PTRACE_SETFPREGS:	return "PTRACE_SETFPREGS";
	case PTRACE_READDATA:	return "PTRACE_READDATA";
	case PTRACE_WRITEDATA:	return "PTRACE_WRITEDATA";
	case PTRACE_READTEXT:	return "PTRACE_READTEXT";
	case PTRACE_WRITETEXT:	return "PTRACE_WRITETEXT";
	case PTRACE_GETFPAREGS:	return "PTRACE_GETFPAREGS";
	case PTRACE_SETFPAREGS:	return "PTRACE_SETFPAREGS";
#if sparc
	case PTRACE_GETWINDOW:	return "PTRACE_GETWINDOW";
	case PTRACE_SETWINDOW:	return "PTRACE_SETWINDOW";
#endif
	case PTRACE_SYSCALL:	return "PTRACE_SYSCALL";
	case PTRACE_DUMPCORE:	return "PTRACE_DUMPCORE";
#if i386
	case PTRACE_SETWRBKPT:	return "PTRACE_SETWRBKPT";
	case PTRACE_SETACBKPT:	return "PTRACE_SETACBKPT";
	case PTRACE_CLRDR7:	return "PTRACE_CLRDR7";
#endif
	case PTRACE_TRAPCODE:	return "PTRACE_TRAPCODE";
#if i386
	case PTRACE_SETBPP:	return "PTRACE_SETBPP";
#endif
	}
	(void) sprintf(buffer, "PTRACE_%d", request);
	return (buffer);
}

#ifdef DEBUG

#ifndef KADB

db_fprdump ( stat ) {
  int *fptr, rn, rnx;
  extern int adb_debug;

	if( adb_debug == 0 ) return;

	printf( "Dump of FP register area:\n" );
	printf( "ptrace returned %d; ", stat );

	if( stat < 0 ) {
	    perror( "adb -- ptrace( GETFPREGS )" );
	} else {
	    printf( "\n" );
	}

	for( rn = 0; rn < 32; rn += 4 ) {
	    printf( "Reg F%d:  ", rn );

	    fptr = (int *) &Prfpregs.pr_fr.pr_regs[rn];

	    printf( "%8X %8X %8X %8X\n", fptr[0], fptr[1], fptr[2], fptr[3] );
	}
	printf( "End of dump of FP register area\n" );
}

#endif !KADB
#endif DEBUG
