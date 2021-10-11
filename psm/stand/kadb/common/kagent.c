/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

#ident  "@(#)kagent.c 1.7     95/02/01 SMI"

#include <sys/types.h>
#include "allregs.h"
#include <sys/errno.h>
#include <sys/debug/debugger.h>
#include <sys/rdb_v3.h>
#include <sys/kagent.h>
#include <sys/reboot.h>
#include <sys/cpuvar.h>
#include <sys/spl.h>
#include <symtab.h>
#ifdef NETKA
#include <rpc/rpc.h>
/* #include "../../lib/fs/nfs_inet/local.h" */
#include "local.h"
#include "sys/lance.h"
#include <sys/dle.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include "sys/sainet.h"
#endif /* NETKA */

extern int errno;	/* for peek error checking */

#define	kdbx_setjmp	_setjmp
#define	kdbx_longjmp	_longjmp
int dorun;
int kdbx_nounlock;		/* don't unlock the debugger */
				/* used for stepping */
static jmp_buf jb;
extern struct allregs regsave[];
int kdbx_error;
static int kdbx_interactive;
int kdbx_ncpus = 1;	/* incremented later if necessary */
extern int nwindows;
int *kdbx_stopcpus_addr;

extern int cur_cpuid;	/* cpu # currently running debugger */

static void runcommand();
void entering_kagent(void);
void exiting_kagent(void);

static int continue_ka;		/* used to terminate the kernel agent */
void
kdbx_exit()
{
	continue_ka = 0;	/* tell the main loop to stop */
}

#ifdef NETKA
struct sainet netaddrs;		/* container for net addr pair */
u_short remoteport;		/* udp port number of cohort */
u_short myport;			/* my udp port number */
u_short using_network;		/* boolean signifies ka will use the network */
#endif /* NETKA */

ka_main_loop(char ch)
{
	struct asym *sym;
#ifdef NETKA
#define	MYPORT 1080
	char pkt_buf[80];		/* buffer for packet info */
	int pkt_size;			/* size of packet */
	enum clnt_stat recv_stat;	/* result of net action */

	extern int network_up;
	extern struct sainet *get_sainet();
	struct sainet sainet;
	extern long ether_arp[];
	extern ether_addr_t	etherbroadcastaddr;	/* defined in inet.c */
	extern bootdev_t bootd;

	static int use_the_net;

	if (ch == 'n') use_the_net = 1;
#endif /* NETKA */

	continue_ka = 1;
	entering_kagent();

	if ((sym = lookup("kdbx_stopcpus")) == 0) {
		printf("kernel not compiled for kernel agent use\n");
		printf("kdbx_stopcpus variable not found in kernel\n");
		exiting_kagent();
		return;
	}

	kdbx_stopcpus_addr = (int *)sym->s_value;

	if ((sym = lookup("kdbx_useme")) == 0) {
		printf("kernel not compiled for kernel agent use\n");
		printf("kdbx_useme variable not found in kernel\n");
		exiting_kagent();
		return;
	}
	pokel(sym->s_value, 1);	 /* set kdbx_useme to true */

#ifdef NETKA
	if (use_the_net) {
		if (network_up == 0) {
			/* initialize debugger le driver */
			if (dle_init() == -1) {
				use_the_net = 0;
				printf("Network initialiazation failed\n");
				goto done;
			}
			dle_save_le();
			/* start the debugger le driver */
			if (dle_attach() == -1) {
				use_the_net = 0;
				printf("Network startup failed\n");
				goto done;
			}
			/* inlined and modified pieces of network open */
			dle_getmacaddr((char *)&sainet.sain_myether);

			bzero((caddr_t)&sainet.sain_myaddr,
			    sizeof (struct in_addr));
			bzero((caddr_t)&sainet.sain_hisaddr,
			    sizeof (struct in_addr));
			bcopy((caddr_t)etherbroadcastaddr,
			    (caddr_t)sainet.sain_hisether,
			    sizeof (ether_addr_t));

			/* bootd is a faked up thing to borrow code */
			bootd.handle = DLE_BOGUS_HANDLE;
			revarp(&bootd, &sainet, (char *) &ether_arp[0]);

			init_netaddr(&sainet);
		done:
			if (use_the_net == 0) {
				using_network = 0;	/* failure */
				network_up = 0;
				printf("Aborting Network Usage\n");
				return;			/* back to kadb */
			} else {
				network_up = 1;
				netaddrs = *get_sainet();
				using_network = 1;	/* success */
			}
		}
	}
#endif /* NETKA */

	while (continue_ka)
		runcommand();

	pokel(sym->s_value, 0); /* set kdbx_useme to false */

	exiting_kagent();
}

