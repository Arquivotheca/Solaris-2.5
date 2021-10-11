/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)support_sparc.c	1.7	95/06/19 SMI" /* from SunOS 4.1 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/user.h>
#include <time.h>
#include <tzfile.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/mmu.h>
#include <sys/pte.h>
#include <sys/privregs.h>
#include <sys/bootconf.h>
#include <sys/debug/debugger.h>
#include <sys/ptrace.h>
#include <sys/sysmacros.h>
#include <sys/obpdefs.h>
#include <sys/openprom.h>
#include <stdarg.h>
#include <allregs.h>

u_int npmgrps;
u_int segmask;
int debugkadb = 0;
extern struct bootops *bootops;
extern int pagesize;
extern struct allregs_v9 regsave;


_exit()
{
	(void) prom_enter_mon();
#ifdef	sun4u
	reload_prom_callback();
#endif
}

int interrupted = 0;

getchar()
{
	register int c;

	while ((c = prom_mayget()) == -1)
		;
	if (c == '\r')
		c = '\n';
	if (c == 0177 || c == '\b') {
		putchar('\b');
		putchar(' ');
		c = '\b';
	}
	putchar(c);
	return (c);
}

/*
 * Read a line into the given buffer and handles
 * erase (^H or DEL), kill (^U), and interrupt (^C) characters.
 * This routine ASSUMES a maximum input line size of LINEBUFSZ
 * to guard against overflow of the buffer from obnoxious users.
 */
gets(buf)
	char buf[];
{
	register char *lp = buf;
	register c;

	for (;;) {
		c = getchar() & 0177;
		switch (c)	{
		case '[':
		case ']':
			putchar('\n');
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
			putchar('^');
			putchar('U');
			putchar('\n');
			continue;
		case 'c'&037:
			dointr(1);
			/* MAYBE REACHED */
			/* fall through */
		default:
		defchar:
			if (lp < &buf[LINEBUFSZ-1]) {
				*lp++ = c;
			} else {
				putchar('\b');
				putchar(' ');
				putchar('\b');
			}
			break;
		}
	}
}

dointr(doit)
{

	putchar('^');
	putchar('C');
	interrupted = 1;
	if (abort_jmp && doit) {
		_longjmp(abort_jmp, 1);
		/*NOTREACHED*/
	}
}

/*
 * Check for ^C on input
 */
tryabort(doit)
{

	if (prom_mayget() == ('c' & 037)) {
		dointr(doit);
		/* MAYBE REACHED */
	}
}

/*
 * Implement pseudo ^S/^Q processing along w/ handling ^C
 * We need to strip off high order bits as monitor cannot
 * reliably figure out if the control key is depressed when
 * prom_mayget() is called in certain circumstances.
 * Unfortunately, this means that s/q will work as well
 * as ^S/^Q and c as well as ^C when this guy is called.
 */
trypause()
{
	register int c;

	c = prom_mayget() & 037;

	if (c == ('s' & 037)) {
		while ((c = prom_mayget() & 037) != ('q' & 037)) {
			if (c == ('c' & 037)) {
				dointr(1);
				/* MAYBE REACHED */
			}
		}
	} else if (c == ('c' & 037)) {
		dointr(1);
		/* MAYBE REACHED */
	}
}

/*
 * Scaled down version of C Library printf.
 */
/*VARARGS1*/
printf(const char *fmt, ...)
{
	va_list x1;

	tryabort(1);
	va_start(x1, fmt);
	prf(fmt, x1);
	va_end(x1);
}

prf(char *fmt, va_list adx)
{
	register int b, c;
	register char *s;

loop:
	while ((c = *fmt++) != '%') {
	if (c == '\0')
			return;
		putchar(c);
	}
again:
	c = *fmt++;
	switch (c) {

	case 'l':
		goto again;
	case 'x': case 'X':
		b = 16;
		goto number;
	case 'd': case 'D':
	case 'u':		/* what a joke */
		b = 10;
		goto number;
	case 'o': case 'O':
		b = 8;
number:
		printn(va_arg(adx, u_long), b);
		break;
	case 'c':
		b = va_arg(adx, int);
		putchar(b);
		break;
	case 's':
		s = va_arg(adx, char *);
		while (c = *s++)
			putchar(c);
		break;
	}
	goto loop;
}

