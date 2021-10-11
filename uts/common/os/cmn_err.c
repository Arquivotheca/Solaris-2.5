/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)cmn_err.c	1.66	95/09/08 SMI"	/* from SVr4.0 1.17 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/vnode.h>
#include <sys/inline.h>
#include <sys/debug.h>
#include <sys/varargs.h>
#include <sys/strlog.h>
#include <sys/debug/debug.h>
#include <sys/user.h>
#include <sys/vnode.h>
#include <sys/cred.h>
#include <sys/session.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/consdev.h>
#include <sys/strredir.h>
#include <sys/msgbuf.h>
#include <sys/reboot.h>

#include <sys/proc.h>		/* XXX - for proc_init */

#include <sys/thread.h>		/* XXX - just for cross-CPU printfs */
#include <sys/cpuvar.h>		/* XXX - just for cross-CPU printfs */

#include <sys/fs/snode.h>

/*
 * cmn_err -- Save output in a buffer where we can look at it with a debugger
 * or with "crash" or with dmesg(8). If the message begins with a '!',
 * then only put it in the buffer, not out to the console.
 */

#define	LBUFSZ	128
#define	NSKMSG	10

/*
 * Circular buf for calling writekmsg via softcall.
 */
struct softkmsg {
	int filled;
	int type;
	int where;
	int len;
	vnode_t *vp;
	char buf[LBUFSZ + 1];
};

static struct softkmsg skmsg[NSKMSG];

extern int	msgbufinit;
extern kmutex_t	prf_lock;

static void	prf_internal(const char *, va_list, vnode_t *,
	int, int, char *, int);
static void	writekmsg(char *, int, vnode_t *, int, int);
static char	*printn(u_longlong_t n, int b, int width, int pad,
	char *lpb, char *linebuf, vnode_t *vp, int prt_where, int prt_type,
	int sbuf_len);
static char	*printc(char c,
	char *lbp, char *linebuf, vnode_t *vp, int prt_where, int prt_type,
	int sbuf_len);
static void	output_line(char *linep, int len, vnode_t *vp,
	int prt_type, int prt_where);
static void	extra_print(char *fmt, va_list adx, int prt_where,
	int prt_type, char *extra_string);
static void	printf_internal(int prt_where, int prt_type, char *fmt, ...);

#ifdef DEBUG
int aask, aok;
/*
 * set a breakpoint here to get back to demon at regular intervals.
 */
void
catchmenow(void)
{}
#endif

/*
 * Can be cleared to cause panicing kernels to show assertion failures
 */
int aignore = 1;


kmutex_t	framebuffer_lock;

/*
 * Scaled down version of C Library printf.
 * Used to print diagnostic information directly on console tty.
 * Since it is not interrupt driven, all system activities are
 * suspended.  Printf should not be used for chit-chat.
 *
 * One additional format: %b is supported to decode error registers.
 * Usage is:
 *	printf("reg=%b\n", regval, "<base><arg>*");
 * Where <base> is the output base expressed as a control character,
 * e.g. \10 gives octal; \20 gives hex.  Each arg is a sequence of
 * characters, the first of which gives the bit number to be inspected
 * (origin 1), and the next characters (up to a control character, i.e.
 * a character <= 32), give the name of the register.  Thus
 *	printf("reg=%b\n", 3, "\10\2BITTWO\1BITONE\n");
 * would produce output:
 *	reg=3<BITTWO, BITONE>
 */

/*PRINTFLIKE1*/
void
printf(char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	prf(fmt, adx, (vnode_t *)NULL, PRW_CONS | PRW_BUF);
	va_end(adx);
}

/*PRINTFLIKE3*/
static void
printf_internal(int prt_where, int prt_type, char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
		prf_internal(fmt, adx, (vnode_t *)NULL, prt_where, prt_type,
			(char *)NULL, 0);
	va_end(adx);
}


/*
 * vprintf is for folks who already are dealing with some varargs
 */
void
vprintf(char *fmt, va_list adx)
{
	prf(fmt, adx, (vnode_t *)NULL, PRW_CONS | PRW_BUF);
}