static void
kdbx_putchar(char c)
{
	if (kdbx_interactive)
	prom_putchar(c);
}

static
kdbx_getchar()
{
	register int c;

	while ((c = prom_mayget()) == -1)
		;
	if (c == '\r')
		c = '\n';
	if (c == 0177 || c == '\b') {
		kdbx_putchar('\b');
		kdbx_putchar(' ');
		c = '\b';
	}
	kdbx_putchar(c);
	return (c);
}

/*
 * Read a line into the given buffer and handles
 * erase (^H or DEL), kill (^U), and interrupt (^C) characters.
 * This routine ASSUMES a maximum input line size of LINEBUFSZ
 * to guard against overflow of the buffer from obnoxious users.
 */
static
kdbx_gets(buf)
	char buf[];
{
	register char *lp = buf;
	register c;

	for (;;) {
		c = kdbx_getchar() & 0177;
		switch (c) {
		case '[':
		case ']':
			if (lp != buf)
				goto defchar;
			*lp++ = c;
		case '\n':
		case '\r':
			*lp++ = '\0';
			return;
		case '\b':
			lp--;
			if (lp < buf)
				lp = buf;
			continue;
		case 'u'&037:			/* ^U */
			lp = buf;
			kdbx_putchar('^');
			kdbx_putchar('U');
			kdbx_putchar('\n');
			continue;
		case 'c'&037:
			kdbx_putchar('^');
			kdbx_putchar('C');
			continue;
		default:
		defchar:
			if (lp < &buf[KA_MAXPKT-1]) {
				*lp++ = c;
			} else {
				kdbx_putchar('\b');
				kdbx_putchar(' ');
				kdbx_putchar('\b');
			}
			break;
		}
	}
}

static int argstosend;
static char *argv[KA_MAXARGS];
static u_long argul[KA_MAXARGS];
static char linebuf[KA_MAXPKT];

static void
replyarg(ul)
	u_long ul;
{
	argul[argstosend++] = ul;
}

static void
setbpt(int *ul)
{
	int vaddr = ul[0];
/* ta 0x7e */
#define	BRKPNT_INSTR	0x91D0207E

	poketext(vaddr, BRKPNT_INSTR);
}

static void
clrbpt(int *ul)
{
	int vaddr = ul[0];
	int instr = ul[1];
	register int i;

	poketext(vaddr, instr);
}

static void
nullcommand(int *ul)
{
}

/*
 * The +1 here is 'cause the saved psr is from the trap window
 */
#define	CWP  (((regsave[cpu].r_psr & 15) + 1) % nwindows)

#define	IS_GLOBAL(x)	(((x) >= RN_GBL(0)) && ((x) <= RN_GBL(7)))
#define	IS_LOCAL(x)	(((x) >= RN_LCL(0)) && ((x) <= RN_LCL(7)))
#define	IS_IN(x)	(((x) >= RN_IN(0)) && ((x) <= RN_IN(7)))
#define	IS_OUT(x)	(((x) >= RN_OUT(0)) && ((x) <= RN_OUT(7)))