/*
 * Printn prints a number n in base b.
 * We don't use recursion to avoid deep kernel stacks.
 */
printn(u_long n, int b)
{
	char prbuf[11];
	register char *cp;

	if (b == 10 && (int)n < 0) {
		putchar('-');
		n = (unsigned)(-(int)n);
	}
	cp = prbuf;
	do {
		*cp++ = "0123456789abcdef"[n%b];
		n /= b;
	} while (n);
	do
		putchar(*--cp);
	while (cp > prbuf);
}

/*
 * Print a character on console.
 */
putchar(int c)
{
	/* XXX - If this is really necessary, prom_putchar() should do it. */
	if (c == '\n')
		(void) prom_putchar('\r');

	(void) prom_putchar(c);
}

/*
 * Fake gettimeofday call
 * Needed for ctime - we are lazy and just
 * give a bogus approximate answer
 */
gettimeofday(struct timeval *tp, struct timezone *tzp)
{

	tp->tv_sec = (1989 - 1970) * 365 * 24 * 60 * 60;	/* ~1989 */
	tzp->tz_minuteswest = 8 * 60;	/* PDT: California ueber alles */
	tzp->tz_dsttime = DST_USA;
}

int errno;

caddr_t
_sbrk(int incr)
{
	extern char start[], end[];
	static caddr_t lim;
	caddr_t val;
	register int i;

	if (nobrk) {
		printf("sbrk:  late call\n");
		errno = ENOMEM;
		return ((caddr_t)-1);
	}
	if (lim == 0)
		lim = (caddr_t)roundup((u_int)end, pagesize);
	if (incr == 0)
		return (lim);
	incr = btoc(incr);
	if ((lim + ctob(incr)) >= (caddr_t)((u_int)start + DEBUGSIZE)) {
		printf("sbrk:  lim %x + %x exceeds %x\n", lim,
		    ctob(incr), (u_int)start + DEBUGSIZE);
		errno = EINVAL;
		return ((caddr_t)-1);
	}
	if ((val = BOP_ALLOC(bootops, lim, ctob(incr), BO_NO_ALIGN)) != lim) {
		printf("sbrk: BOP_ALLOC failed.\n");
		errno = EINVAL;
		return ((caddr_t) -1);
	}
	pagesused += incr;
	for (i = 0; i < incr; i++, lim += pagesize) {
#if defined(sun4c)
		Setpgmap(lim, PG_V | PG_KW | PGT_OBMEM | getpgmap(lim));
#endif
#ifdef  sun4m
#if 0
		/*
		* XXX - Include VA_TO_PFN so that the pte protection
		* can be changed to be supervisor only.
		*/
		pfn = VA_TO_PFN(addr);
		tmppte.pte_int = PTEOF(0, pfn, MMU_STD_SRWX, 0);
		tmppte.pte_int = PTEOF(0, --lastpg, MMU_STD_SRWX, 0);
		Setpgmap(lim, tmppte.pte_int);
		mmu_flushall();
#endif
#endif
	}
	return (val);
}

#define	PHYSOFF(p, o)	\
	((physadr)(p)+((o)/sizeof (((physadr)0)->r[0])))

/*
 * Fake ptrace - ignores pid and signals
 * Otherwise it's about the same except the "child" never runs,
 * flags are just set here to control action elsewhere.
 */