/*
 * uprintf prints to the current user's terminal. It may block
 * if the tty queue is overfull. No message is printed if the
 * queue does not clear in a reasonable time. Should determine
 * whether current terminal user is related to this process.
 */
/*PRINTFLIKE1*/
void
uprintf(char *fmt, ...)
{
	register vnode_t *vp;
	va_list x1;

	va_start(x1, fmt);
	if ((vp = curproc->p_sessp->s_vp) == NULL) {
		prf_internal(fmt, x1, (vnode_t *)NULL, PRW_CONS | PRW_BUF, 0,
			(char *)NULL, 0);
		/* no controlling tty - send it to the console */
	} else {
		struct vnode *cvp;

		/*
		 * XXX - If the process controlling tty is the "workstation"
		 * console or hardware console (serial port), send the output
		 * to the current console, in case it was redirected after
		 * the process was assigned its controlling tty.
		 *
		 * Note:  The common vnode pointers have to be compared
		 * since the shadow vnodes are always different.
		 */
		cvp = common_specvp(vp);
		if (((rwsconsvp != NULL) &&
		    (cvp == common_specvp(rwsconsvp))) ||
		    cvp == common_specvp(rconsvp))
			vp = NULL;
		prf_internal(fmt, x1, vp, PRW_CONS | PRW_BUF, 0,
		    (char *)NULL, 0);
	}
	va_end(x1);
}

/*
 * sprintf -- this is folded into prf_internal() and will provide
 * the same level of functionality as available for other kernel related
 * printf-style operations. This function is needed for vcmn_err() so it
 * can print notes and warnings with one call to prf_internal() and cause
 * one message to be logged as opposed to three previously.
 */
/*PRINTFLIKE2*/
char *
sprintf(char *buf, const char *fmt, ...)
{
	char *rval;
	va_list ap;

	va_start(ap, fmt);
	rval = vsprintf(buf, fmt, ap);
	va_end(ap);

	return (rval);
}

char *
vsprintf(char *buf, const char *fmt, va_list ap)
{
	return (vsprintf_len(-1, buf, fmt, ap));
}

/*PRINTFLIKE3*/
char *
sprintf_len(int len, char *buf, const char *fmt, ...)
{
	char *rval;
	va_list ap;

	va_start(ap, fmt);
	rval = vsprintf_len(len, buf, fmt, ap);
	va_end(ap);

	return (rval);
}

char *
vsprintf_len(int len, char *buf, const char *fmt, va_list ap)
{
	if (buf == (char *)0 || fmt == (char *)0) {
		return ((char *)0);
	}

	prf_internal(fmt, ap, (vnode_t *)NULL, PRW_STRING, 0, buf, len);
	return (buf);
}

/*PRINTFLIKE2*/
void
cmn_err(int level, char *fmt, ...)
{
	va_list adx;

#ifndef LOCKNEST
	va_start(adx, fmt);
	vcmn_err(level, fmt, adx);
	va_end(adx);
#endif
}