static int *
mapregs(cpu, type, regno)
	int cpu;
	int type;
	int regno;
{
	int *ip;
	u_int savewin;
	static int g0;
	struct allregs *regs;
	int cwp;
	static unsigned int kdbx_regsave_p = 0;
	static int allregs_size;

	/*
	 *	the registers are saved in a two different places
	 *	depending on if this is the processor that entered
	 *	the debugger or not.  If this is the processor that
	 *	entered, the regs are in the regsave struct.  For
	 *	other processors the regs need to be found in the
	 *	kernel's other regs storage area.
	 */

	if (cpu == cur_cpuid) {
		regs = regsave;
		/* regs = &regsave; old */
	} else {
		if (kdbx_regsave_p == 0xffffffff) {
			return ((int *)0);
		}

		if (kdbx_regsave_p == 0) {
			struct asym *sym;
			int max_win;
			/* need to look it up */
			if ((sym = lookup("kdbx_regsave")) == 0) {
			    printf(
			"mapregs: kernel symbol kdbx_regsave not found\n");
			    kdbx_regsave_p = 0xffffffff;
			    return ((int *)0);
			}
			kdbx_regsave_p = sym->s_value;

			if ((sym = lookup("kdbx_maxwin")) == 0) {
				max_win = 8;	/* default */
			} else {
				max_win = peekl(sym->s_value);
			}
			allregs_size = sizeof (struct allregs);
			if (MAXKADBWIN != max_win) {
				allregs_size -= ((MAXKADBWIN - max_win) *
				    sizeof (struct rwindow));
			}
		}
		regs = (struct allregs *)(kdbx_regsave_p +
		    (cpu * allregs_size));
	}

	switch (type) {
	case SR_IREG:
		cwp = (((regs->r_psr & 0xf) + 1) % nwindows);

		if (IS_GLOBAL(regno)) {
			if (regno == RN_GBL(0)) {
				ip = &g0;
				g0 = 0;
			} else {
			    ip = &regs->r_globals[regno-RN_GBL(1)];
			}
		} else if (IS_LOCAL(regno)) {
			ip = &regs->r_window[cwp].rw_local[regno-RN_LCL(0)];
		} else if (IS_IN(regno)) {
			ip = &regs->r_window[cwp].rw_in[regno-RN_IN(0)];
		} else if (IS_OUT(regno)) {
			savewin = (cwp -1) % nwindows;
			if (savewin == -1)
				savewin = nwindows - 1;
			ip = &regs->r_window[savewin].rw_in[regno-RN_OUT(0)];
		} else {
			ip = (int *)0;
		}
		break;
	case SR_ISTATE:
		switch (regno) {
			case RN_WIM:	ip = &(regs->r_wim);	break;
			case RN_TBR:	ip = &(regs->r_tbr);	break;
			case RN_PSR:	ip = &(regs->r_psr);	break;
			case RN_PC:	ip = &(regs->r_pc);	break;
			case RN_NPC:	ip = &(regs->r_npc);	break;
			case RN_Y:	ip = &(regs->r_y);	break;
			default:	ip = (int *)0;		break;
		}
		break;
	default:
		ip = (int *)0; break;
		break;
	}
	return (ip);
}

static void
rdreg(int *ul)
{
	int cpu = ul[0];
	int type = ul[1];
	int regno = ul[2];
	int *valp;

	if ((valp = mapregs(cpu, type, regno)) == (int *)0) {
		kdbx_error = KA_ERROR_ARGS;
		return;
	}
	replyarg(*valp);
}

static void
wrreg(int *ul)
{
	int cpu = ul[0];
	int type = ul[1];
	int regno = ul[2];
	int *valp;

	if ((valp = mapregs(cpu, type, regno)) == (int *)0) {
		kdbx_error = KA_ERROR_ARGS;
		return;
	}
	*valp = ul[3];
}

static void
cont(int *ul)
{
	extern int dorun;
	int cpu_id = ul[0];

	dorun = 1;
}

static void
ka_stop(int *ul)
{
/* this never gets entered as a command */
}

static void
interactive(int *ul)
{
	kdbx_interactive = (kdbx_interactive ? 0 : 1);
}

static void
step(int *ul)
{
	dorun = 1;
	kdbx_nounlock++;
#ifdef NETKA
	if (using_network) {
		dle_detach();
		dle_restore_le();
	}
#endif /* NETKA */

	exiting_kagent();
	doswitch();
	entering_kagent();

#ifdef NETKA
	if (using_network) {
		dle_save_le();
		dle_attach();
	}
#endif /* NETKA */
	kdbx_nounlock = 0;
}