ptrace(int request, int pid, char *addr, int data, char *addr2)
{
	int rv = 0;
	register int i, val;
	register int *p;

	db_printf(5, "ptrace: %s, pid=%X addr=%X, data=%X, addr2=%X sp=%X",
	    map(request), pid, addr, data, addr2, getsp());

	switch (request) {
	case PTRACE_TRACEME:	/* do nothing */
		break;

	case PTRACE_PEEKTEXT:
	case PTRACE_PEEKDATA:
		{		/* start of block */
		int success;
		char *dstaddr;

		rv = 0;
		success = 0;
		rv = peekl(addr);
		success |= (errno == 0);
		if (errno) {
			dstaddr = (char *) &rv;
			*((short *)dstaddr) = peek((short *)addr);
			success |= (errno == 0);
			if (errno) {
				*dstaddr = Peekc(addr);
				success |= (errno == 0);
				*(dstaddr+1) = Peekc(addr+1);
				success |= (errno == 0);
			}
			*((short *)dstaddr+2) = peek((short *)(addr+2));
			success |= (errno == 0);
			if (errno) {
				*(dstaddr+2) = Peekc(addr+2);
				success |= (errno == 0);
				*(dstaddr+3) = Peekc(addr+3);
				success |= (errno == 0);
			}
		}

		if (!success) {
			rv = -1;
			errno = EFAULT;
		}
		}	/* end of block */
		break;

	case PTRACE_PEEKUSER:
		break;

	case PTRACE_POKEUSER:
		break;

	case PTRACE_POKETEXT:
		rv = poketext(addr, data);
		break;

	case PTRACE_POKEDATA:
		{		/* start of block */
		int success;
		char *datap;

		rv = 0;
		success = 0;
		rv = pokel(addr, data);
		success |= (errno == 0);
		if (errno) {
			datap = (char *) &data;
			rv = pokes((short *)addr, *(short *)datap);
			success |= (errno == 0);
			if (errno) {
				rv = pokec(addr, *datap);
				success |= (errno == 0);
				rv = pokec(addr+1, *(datap+1));
				success |= (errno == 0);
			}
			rv = pokes((short *)(addr+2), *(short *)(datap+2));
			success |= (errno == 0);
			if (errno) {
				rv = pokec(addr+2, *(datap+2));
				success |= (errno == 0);
				rv = pokec(addr+3, *(datap+3));
				success |= (errno == 0);
			}
		}

		if (!success) {
			rv = -1;
			errno = EFAULT;
		}
		}	/* end of block */
		break;

	case PTRACE_SINGLESTEP:
		dotrace = 1;
		/* fall through to ... */
	case PTRACE_CONT:
		dorun = 1;
		if ((int)addr != 1) {
#ifdef	__sparcv9
			reg->r_pc = regsave.r_pc = (int)addr;
#else
			reg->r_pc = (int)addr;
#endif
		}
		break;

	case PTRACE_SETREGS:
#ifdef	__sparcv9
		rv = scopy(addr, (caddr_t)&regsave,
		    sizeof (struct allregs_v9));
#else
		rv = scopy(addr, (caddr_t)reg,
		    sizeof (struct allregs));
#endif	/* __sparcv9 */
		break;

	case PTRACE_GETREGS:
#ifdef	__sparcv9
		rv = scopy((caddr_t)&regsave, addr,
		    sizeof (struct allregs_v9));
#else
		rv = scopy((caddr_t)reg, addr,
		    sizeof (struct allregs));
#endif	/* sparc */

		break;

	case PTRACE_WRITETEXT:
	case PTRACE_WRITEDATA:
		rv = scopy(addr2, addr, data);
		break;

	case PTRACE_READTEXT:
	case PTRACE_READDATA:
		rv = scopy(addr, addr2, data);
		break;

	case PTRACE_KILL:
	case PTRACE_ATTACH:
	case PTRACE_DETACH:
	default:
		errno = EINVAL;
		rv = -1;
		break;
	}
	return (rv);
}

/*
 * This localtime is a modified version of offtime from libc, which does not
 * bother to figure out the time zone from the kernel, from environment
 * varaibles, or from Unix files.  We just return things in GMT format.
 */

static int mon_lengths[2][MONS_PER_YEAR] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
	31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static int year_lengths[2] = {
	DAYS_PER_NYEAR, DAYS_PER_LYEAR
};