void
vcmn_err(register int level, register char *fmt, va_list adx)
{
	short prt_where;
	static char whirl[] = "|/-\\";
	static int whirlpos;

	/*
	 * Set up to print to msgbuf, console, or both
	 * as indicated by the first character of the
	 * format.
	 */

	if (*fmt == '!') {
		prt_where = PRW_BUF;
		fmt++;
	} else if (*fmt == '^') {
		prt_where = PRW_CONS;
		fmt++;
	} else if (*fmt == '?') {
		/*
		 * This is a SunDDI extension - format strings
		 * beginning with a '?' are always logged, but
		 * CE_CONT level messages are only printed on
		 * the console if we booted with the 'verbose'
		 * flag to boot (or we ORed it into boothowto
		 * via /etc/system).  Other message priorities
		 * are unaffected.
		 *
		 * Once icode() has run, 'proc_init' is non-NULL.
		 * XXX	This should be keyed off rconsvp
		 */
		fmt++;
		prt_where = PRW_BUF;
		if (level != CE_CONT || (boothowto & RB_VERBOSE))
			prt_where |= PRW_CONS;
		else if (proc_init == (proc_t *)0) {

			/*
			 * Print an indication of progress to the console.
			 */
			mutex_enter(&prf_lock);
			cnputc((int)whirl[whirlpos++ & 3], 0);
			cnputc((int)'\b', 0);
			mutex_exit(&prf_lock);
		}
	} else
		prt_where = PRW_BUF | PRW_CONS;

	switch (level) {
		case CE_CONT:
			prf_internal(fmt, adx, (vnode_t *)NULL, prt_where,
				0, (char *)NULL, 0);
			break;

		case CE_NOTE:
			extra_print(fmt, adx, prt_where, SL_NOTE, "NOTICE");
			break;

		case CE_WARN:
			extra_print(fmt, adx, prt_where, SL_WARN, "WARNING");
			break;

		case CE_PANIC:
			/*
			 * Processes logging console messages
			 * will never run.  Force message to
			 * go to console.
			 */
			conslogging = 0;

			/*
			 * XXX - dump volatiles somehow?
			 */
			do_panic(fmt, adx);
			break;

		default:
			cmn_err(CE_PANIC,
			    "unknown level in cmn_err (level=%d, msg=\"%s\")",
			    level, fmt);
	}
}

/*
 * extra_print -- useful for printing an extra string before the real thing
 *
 * It is good to make this a separate function so that other functions
 * will not carry the overhead of allocating a long buffer on the stack.
 */
static void
extra_print(char *fmt, va_list adx, int prt_where, int prt_type,
	char *extra_string)
{
	char	sbuf[256];

	(void) vsprintf_len(sizeof (sbuf), sbuf, fmt, adx);
	printf_internal(prt_where, prt_type, "%s: %s\n", extra_string, sbuf);
}

static void
softwritekmsg(struct softkmsg *smp)
{
	struct softkmsg smbuf;

	mutex_enter(&prf_lock);

	for (smp = skmsg; smp < &skmsg[NSKMSG]; smp++) {

		if (smp->filled) {
			smbuf = *smp;
			smp->filled = 0;
			mutex_exit(&prf_lock);

			writekmsg(smbuf.buf, smbuf.len, smbuf.vp,
			    smbuf.type, smbuf.where);

			mutex_enter(&prf_lock);
		}
	}
	mutex_exit(&prf_lock);

}

static void
output_line(char *linep, int len, vnode_t *vp, int prt_type, int prt_where)
{
	struct softkmsg *smp;

	/*
	 * If called from a high-level interrupt handler, arrange for
	 * a softcall to print the message from a lower level where
	 * we can use adaptive mutex locks.  If we're panicing, though,
	 * go ahead, since panic makes us single-threaded.
	 */

	if (CPU->cpu_on_intr && !panicstr) {

		mutex_enter(&prf_lock);
		/*
		 * find an empty slot.  if none, keep stuffing last slot.
		 */
		smp = skmsg;
		while (smp->filled && smp < &skmsg[NSKMSG - 1])
			smp++;

		bcopy(linep, smp->buf, len);
		smp->vp = vp;
		smp->type = prt_type;
		smp->where = prt_where;
		smp->len = len;
		smp->filled = 1;

		mutex_exit(&prf_lock);

		softcall((void (*)(caddr_t))softwritekmsg, NULL);
	} else
		writekmsg(linep, len, vp, prt_type, prt_where);

}

/*
 * printc -- print a char and return the current ptr to the buffer
 * Use PRINTC for better readability. This fits in one line.
 */
#define	PRINTC(c)		\
		printc((c), lbp, linebuf, vp, prt_where, prt_type, sbuf_len)

static char *
printc(
	char c,
	char *lbp,
	char *linebuf,
	vnode_t *vp,		/* needed for output_line */
	int prt_where,
	int prt_type, 		/* needed for output_line */
	int sbuf_len)		/* length of the sprintf buffer */
{
	int msgsize = lbp - linebuf + 1;

	*lbp++ = c;

	if (prt_where & PRW_STRING) {
		if (msgsize == sbuf_len) /* overflow: keep stuffing last char */
			lbp--;
		return (lbp);
	}

	if (msgsize == LBUFSZ || c == '\0') {
		if (c == '\0') {
			if (msgsize == 1)
				return (linebuf);
			msgsize--;
		}
		output_line(linebuf, msgsize, vp, prt_type, prt_where);
		lbp = linebuf;
	}

	return (lbp);
}

