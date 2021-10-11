
/*
 * adb - process control
 */

#ident "@(#)pcs.c	1.25	95/06/13 SMI"

#include "adb.h"
#include <sys/ptrace.h>
#if defined(KADB) && defined(i386)
#include <sys/frame.h>
#endif

/* breakpoints */
struct	bkpt *bkpthead;

int	loopcnt;
int	ndebug;
int	savdot;

#if defined(KADB) && defined(i386)
char	datalen;
#endif 

/* Generalized from the stuff previously used in the ":b" case of
 * subpcs() below.  With the invention of temp breakpoints, this
 * was put to more general use.
 */
struct bkpt *
get_bkpt(where)
	addr_t where;
{
	struct bkpt *bkptr;

	db_printf(5, "get_bkpt: where=%X", where);
	/* If there is one there all ready, clobber it. */
	bkptr = bkptlookup(where);
	if (bkptr)
		bkptr->flag = 0;

	/* Look for the first free entry on the list. */
	for (bkptr = bkpthead; bkptr; bkptr = bkptr->nxtbkpt)
		if (bkptr->flag == 0)
			break;

	/* If there wasn't one, get one and link it in the list. */
	if (bkptr == 0) {
		bkptr = (struct bkpt *)malloc(sizeof *bkptr);
		if (bkptr == 0)
			error("bkpt: no memory");
		bkptr->nxtbkpt = bkpthead;
		bkpthead = bkptr;
	}
#ifdef INSTR_ALIGN_MASK
	if (where & INSTR_ALIGN_MASK ) {
		error( BPT_ALIGN_ERROR );
	}
#endif INSTR_ALIGN_MASK
	bkptr->loc = where;		/* set the location */
	(void) readproc(bkptr->loc, (char *) &(bkptr->ins), SZBPT);
	db_printf(5, "get_bkpt: returns %X", bkptr);
	return bkptr;
}