struct tm *
localtime(const time_t *clock)
{
	register struct tm *tmp;
	register long days;
	register long rem;
	register int y;
	register int yleap;
	register int *ip;
	static struct tm tm;

	tmp = &tm;
	days = *clock / SECS_PER_DAY;
	rem = *clock % SECS_PER_DAY;
	while (rem < 0) {
		rem += SECS_PER_DAY;
		--days;
	}
	while (rem >= SECS_PER_DAY) {
		rem -= SECS_PER_DAY;
		++days;
	}
	tmp->tm_hour = (int) (rem / SECS_PER_HOUR);
	rem = rem % SECS_PER_HOUR;
	tmp->tm_min = (int) (rem / SECS_PER_MIN);
	tmp->tm_sec = (int) (rem % SECS_PER_MIN);
	tmp->tm_wday = (int) ((EPOCH_WDAY + days) % DAYS_PER_WEEK);
	if (tmp->tm_wday < 0)
		tmp->tm_wday += DAYS_PER_WEEK;
	y = EPOCH_YEAR;
	if (days >= 0)
	for (;;)	{
			yleap = isleap(y);
			if (days < (long) year_lengths[yleap])
				break;
			++y;
			days = days - (long) year_lengths[yleap];
		} else
		do {
		--y;
		yleap = isleap(y);
		days = days + (long) year_lengths[yleap];
	} while (days < 0);
	tmp->tm_year = y - TM_YEAR_BASE;
	tmp->tm_yday = (int) days;
	ip = mon_lengths[yleap];
	for (tmp->tm_mon = 0; days >= (long) ip[tmp->tm_mon]; ++(tmp->tm_mon))
		days = days - (long) ip[tmp->tm_mon];
	tmp->tm_mday = (int) (days + 1);
	tmp->tm_isdst = 0;
	/*
	 * tm_zone and and tm_gmoff are not defined in the tm struct anymore.
	 * Find out what to do about this.
	tmp->tm_zone = "GMT";
	tmp->tm_gmtoff = 0;
	 */
	return (tmp);
}

/*
 * Return the ptr in sp at which the character c appears;
 * NULL if not found
 */

#define	NULL	0

char *
index(char *sp, char c)
{

	do {
		if (*sp == c)
			return (sp);
	} while (*sp++);
	return (NULL);
}

/*
 * Return the ptr in sp at which the character c last
 * appears; NULL if not found
 */

#define	NULL	0

char *
rindex(char *sp, char c)
{
	register char *r;

	r = NULL;
	do {
		if (*sp == c)
			r = sp;
	} while (*sp++);
	return (r);
}

extern char target_bootname[];
extern char target_bootargs[];
extern char aline[];
extern char * module_path;

/*
 * Property intercept routines for kadb, so that it can
 * tell unix it's real name, and it's real bootargs. We
 * also let it figure out our virtual start and end addresses
 * rather than hardcoding them somewhere nasty.
 */
static int
kadb_getprop(struct bootops *bop, char *name, void *buf)
{
	extern char	start[];
	u_int start_addr = (u_int)start;

	if (strcmp("whoami", name) == 0) {
		(void) strcpy(buf, aline);
	} else if (strcmp("boot-args", name) == 0) {
		(void) strcpy(buf, target_bootargs);
	} else if (strcmp("debugger-start", name) == 0) {
		bcopy(&start_addr, buf, sizeof (caddr_t));
	} else if (strcmp("module-path", name) == 0) {
		(void) strcpy(buf, module_path);
	} else
		return (BOP_GETPROP(bop->bsys_super, name, buf));
	return (0);
}

static int
kadb_getproplen(struct bootops *bop, char *name)
{
	if (strcmp("whoami", name) == 0) {
		return (strlen(aline) + 1);
	} else if (strcmp("boot-args", name) == 0) {
		return (strlen(target_bootargs) + 1);
	} else if (strcmp("debugger-start", name) == 0) {
		return (sizeof (void *));
	} else if (strcmp("module-path", name) == 0) {
		return (strlen(module_path) + 1);
	} else
		return (BOP_GETPROPLEN(bop->bsys_super, name));
}

/* ARGSUSED3 */
void
early_startup(union sunromvec *rump, int shim, struct bootops *buutops)
{
	extern struct bootops *bootops;
	static struct bootops kadb_bootops;

	/*
	 * Save parameters from boot in obvious globals, and set
	 * up the bootops to intercept property look-ups.
	 */
	bootops = buutops;

	prom_init("kadb", rump);
	if (debugkadb)
		(void) prom_enter_mon();

	if (BOP_GETVERSION(bootops) != BO_VERSION) {
		prom_printf("WARNING: %d != %d => %s\n",
		    BOP_GETVERSION(bootops), BO_VERSION,
		    "mismatched version of /boot interface.");
	}

	bcopy((caddr_t)bootops, (caddr_t)&kadb_bootops,
	    sizeof (struct bootops));

	kadb_bootops.bsys_super = bootops;
	kadb_bootops.bsys_getprop = kadb_getprop;
	kadb_bootops.bsys_getproplen = kadb_getproplen;

	bootops = &kadb_bootops;

	(void) fiximp();
}