/*
 * prt_where directs the output according to:
 *	PRW_STRING -- output to sprintf_buf
 * 		the caller is responsible for providing a large enough buffer
 *	PRW_BUF -- output to system buffer
 *	PRW_CONS -- output to console
 *
 * If PRW_STRING is on, PRW_CONS is not allowed simultaneously.
 *
 * prt_type is ultimately needed for calling strlog
 */
static void
prf_internal(
	register const char *fmt,
	register va_list adx,
	vnode_t *vp,
	int prt_where,
	int prt_type,
	char *sprintf_buf,
	int sbuf_len)
{
	register int b, c, i;
	u_longlong_t ul;
	longlong_t l;
	register char *s;
	int any;
	char consbuf[LBUFSZ+1];	/* extra char to NULL terminate for strlog() */
	char *linebuf;		/* ptr to beginning of output buf */
	register char *lbp;	/* current buffer pointer */
	int pad;
	int width;
	int ells;

	/*
	 * If we are not printing to the console only, then or in
	 * the SL_CONSOLE flag so that strlog is called in writekmsg.
	 */
	if (prt_where != PRW_CONS)
		prt_type |= SL_CONSOLE;

	if (prt_where & PRW_STRING) {
		/*
		 * Make sure that PRW_CONS is off, both cannot work at the
		 * same time. Built in protection.
		 */
		prt_where &= ~PRW_CONS;
		lbp = sprintf_buf;
		linebuf = sprintf_buf;
	} else {
		lbp = consbuf;
		linebuf = consbuf;
	}

loop:
	while ((c = *fmt++) != '%') {
		lbp = PRINTC(c);
		if (c == '\0')
			return;
	}

	c = *fmt++;
	for (pad = ' '; c == '0'; c = *fmt++) {
		pad = '0';
	}

	for (width = 0; c >= '0' && c <= '9'; c = *fmt++) {
		width = width * 10 + c - '0';
	}

	for (ells = 0; c == 'l'; c = *fmt++) {
		ells++;
	}

	switch (c) {
	case 'd': case 'D':
		b = 10;
		l = (ells <= 1) ? (longlong_t)va_arg(adx, long)
				: va_arg(adx, longlong_t);
		if (l < 0) {
			lbp = PRINTC('-');
			width--;
			ul = -l;
		} else {
			ul = l;
		}
		goto number;

	case 'x': case 'X':
		b = 16;
		goto u_number;

	case 'u':
		b = 10;
		goto u_number;

	case 'o': case 'O':
		b = 8;
u_number:
		ul = (ells <= 1) ? (u_longlong_t)va_arg(adx, u_long)
				: va_arg(adx, u_longlong_t);
number:
		lbp = printn((u_longlong_t)ul, b, width, pad,
		    lbp, linebuf, vp, prt_where, prt_type, sbuf_len);
		break;

	case 'c':
		b = va_arg(adx, int);
		for (i = 24; i >= 0; i -= 8)
			if ((c = ((b >> i) & 0x7f)) != 0) {
				if (c == '\n')
					lbp = PRINTC('\r');
				lbp = PRINTC(c);
			}
		break;

	case 'b':
		b = va_arg(adx, int);
		s = va_arg(adx, char *);
		lbp = printn((u_longlong_t)(unsigned)b, *s++, width, pad,
		    lbp, linebuf, vp, prt_where, prt_type, sbuf_len);
		any = 0;
		if (b) {
			while ((i = *s++) != 0) {
				if (b & (1 << (i-1))) {
					lbp = PRINTC(any? ',' : '<');
					any = 1;
					for (; (c = *s) > 32; s++)
						lbp = PRINTC(c);
				} else
					for (; *s > 32; s++)
						;
			}
			if (any)
				lbp = PRINTC('>');
		}
		break;

	case 's':
		s = va_arg(adx, char *);
		if (!s) {
			/* null string, be polite about it */
			s = "<null string>";
		}
		while ((c = *s++) != 0) {
			if (c == '\n')
				lbp = PRINTC('\r');
			lbp = PRINTC(c);
		}
		break;

	case '%':
		lbp = PRINTC('%');
		break;
	}
	goto loop;
}