subpcs(modif)
	int modif;
{
	register int check;
	int execsig;
	int runmode;
	struct bkpt *bkptr;
	char *comptr;
	int i, line, hitbp = 0;
	char *m;
	struct stackpos pos;

	db_printf(4, "subpcs: modif='%c'", modif);
	execsig = 0;
	loopcnt = count;
	switch (modif) {

#if 0
	case 'D':
		dot = filextopc(dot);
		if (dot == 0)
			error("don't know pc for that source line");
		/* fall into ... */
#endif
	case 'd':
		bkptr = bkptlookup(dot);
		if (bkptr == 0)
			error("no breakpoint set");
		else if (kernel)
			(void) printf("Not possible with -k option.\n");
		else
			db_printf(2, "subpcs: bkptr=%X", bkptr);
		bkptr->flag = 0;
#if defined(KADB) && defined(i386)
		if (bkptr->type == BPACCESS || bkptr->type == BPWRITE || bkptr->type == BPDBINS)
			ndebug--;
#endif
		return;

	case 'z':			/* zap all breakpoints */
		if (kernel) {
			(void) printf("Not possible with -k option.\n");
			return;
		}
		for (bkptr = bkpthead; bkptr; bkptr = bkptr->nxtbkpt) {
			bkptr->flag = 0;
		}
		ndebug = 0;
		return;

#if 0
	case 'B':
		dot = filextopc(dot);
		if (dot == 0)
			error("don't know pc for that source line");
		/* fall into ... */
#endif
	case 'b':
		if (kernel) {
			(void) printf("Not possible with -k option.\n");
			return;
		}
#if defined(KADB) && defined(i386)
	case 'a':			/* set data breakpoint (access) */
	case 'w':			/* set data breakpoint (write) */
	case 'p':
#endif
		bkptr = get_bkpt(dot);
		db_printf(2, "subpcs: bkptr=%X", bkptr);
		bkptr->initcnt = bkptr->count = count;
		bkptr->flag = BKPTSET;
#if defined(KADB) && defined(i386)
		if (modif == 'b')
			bkptr->type = BPINST;
		else if (ndebug == NDEBUG)
			error("bkpt: no more debug registers");
		else if (modif == 'p') {
	    		bkptr->type = BPDBINS;
			ndebug++;
		}
		else if (modif == 'a') {
			bkptr->type = BPACCESS;
			bkptr->len = datalen;
			ndebug++;
		} else if (modif == 'w') {
			bkptr->len = datalen;
			bkptr->type = BPWRITE;
			ndebug++;
		}
#endif
		check = MAXCOM-1;
		comptr = bkptr->comm;
		(void) rdc(); lp--;
		do
			*comptr++ = readchar();
		while (check-- && lastc != '\n');
		*comptr = 0; lp--;
		if (check)
			return;
		error("bkpt command too long");
#if defined(KADB) && defined(i386)
	case 'I':
	case 'i':
		printf("%x\n", inb(address));
		return;

	case 'O':
	case 'o':
		outb(address, count);
		return;

#else
#ifndef KADB
	case 'i':
	case 't':
		if (kernel) {
			(void) printf("Not possible with -k option.\n");
			return;
		}
		if (!hadaddress)
			error("which signal?");
		if (expv <= 0 || expv >=NSIG)
			error("signal number out of range");
		sigpass[expv] = modif == 'i';
		return;

	case 'l':
		if (!pid)
			error("no process");
		if (!hadaddress)
			error("no lwpid specified");
		db_printf(2, "subpcs: expv=%D, pid=%D", expv, pid);
		(void) set_lwp(expv, pid);
		return;

	case 'k':
		if (kernel)
			(void) printf("Not possible with -k option.\n");
		if (pid == 0)
			error("no process");
		printf("%d: killed", pid);
		endpcs();
		return;

	case 'r':
		if (kernel) {
			(void) printf("Not possible with -k option.\n");
			return;
		}
		setup();
		runmode = PTRACE_CONT;
		subtty();
		db_printf(2, "subpcs: running pid=%D", pid);
		break;

	case 'A':                       /* attach process */
		if (kernel) {
			(void) printf("Not possible with -k option.\n");
			return;
		}
		if (pid)
			error("process already attached");
		if (!hadaddress)
			error("specify pid in current radix");
		if (ptrace(PTRACE_ATTACH, address) == -1)
			error("can't attach process");
		pid = address;
		bpwait(0);
		printf("process %d stopped at:\n", pid);
		print_dis(Reg_PC);
		userpc = (addr_t)dot;
		return;

	case 'R':                       /* release (detach) process */
		if (kernel) {
			(void) printf("Not possible with -k option.\n");
			return;
		}
		if (!pid)
			error("no process");
		if (ptrace(PTRACE_DETACH, pid, readreg(Reg_PC), SIGCONT) == -1)
			error("can't detach process");
		pid = 0;
		return;
#endif !KADB
#endif !KADB && !i386

	case 'e':			/* execute instr. or routine */
		if (kernel) {
			(void) printf("Not possible with -k option.\n");
			return;
		}
#ifdef sparc
		/* Look for an npc that does not immediately follow pc.
		 * If that's the case, then look to see if the immediately
		 * preceding instruction was a call.  If so, we're in the
		 * delay slot of a pending call we that want to skip.
		 */
		if ((userpc + 4 != readreg(Reg_NPC)) &&
			/* Is the preceding instruction a CALL? */
			(((bchkget(userpc - 4, ISP) >> 6)  == SR_CALL_OP) ||
			/* Or, is the preceding instruction a JMPL? */
			(((bchkget(userpc - 4, ISP) >> 6) == SR_FMT3a_OP) &&
			((bchkget(userpc - 3, ISP) >> 2) == SR_JUMP_OP)))) {
			/* If there isn't a breakpoint there all ready */
			if (!bkptlookup(userpc + 4)) {
	       			bkptr = get_bkpt(userpc + 4);
				bkptr->flag = BKPT_TEMP;
				bkptr->count = bkptr->initcnt = 1;
				bkptr->comm[0] = '\n';
				bkptr->comm[1] = '\0';
			}
			goto colon_c;
		}
		else
			modif = 's';	/* Behave as though it's ":s" */
		/* FALL THROUGH */
#endif sparc
#ifdef i386
		/*
		 * Look for an call instr. If it is a call set break
		 * just after call so run thru the break point and stop
		 * at returned address.
		 */
		{
			unsigned op1;
			int	brkinc = 0;
			int	pc = readreg(REG_PC);
			
			op1 = (get (pc, DSP) & 0xff);
			/* Check for simple case first */
			if (op1 == 0xe8) 
				brkinc = 5; /* length of break instr */
			else if (op1 == 0xff) {
				/* This is amuch more complex case (ie pointer
				   to function call). */
				op1 = (get (pc+1, DSP) & 0xff);
				switch (op1) 
				{
				      case 0x15:
				      case 0x90:
					brkinc = 6;
					break;
				      case 0x50:
				      case 0x51:
				      case 0x52:
				      case 0x53:
				      case 0x55:
				      case 0x56:
				      case 0x57:
					brkinc = 3;
				      case 0x10:
				      case 0xd0:
				      case 0xd2:
				      case 0xd3:
				      case 0xd6:
				      case 0xd7:
					brkinc = 2;
				}
			}
			if (brkinc)
			{
				/* If there isn't a breakpoint there all ready */
				if (!bkptlookup(userpc + brkinc)) {
					bkptr = get_bkpt(userpc + brkinc);
					bkptr->flag = BKPT_TEMP;
#ifdef KADB
					bkptr->type = BPINST;
#endif
					bkptr->count = bkptr->initcnt = 1;
					bkptr->comm[0] = '\n';
					bkptr->comm[1] = '\0';
				}
				goto colon_c;
			}
			else
			    modif = 's';     /* Behave as though it's ":s" */
		}
		
		/* FALL THROUGH */
#endif i386

	case 's':
	case 'S':
		if (kernel) {
			(void) printf("Not possible with -k option.\n");
			return;
		}
		if (pid) {
			execsig = getsig(signo);
			db_printf(2, "subpcs: execsig=%D", execsig);
		} else {
			setup();
			loopcnt--;
		}
		runmode = PTRACE_SINGLESTEP;
#if 0
		if (modif == 's')
			break;
		if ((pctofilex(userpc), filex) == 0)
			break;
		subtty();
		for (i = loopcnt; i > 0; i--) {
			line = (pctofilex(userpc), filex);
			if (line == 0)
				break;
			do {
				loopcnt = 1;
				if (runpcs(runmode, execsig)) {
					hitbp = 1;
					break;
				}
				if (interrupted)
					break;
			} while ((pctofilex(userpc), filex) == line);
			loopcnt = 0;
		}
#endif
		break;

	case 'u':			/* Continue to end of routine */
		if (kernel) {
			(void) printf("Not possible with -k option.\n");
			return;
		}
		stacktop(&pos);
#ifdef	sparc
		savdot = pos.k_caller + 8;
#else	/* sparc */
		savdot = get(pos.k_fp + FR_SAVPC, DSP);
#endif	/* sparc */
		db_printf(2, "subpcs: savdot=%X", savdot);
		bkptr = get_bkpt(savdot);
		bkptr->flag = BKPT_TEMP;
#if defined(KADB) && defined(i386)
		bkptr->type = BPINST;
#endif
		/* Associate this breakpoint with the caller's fp/sp. */
#if defined(KADB) && defined(i386)
		bkptr->count = pos.k_fp;
#endif
		bkptr->initcnt = 1;
		bkptr->comm[0] = '\n';
		bkptr->comm[1] = '\0';
		/* Fall through */

	case 'c':
	colon_c:
		if (kernel) {
			(void) printf("Not possible with -k option.\n");
			return;
		}
		if (pid == 0)
			error("no process");
		runmode = PTRACE_CONT;
		execsig = getsig(signo);
		db_printf(2, "subpcs: execsig=%D", execsig);
		subtty();
		break;

	default:
		db_printf(3, "subpcs: bad modifier");
		error("bad modifier");
	}

	if (hitbp || (loopcnt > 0 && runpcs(runmode, execsig)))
#if !defined(KADB) || !defined(i386)
		m = "breakpoint%16t";
	else
		m = "stopped at%16t";
#else
		m = "breakpoint:\n";
	else
		m = "stopped at:\n";
#endif
	adbtty();
	printf(m);
	delbp();
	print_dis(Reg_PC);
}