static void
switch_cpu(int *ul)
{
	kdbx_error = KA_ERROR_CMD;
}

static void
setwpt(int *ul)
{
	kdbx_error = KA_ERROR_CMD;
}

static void
clrwpt(int *ul)
{
	kdbx_error = KA_ERROR_CMD;
}

static void
rdmem(int *ul)
{
	register int i;
	u_int val;

	u_int vaddr = ul[0];
	u_int size = ul[1];
	u_int count = ul[2];

	for (i = 0; i < count; i++) {
		switch (size) {
			case 1: val = (u_int)Peekc(vaddr); break;
			case 2: val = (u_int)peek(vaddr); break;
			case 4: val = (u_int)peekl(vaddr); break;
			default: kdbx_error = KA_ERROR_SIZE; break;
		}
		if (kdbx_error)
			return;
		replyarg(val);
		vaddr += size;
	}
}

static void
wrmem(int *ul)
{
	register int i;

	u_int vaddr = ul[0];
	u_int size = ul[1];
	u_int count = ul[2];
	u_int val = ul[3];

	for (i = 0; i < count; i++) {
		switch (size) {
		case 1: (void)pokec(vaddr, (char)val); break;
		case 2: (void)pokes(vaddr, (short)val); break;
		case 4: (void)pokel(vaddr, val); break;
		default:
			kdbx_error = KA_ERROR_SIZE;
			break;
		}
		if (kdbx_error)
			return;
		vaddr += size;
	}
}

#ifdef notdef
/* this is processor specific and I don't think it is used */
static void
rdasimem(int *ul)
{
	register int i;
	u_int val;

	u_int cpu = ul[0];
	u_int asi = ul[1];
	u_int addr = ul[2];
	u_int size = ul[3];
	u_int count = ul[4];

	/* XXX need to switch */
	if (cpu != cur_cpuid) {
		kdbx_error = KA_ERROR_ARGS;
		return;
	}
	for (i = 0; i < count; i++) {
		switch (size) {
		case 1: val = asi_peekc(asi, addr); break;
		case 2: val = asi_peeks(asi, addr); break;
		case 4: val = asi_peekl(asi, addr); break;
		default:
			kdbx_error = KA_ERROR_SIZE;
			break;
		}
		if (kdbx_error)
			return;
		replyarg(val);
		addr += size;
	}
}

static void
wrasimem(int *ul)
{
	register int i;

	u_int cpu = ul[0];
	u_int asi = ul[1];
	u_int addr = ul[2];
	u_int size = ul[3];
	u_int count = ul[4];
	u_int val = ul[5];

	/* XXX need to switch */
	if (cpu != cur_cpuid) {
		kdbx_error = KA_ERROR_ARGS;
		return;
	}
	for (i = 0; i < count; i++) {
		switch (size) {
		case 1: asi_pokec(asi, addr, val); break;
		case 2: asi_pokes(asi, addr, val); break;
		case 4: asi_pokel(asi, addr, val); break;
		default:
			kdbx_error = KA_ERROR_SIZE;
			break;
	}
		if (kdbx_error)
			return;
		addr += size;
	}
}
#else /* notdef */
static void
rdasimem(int *ul)
{
}
static void
wrasimem(int *ul)
{
}
#endif /* notdef */

static void
rdphysmem(int *ul)
{
	kdbx_error = KA_ERROR_CMD;
}

static void
wrphysmem(int *ul)
{
	kdbx_error = KA_ERROR_CMD;
}

static void
getcurcpu(int *ul)
{
	replyarg(cur_cpuid);
}

static void
getncpus(int *ul)
{
	struct asym *sym;

	if ((sym = lookup("ncpus")) == 0) {
		printf("getncpus: ncpus not found\n");
		replyarg(1);
	}

	kdbx_ncpus = peekl(sym->s_value);
	if (errno == EFAULT) {
		printf("getncpus: unable to read ncpus value\n");
		kdbx_ncpus = 1;
	}

	replyarg(kdbx_ncpus);
}