void
prf(register char *fmt, register va_list adx, vnode_t *vp, int prt_where)
{
	prf_internal(fmt, adx, vp, prt_where, 0, (char *)NULL, 0);
}

/*
 * Printn prints a number n in base b.
 * We don't use recursion to avoid deep kernel stacks.
 */
static char *
printn(
	u_longlong_t n,
	int b,
	int width,
	int pad,
	char *lbp,
	char *linebuf,
	vnode_t *vp,
	int prt_where,
	int prt_type,
	int sbuf_len)
{
	char prbuf[22];	/* sufficient for a 64 bit octal value */
	char *cp;

	cp = prbuf;
	do {
		*cp++ = "0123456789abcdef"[n%b];
		n /= b;
		width--;
	} while (n);
	while (width-- > 0)
		*cp++ = pad;
	do {
		lbp = PRINTC(*--cp);
	} while (cp > prbuf);
	return (lbp);
}

/*
 * Called by the ASSERT macro in debug.h when an assertion fails.
 */
int
assfail(
	register char *a,
	register char *f,
	int l)
{
#ifndef LOCKNEST

	/*
	 * Quit sending messages to syslogd (i.e. logging processes).
	 */
	conslogging = 0;

#ifdef DEBUG
	if (aask)  {
		cmn_err(CE_NOTE, "ASSERTION CAUGHT: %s, file: %s, line: %d",
		a, f, l);
		debug_enter((char *)NULL);
	}
	if (aok)
		return (0);
#endif
	if (panicstr) {
		if (!aignore) {
			cmn_err(CE_WARN,
			    "assertion failed: %s, file: %s, line: %d",
			    a, f, l);
		}
		return (0);
	}

	cmn_err(CE_PANIC, "assertion failed: %s, file: %s, line: %d", a, f, l);
	return (0);
#endif	/* LOCKNEST */
}

/*
 * Write a block of characters either to the real console or, if console output
 * is redirected elsewhere (pseudo-tty), to wherever else it is redirected.
 * We don't want to do this one character at a time because we don't want
 * to chew up all the stream buffers with one-character writes.
 *
 * XXX - What we *want* is a streams pipe or something like that; some program
 * would run in the console window and read from the streams pipe.  *All*
 * "frame buffer" character output would get sent to that streams pipe instead.
 */
static void
writekmsg(
	register char *msgp,
	int count,
	register vnode_t *vp,
	int prt_type,
	int prt_where
)
{
	register int c;
	register int prton_frame_buffer = 1; /* send output to frame buffer */
	struct stdata *stp;
	vnode_t *redirecteevp = NULL;
	extern int in_modprintf;
	int wait = 0;
	int driver_mutex = 0;


	if (prt_where & PRW_BUF) {
		msgp[count] = '\0';

		if (conslogging)
			(void) strlog(0, 0, 0, prt_type, msgp, 0);
		else
			msgbuf_puts(msgp);

		if (prt_where == PRW_BUF)
			return;
	}

	/*
	 * If "vp" is non NULL, wait until the message can be sent to
	 * the stream associated with it (uprintf case).
	 */
	if (vp != NULL)
		wait = 1;

	/*
	 * If the "unsafe_driver" mutex was held by the thread
	 * on entry, release it so that this message can be sent
	 * downstream regardless of whether there are any unsafe
	 * stream modules or drivers.  I must drop it before
	 * acquiring the iwscons lock to prevent a deadlock with
	 * iwscn_ioctl.
	 */
	if (UNSAFE_DRIVER_LOCK_HELD()) {
		driver_mutex = 1;
		mutex_exit(&unsafe_driver);
	}

	/*
	 * If vp == NULL and this isn't a "panic" message, the output
	 * is automatically directed to the current console window.
	 * Also, if we are printing from within the module loading code
	 * then don't use the re-directed console as we may recursively
	 * try to load the iwscn driver and get a watchdog reset. In
	 * addition, if we are in the iwscn driver (specifically the
	 * iwscnioctl routine) and we call into the module code to
	 * load a module and we want to do a printf, the iwscn_lock will
	 * be held by the iwscn_ioctl code so in srredirecteevp_lookup we
	 * do a rw_tryenter rather than a rw_enter so that we will
	 * get back a NULL vp if we are already locked.
	 */
	if ((vp == NULL) && (panicstr == NULL) && in_modprintf == 0) {
		/*
		 * If the "hardware" console is identical to the indirect
		 * "workstation" console, obtain the vnode associated
		 * with the topmost entry on the redirection stack.
		 *
		 * "rconsvp" and "wsconsvp" will both be NULL if output
		 * is generated before the console is configured and hence
		 * it will be directed to the frame buffer.
		 */
		if ((cn_conf == 0) && (rconsvp != NULL) &&
		    (rconsvp == wsconsvp)) {

			/*
			 * NOTE:  This lookup operation also acquires
			 * the "readers" lock on the workstation console
			 * "redirection" driver, if a "vp" is returned.
			 *
			 * This is necessary to ensure that the redirection
			 * stack doesn't change (i.e., the current console
			 * window is not changed by either a "push" or "pop").
			 *
			 * The lock must be released only after the message
			 * is put on the associated stream.
			 */
			vp = redirecteevp = srredirecteevp_lookup();
		} else {
			/*
			 * XXX - "rconsvp" will be NULL if output is
			 * being generated as a result of a halt, reboot
			 * or a panic.
			 */
			if ((rconsvp != NULL) && (rconsvp->v_stream != NULL) &&
			    (cn_conf == 0))
				vp = rconsvp;
			else
				vp = NULL;
		}
	}

	if (vp != NULL && (stp = vp->v_stream)) {
		/*
		 * Check if the stream is full due to flow control
		 * before sending messages down stream.
		 */

		/* XXX: These should be declared in some header file */
		extern int strcheckoutq(struct stdata *, int);
		extern int stroutput(struct stdata *, char *, int, int);

		if (strcheckoutq(stp, wait) &&
		    stroutput(stp, msgp, count, wait))
			prton_frame_buffer = 0;
	}

	/*
	 * Now, release the lock on the redirection driver.
	 */
	if (redirecteevp != NULL)
		srredirecteevp_rele();
	/*
	 * Now, re-acquire the "unsafe_driver" mutex if we had
	 * it on entry.
	 */
	if (driver_mutex)
		mutex_enter(&unsafe_driver);


	/*
	 * The output is sent to the frame buffer for all panic
	 * messages or as a result of flow control problems on the
	 * stream associated with the console.
	 */
	if (prton_frame_buffer) {
		int device_in_use = 0;
		struct snode *csp;

		if (ncpus > 1) {
			/*
			 * Here's the nasty stuff used to avoid another cpu
			 * touching the framebuffer hardware at the same time
			 * as the kernel is asking the PROM to render
			 * characters.
			 */
			if (fbvp) {
				csp = VTOS(VTOS(fbvp)->s_commonvp);
				/*
				 * Holding this mutex prevents other cpus
				 * from adding mappings while we're in the
				 * PROM on this cpu.
				 */
				mutex_enter(&csp->s_lock);
				device_in_use = csp->s_mapcnt;
			}

			/*
			 * The framebuffer_lock is only required when running
			 * on machine with more than one CPU.
			 *
			 * XXX	How does it help?  The PROM provides
			 * it's own mutex.
			 */
			mutex_enter(&framebuffer_lock);
		}
		while (count > 0) {
			count--;
			if ((c = *msgp++) != '\0') {
				/* don't print NULs */
				cnputc(c, device_in_use);
			}
		}
		if (ncpus > 1) {
			mutex_exit(&framebuffer_lock);

			if (fbvp)
				mutex_exit(&csp->s_lock);
		}
	}
}


#include <sys/kmem.h>
#define	MSGBUF_SAVE_MAX 10