static void
getcpulist(int *ul)
{
	int i;
	static struct cpu **cpu;
	int foundcpu = 0;

	if (!foundcpu) {
		if (lookup("cpu") != 0) {
			cpu = (struct cpu **)(lookup("cpu")->s_value);
			foundcpu = 1;
		} else {
			replyarg(0);
			return;
		}
	}

	for (i = 0; i < NCPU; i++)
		if ((cpu[i] != NULL) && (cpu[i]->cpu_flags | CPU_READY))
			replyarg((u_long)i);
}

struct command {
	void (*cmd_func)();
	char *cmd_inter;
	int cmd_nargs;
};

static struct command commands [KA_NCOMMANDS] = {
/* 0x00 -- KA_NULL */
	{ nullcommand, "null", 0 },
/* 0x01 -- KA_SET_BPT vaddr */
	{ setbpt, "setbpt", 1 },
/* 0x02 -- KA_CLR_BPT vaddr instr */
	{ clrbpt, "clrbpt", 2 },
/* 0x03 -- KA_SET_WPT vaddr */
	{ setwpt, "setwpt", 1 },
/* 0x04 -- KA_CLR_WPT vaddr */
	{ clrwpt, "clrwpt", 1 },
/* 0x05 -- KA_RD_MEM vaddr size count */
	{ rdmem, "rdmem", 3 },
/* 0x06 -- KA_WRT_MEM vaddr size count value */
	{ wrmem, "wrmem", 4 },
/* 0x07 -- KA_RD_ASIMEM cpu_id asi paddr size count */
	{ rdasimem, "rdasimem", 5 },
/* 0x08 -- KA_WRT_ASIMEM cpu_id asi paddr size count value */
	{ wrasimem, "wrasimem", 6 },
/* 0x09 -- KA_RD_PHYSMEM paddr-hi paddr-lo size count */
	{ rdphysmem, "rdphysmem", 4 },
/* 0x0A -- KA_WRT_PHYSMEM paddr-hi paddr-lo size count value */
	{ wrphysmem, "wrphysmem", 5 },
/* 0x0B -- KA_RD_REG cpu_id type regno */
	{ rdreg, "rdreg", 3 },
/* 0x0C -- KA_WRT_REG cpu_id type regno value */
	{ wrreg, "wrreg", 4 },
/* 0x0D -- KA_CONTINUE cpu_id */
	{ cont, "cont",  1 },
/* 0x0E -- KA_STEP */
	{ step, "step", 0 },
/* 0x0F -- KA_STOP */
	{ ka_stop, "stop", 0 },
/* 0x10 -- KA_SWITCH cpu_id */
	{ switch_cpu, "switch", 1 },
/* 0x11 -- KA_NCPU */
	{ getncpus, "getncpus", 0 },
/* 0x12 -- KA_EXIT */
	{ kdbx_exit, "exit", 0 },
/* 0x13 -- KA_INTERACTIVE */
	{ interactive, "leave", 0 },
/* 0x14 -- KA_CURCPU */
	{ getcurcpu, "getcurcpu", 0 },
/* 0x15 -- KA_CPULIST */
	{ getcpulist, "getcpulist", 0 }
};

static int
xlatecommand(char *com)
{
	int i;

	for (i = 0; i < KA_NCOMMANDS; i++)
		if (strcmp(com, commands[i].cmd_inter) == 0)
			break;
	return (i);
}