typedef struct mlist {
	struct mlist	*next;
	char		*buf;
} msglist_t;

static msglist_t	*msglist;
static msglist_t	**msglist_lastp = &msglist;
static int		msgbufs_saved;
static kmutex_t		msgbuf_lock;

void
msgbuf_init(void)
{
	mutex_init(&msgbuf_lock, "msgbuf lock", MUTEX_DEFAULT, NULL);

	if (msgbuf.msg_magic != MSG_MAGIC || msgbuf.msg_size != MSG_BSIZE) {

		msgbuf.msg_magic = MSG_MAGIC;
		msgbuf.msg_size = MSG_BSIZE;
		msgbuf.msg_bufx = 0;
		msgbuf.msg_bufr = 0;

		bzero(msgbuf.msg_bufc, MSG_BSIZE);
	}
}

static void
msgbuf_save(void)
{
	msglist_t		*new;
	int			size;

	if (msgbufs_saved == MSGBUF_SAVE_MAX || !kmem_ready)
		return;

	if (!(new = (msglist_t *)kmem_zalloc(sizeof (msglist_t), KM_NOSLEEP)))
		return;
	if (!(new->buf = (char *)kmem_zalloc(MSG_BSIZE, KM_NOSLEEP))) {
		kmem_free(new, sizeof (msglist_t));
		return;
	}

	*msglist_lastp = new;
	msglist_lastp = &new->next;

	size = msgbuf.msg_size - msgbuf.msg_bufr;
	bcopy(&msgbuf.msg_bufc[msgbuf.msg_bufr], new->buf, size);
	bcopy(msgbuf.msg_bufc, new->buf + size, msgbuf.msg_bufx);

	msgbufs_saved++;
}

static void
msgcp(caddr_t from, caddr_t *to, size_t size, size_t *space)
{
	size_t copysize = MIN(size, *space);

	bcopy(from, *to, copysize);
	*to += copysize;
	*space -= copysize;
}

caddr_t
msgbuf_get(caddr_t buf, size_t bufsize)
{
	msglist_t	*listp;
	msglist_t	*msglist_next = NULL;

	mutex_enter(&msgbuf_lock);

	for (listp = msglist; listp; listp = msglist_next) {
		msgcp(listp->buf, &buf, MSG_BSIZE, &bufsize);
		msglist_next = listp->next;
		kmem_free(listp->buf, MSG_BSIZE);
		kmem_free(listp, sizeof (msglist_t));
	}
	msglist = NULL;
	msglist_lastp = &msglist;
	msgbufs_saved = 0;

	if (msgbuf.msg_bufx < msgbuf.msg_bufr) {

		msgcp(&msgbuf.msg_bufc[msgbuf.msg_bufr], &buf,
		    msgbuf.msg_size - msgbuf.msg_bufr, &bufsize);

		msgcp(msgbuf.msg_bufc, &buf, msgbuf.msg_bufx, &bufsize);

	} else {
		msgcp(&msgbuf.msg_bufc[msgbuf.msg_bufr], &buf,
		    msgbuf.msg_bufx - msgbuf.msg_bufr, &bufsize);
	}

	mutex_exit(&msgbuf_lock);

	return (buf);
}

void
msgbuf_clear(void)
{
	mutex_enter(&msgbuf_lock);

	msgbuf.msg_bufr = msgbuf.msg_bufx;

	mutex_exit(&msgbuf_lock);
}

void
msgbuf_puts(caddr_t str)
{
	static init_done;

	if (!msgbufinit)
		return;

	if (!init_done) {
		msgbuf_init();
		init_done = 1;
	}

	mutex_enter(&msgbuf_lock);

	while (*str) {

		msgbuf.msg_bufc[msgbuf.msg_bufx++] = *str++;

		if (msgbuf.msg_bufx == msgbuf.msg_size)
			msgbuf.msg_bufx = 0;

		if (msgbuf.msg_bufx == msgbuf.msg_bufr)
			msgbuf_save();
	}

	mutex_exit(&msgbuf_lock);
}

size_t
msgbuf_size(void)
{
	return (MSG_BSIZE * (msgbufs_saved + 1));
}