static void
sendreply()
{
	register int i, j;
#ifdef NETKA
#define	BUFSIZE	32
	unsigned int buf[BUFSIZE]; /* large to exceed minimum pkt size */
#define	ACKBUFSIZE 10
	char *ackbuf[ACKBUFSIZE];
	int acksize;

	if (using_network) {
		buf[0] = 0xff & kdbx_error;	/* low byte is status */
		buf[0] |= (0xff & argstosend) << 24; /* high is num args */
		if (!kdbx_error) {
			for (i = 0, j = 1; i < argstosend; i++, j++) {
				buf[j] = argul[i];
			}
		}

		/* send the error code with a prefix of the error code */
		acksize = ACKBUFSIZE;
		myport = MYPORT;
		/* I transmit the whole buffer because the net may */
		/* ignore packets under 64 bytes.  The ethernet, ip and */
		/* udp headers are about 42 bytes.  22 bytes should put */
		/* us over the minimum. */
		xmit((caddr_t *)buf, BUFSIZE, ackbuf, &acksize,
		    &myport, remoteport, 0, 1000, &netaddrs);

		return;
	}
#endif

	prom_printf("%x ", kdbx_error);
	if (!kdbx_error) {
		for (i = 0; i < argstosend; i++) {
			prom_printf("%x ", argul[i]);
		}
	}
	prom_printf("\n");
}

static void
runcommand()
{
	register int argc, i;
	u_int command;
	u_long hex_to_ul();
	char *cmd;
	int len;
#ifdef NETKA
#define	ETHERBUFSIZE 24
	char ackbuf[ETHERBUFSIZE];
	char buf[ETHERBUFSIZE];
	int acksize;

#endif /* NETKA */

	kdbx_error = 0;
	argstosend = 0;
#ifdef NETKA
	if (using_network) {	/* using network */
		int rcv_size;
		enum clnt_stat recv_stat;	/* result of net action */

		rcv_size = KA_MAXPKT;

		do {
			recv_stat = recv((caddr_t)linebuf, &rcv_size, 100000,
			    MYPORT, &remoteport, &netaddrs);
		} while (recv_stat == RPC_TIMEDOUT);

	} else { /* not using network */
#endif /* NETKA */
		if (kdbx_interactive)
			prom_printf("kdbx-agent: ");
		kdbx_gets(linebuf);
		/* this is to add exit for users */
		if (((linebuf[0] == 'q') || (linebuf[1] == 'q')) ||
		    ((linebuf[0] == 'e') && (linebuf[1] == 'x'))) {
			strcpy(linebuf, "02 12"); /* create exit command */
		}
#ifdef NETKA
	} /* not using network */
#endif /* NETKA */
/* Read character count and verify the packet  */
	cmd = linebuf;
	if ((!kdbx_interactive) && (*linebuf != '\0')) {
		if (linebuf[2] == ' ') {
			linebuf[2] = '\0';
			cmd = &linebuf[3];
			if ((len = hex_to_ul(&linebuf[0])) != 0) {
				if (len != strlen(cmd))
					kdbx_error = KA_ERROR_PKT;
			} else {
				kdbx_error = KA_ERROR_PKT;
			}
		} else {
			kdbx_error = KA_ERROR_PKT;
		}
		if (kdbx_error) {
			sendreply();
			return;
		}
	}
	argc = parseargs(cmd);
	if (kdbx_error) {
		sendreply();
		return;
	}
	if (kdbx_interactive) {
		for (i = 1; i < argc; i++)
			argul[i] = hex_to_ul(argv[i]);
		command = xlatecommand(argv[0]);
	} else {
		for (i = 0; i < argc; i++)
			argul[i] = hex_to_ul(argv[i]);
		command = argul[0];
	}

	if (command >= KA_NCOMMANDS) {
		kdbx_error = KA_ERROR_CMD;
		sendreply();
		return;
	}
	if (--argc != commands[command].cmd_nargs) {
		kdbx_error = KA_ERROR_ARGS;
		sendreply();
		return;
	}
	(*commands[command].cmd_func)(&argul[1]);
	sendreply();
	if (dorun) {
#ifdef NETKA
		if (using_network) {	/* using network */
			if (remoteport) {	/* a peer to talk to */
			}
			dle_detach();
			dle_restore_le();
		}
#endif /* NETKA */
		exiting_kagent();

		/*
		 * Done debugging, so switch back to main (debuggee) registers
		 * This long jumps to kdbx_cmd.
		 */
		doswitch();

		entering_kagent();

		/* tell the server we're alive */
#ifdef NETKA
		if (using_network) {	/* using network */
			if (remoteport) {	/* a peer to talk to */
			    dle_save_le();
			    dle_attach();
			    acksize = ETHERBUFSIZE;
			    myport = MYPORT;
			    strcpy(buf, "XYZRDB");
			    /* sending more than string length to meet */
			    /* minimum packet size of 64 bytes, */
			    /* ethernet + ip + udp headers = 42 bytes */
			    xmit((caddr_t *)buf, ETHERBUFSIZE, ackbuf, &acksize,
				&myport, remoteport, 0, 1000, &netaddrs);
			} else {	/* we have no peer */
			    prom_printf("Stopped with no server connected\n");
			    continue_ka = 0; /* back to kadb */
			}
		} else {
#endif /* NETKA */
			prom_printf("XYZRDB");
#ifdef NETKA
		}
#endif /* NETKA */
		dorun = 0;
	}
}

static int
parseargs(buf)
	char *buf;
{
	register char *cp;
	register int i, argc;

	for (i = 0; i < KA_MAXARGS; i++)
		argv[i] = (char *)0;
	argc = 0; argv[argc++] = buf;
	for (cp = buf; *cp; cp++) {
		if (*cp == ' ') {
			*cp = '\0';
			if (argc >= KA_MAXARGS) {
				kdbx_error = KA_ERROR_ARGS;
				break;
			}
			argv[argc++] = cp + 1;
		}
	}
	return (argc);
}

static char *
eostring(char *str)
{
	while (*str++ != '\0')
		;

	return (str - 1);
}
static char *
ul_to_hex(char *cp, unsigned int num)
{
	static char *digit = "0123456789abcdef";
	unsigned int i, mask, shift, found_digit = 0;
	char *ret_cp = cp;

	if (num == 0) {
		*cp++ = '0';
		*cp = '\0';
		return (ret_cp);
	}

	for (mask = 0xf0000000, shift = 28; mask; mask >>= 4, shift -= 4) {
		i = (num & mask);
		if ((i != 0) || (found_digit)) {
			*cp++ = digit[ i>>shift ];
			found_digit = 1;
		}
	}
	*cp = '\0';
	return (ret_cp);
}

static u_long
hex_to_ul(char *cp)
{
	u_long ul = 0;
	char c;

	if (cp == 0)
		return (0);
	while (c = *cp++) {
		if (c == 'x') {
			continue;
		} else {
			ul <<= 4;
			if (c >= '0' && c <= '9') {
				ul += c - '0';
			} else if (c >= 'a' && c <= 'f') {
				ul += c - 'a' + 0xa;
			} else if (c >= 'A' && c <= 'F') {
				ul += c - 'A' + 0xa;
			} else {
				kdbx_error = KA_ERROR_ARGS;
				return (0);
			}
		}
	}
	return (ul);
}

static int ka_saved_spl;

static void
ka_splr_preprom(void)
{
	ka_saved_spl = ka_splr(ipltospl(13));
	/*
	 * this is "13" because kern_splr_preprom uses spl7 and
	 * 13 is what spl7() uses
	 */
}

static void
ka_splx_postprom(void)
{
	(void) ka_splx(ka_saved_spl);
}

static void (*preprom_save)(void);
static void (*postprom_save)(void);

static int in_kagent = 0;

void
entering_kagent(void)
{
	if (in_kagent) {
		prom_printf("entering_kagent: Already in kagent\n");
	} else {
		preprom_save = (void (*)(void))
		    prom_set_preprom(ka_splr_preprom);
		postprom_save = (void (*)(void))
		    prom_set_postprom(ka_splx_postprom);
		in_kagent = 1;
	}
}

void
exiting_kagent(void)
{
	if (!in_kagent) {
		prom_printf("exiting_kagent: Not in kagent 0x%x\n");
	} else {
		(void) prom_set_preprom(preprom_save);
		(void) prom_set_postprom(postprom_save);
		in_kagent = 0;
	}
}

int
devopen(int unused) /* open SUNMON devices */
{
	prom_printf("The kernel agent will not work on this architecture\n");
}
