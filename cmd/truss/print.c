 /*
  * Copyright (c) 1992 Sun Microsystems, Inc.  All Rights Reserved.
  */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident "@(#)print.c	1.17	94/06/22 SMI"	/* SVr4.0 1.9	*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <termio.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/ulimit.h>
#include <sys/utsname.h>
#include <sys/vtrace.h>
#include <sys/kstat.h>
#include <sys/modctl.h>
#include <sys/acl.h>
#include <stropts.h>

#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/isa_defs.h>
#include <sys/systeminfo.h>
#ifdef SYS_kaio
#include <sys/aio.h>
#endif
#include <bsm/audit.h>
#include "pcontrol.h"
#include "print.h"
#include "ramdata.h"
#include "proto.h"

/*
 * Function prototypes for static routines in this module.
 */
#if	defined(__STDC__)

static	void	prt_nov( int , int );
static	void	prt_dec( int , int );
static	void	prt_oct( int , int );
static	void	prt_hex( int , int );
static	void	prt_hhx( int , int );
static	void	prt_dex( int , int );
static	void	prt_stg( int , int );
static	void	prt_rst( int , int );
static	void	prt_rlk( int , int );
static	void	prt_ioc( int , int );
static	void	prt_ioa( int , int );
static	void	prt_fcn( int , int );

#if defined(i386)
static	void	prt_s86( int , int );
#else /* !i386 */
static	void	prt_s3b( int , int );
#endif /* !i386 */

static	void	prt_uts( int , int );
static	void	prt_msc( int , int );
static	void	prt_msf( int , int );
static	void	prt_smc( int , int );
static	void	prt_sef( int , int );
static	void	prt_shc( int , int );
static	void	prt_shf( int , int );
static	void	prt_sfs( int , int );
static	void	prt_opn( int , int );
static	void	prt_sig( int , int );
static	void	prt_six( int , int );
static	void	prt_act( int , int );
static	void	prt_smf( int , int );
static	void	prt_plk( int , int );
static	void	prt_mtf( int , int );
static	void	prt_mft( int , int );
static	void	prt_iob( int , int );
static	void	prt_wop( int , int );
static	void	prt_spm( int , int );
static	void	prt_mpr( int , int );
static	void	prt_mty( int , int );
static	void	prt_mcf( int , int );
static	void	prt_mc4( int , int );
static	void	prt_mc5( int , int );
static	void	prt_mad( int , int );
static	void	prt_ulm( int , int );
static	void	prt_rlm( int , int );
static	void	prt_cnf( int , int );
static	void	prt_inf( int , int );
static	void	prt_ptc( int , int );
static	void	prt_fui( int , int );
static	void	prt_idt( int , int );
static	void	prt_lwf( int , int );
static	void	prt_itm( int , int );
static	void	prt_vtr( int , int );
static	void	prt_mod( int , int );
static	void	prt_whn( int , int );
static	void	prt_acl( int , int );
static	void	prt_aio( int , int );
static	void	prt_aud( int , int );
static	void	prt_llo( int , int, int );
static	void	grow( int );
static	CONST char *	mmap_protect( int );
static	CONST char *	mmap_type( int );

#else	/* defined(__STDC__) */

static	void	prt_nov();
static	void	prt_dec();
static	void	prt_oct();
static	void	prt_hex();
static	void	prt_hhx();
static	void	prt_dex();
static	void	prt_stg();
static	void	prt_rst();
static	void	prt_rlk();
static	void	prt_ioc();
static	void	prt_ioa();
static	void	prt_fcn();

#if defined(i386)
static	void	prt_s86();
#else /* !i386 */
static	void	prt_s3b();
#endif /* !i386 */

static	void	prt_uts();
static	void	prt_msc();
static	void	prt_msf();
static	void	prt_smc();
static	void	prt_sef();
static	void	prt_shc();
static	void	prt_shf();
static	void	prt_sfs();
static	void	prt_opn();
static	void	prt_sig();
static	void	prt_six();
static	void	prt_act();
static	void	prt_smf();
static	void	prt_plk();
static	void	prt_mtf();
static	void	prt_mft();
static	void	prt_iob();
static	void	prt_wop();
static	void	prt_spm();
static	void	prt_mpr();
static	void	prt_mty();
static	void	prt_mcf();
static	void	prt_mc4();
static	void	prt_mc5();
static	void	prt_mad();
static	void	prt_ulm();
static	void	prt_rlm();
static	void	prt_cnf();
static	void	prt_inf();
static	void	prt_ptc();
static	void	prt_fui();
static	void	prt_idt();
static	void	prt_lwf();
static	void	prt_itm();
static	void	prt_vtr();
static	void	prt_mod();
static	void	prt_whn();
static	void	prt_acl();
static	void	prt_aio();
static	void	prt_aud();
static	void	prt_llo();
static	void	grow();
static	CONST char *	mmap_protect();
static	CONST char *	mmap_type();

#endif	/* defined(__STDC__) */

#define GROW(nb) if (sys_leng+(nb) >= sys_ssize) grow(nb)

/*ARGSUSED*/
static void
prt_nov(raw, val)	/* print nothing */
int raw;
int val;
{
}

/*ARGSUSED*/
static void
prt_dec(raw, val)	/* print as decimal */
int raw;
int val;
{
	GROW(12);
	sys_leng += sprintf(sys_string+sys_leng, "%d", val);
}

/*ARGSUSED*/
static void
prt_oct(raw, val)	/* print as octal */
int raw;
int val;
{
	GROW(12);
	sys_leng += sprintf(sys_string+sys_leng, "%#o", val);
}

/*ARGSUSED*/
static void
prt_hex(raw, val)	/* print as hexadecimal */
int raw;
int val;
{
	GROW(10);
	sys_leng += sprintf(sys_string+sys_leng, "0x%.8X", val);
}

/*ARGSUSED*/
static void
prt_hhx(raw, val)	/* print as hexadecimal (half size) */
int raw;
int val;
{
	GROW(10);
	sys_leng += sprintf(sys_string+sys_leng, "0x%.4X", val);
}

/*ARGSUSED*/
static void
prt_dex(raw, val)	/* print as decimal if small, else hexadecimal */
int raw;
int val;
{
	if (val & 0xff000000)
		prt_hex(0, val);
	else
		prt_dec(0, val);
}

/*ARGSUSED*/
static void
prt_llo(raw, val1, val2)	/* print long long offset */
int raw;
int val1;
int val2;
{
	int hival;
	int loval;

#ifdef	_LONG_LONG_LTOH
	hival = val2;
	loval = val1;
#else
	hival = val1;
	loval = val2;
#endif

	if (hival == 0)
		prt_dex(0, loval);
	else {
		GROW(18);
		sys_leng += sprintf(sys_string+sys_leng, "0x%.8X%.8X",
			hival, loval);
	}
}

static void
prt_stg(raw, val)	/* print as string */
int raw;
int val;
{
	register char * s = raw? NULL : fetchstring((long)val, 400);

	if (s == NULL)
		prt_hex(0, val);
	else {
		GROW((int)strlen(s)+2);
		sys_leng += sprintf(sys_string+sys_leng, "\"%s\"", s);
	}
}

static void
prt_rst(raw, val)	/* print as string returned from syscall */
int raw;
int val;
{
	register char * s = (raw || Errno || slowmode)? NULL :
				fetchstring((long)val, 400);

	if (s == NULL)
		prt_hex(0, val);
	else {
		GROW((int)strlen(s)+2);
		sys_leng += sprintf(sys_string+sys_leng, "\"%s\"", s);
	}
}

static void
prt_rlk(raw, val)	/* print contents of readlink() buffer */
int raw;
int val;		/* address of buffer */
{
	register char * s = (raw || Errno || slowmode || Rval1 <= 0)? NULL :
				fetchstring((long)val, (Rval1>400)?400:Rval1);

	if (s == NULL)
		prt_hex(0, val);
	else {
		GROW((int)strlen(s)+2);
		sys_leng += sprintf(sys_string+sys_leng, "\"%s\"", s);
	}
}

static void
prt_ioc(raw, val)	/* print ioctl code */
int raw;
int val;
{
	register CONST char * s = raw? NULL : ioctlname(val);

	if (s == NULL)
		prt_hex(0, val);
	else
		outstring(s);
}

static void
prt_ioa(raw, val)	/* print ioctl argument */
int raw;
int val;
{
	register CONST char * s;

	switch(sys_args[1]) {	/* cheating -- look at the ioctl() code */

	/* kstat ioctl()s */
	case KSTAT_IOC_READ:
	case KSTAT_IOC_WRITE:
		prt_stg(raw, (int) (&((kstat_t *) val)->ks_name));
		break;

	/* streams ioctl()s */
	case I_LOOK:
		prt_rst(raw, val);
		break;
	case I_PUSH:
	case I_FIND:
		prt_stg(raw, val);
		break;
	case I_LINK:
	case I_UNLINK:
	case I_SENDFD:
		prt_dec(0, val);
		break;
	case I_SRDOPT:
		if (raw || (s = strrdopt(val)) == NULL)
			prt_dec(0, val);
		else
			outstring(s);
		break;
	case I_SETSIG:
		if (raw || (s = strevents(val)) == NULL)
			prt_hex(0, val);
		else
			outstring(s);
		break;
	case I_FLUSH:
		if (raw || (s = strflush(val)) == NULL)
			prt_dec(0, val);
		else
			outstring(s);
		break;

	/* tty ioctl()s */
	case TCSBRK:
	case TCXONC:
	case TCFLSH:
	case TCDSET:
		prt_dec(0, val);
		break;

	default:
		prt_hex(0, val);
		break;
	}
}

static void
prt_fcn(raw, val)	/* print fcntl code */
int raw;
int val;
{
	register CONST char * s = raw? NULL : fcntlname(val);

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
#if defined(i386)
prt_s86(raw, val)	/* print sysi86 code */
#else /* !i386 */
prt_s3b(raw, val)	/* print sys3b code */
#endif /* !i386 */
int raw;
int val;
{

#if defined(i386)
	register CONST char * s = raw? NULL : si86name(val);
#else /* !i386 */
	register CONST char * s = raw? NULL : s3bname(val);
#endif /* !i386 */

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_uts(raw, val)	/* print utssys code */
int raw;
int val;
{
	register CONST char * s = raw? NULL : utscode(val);

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_msc(raw, val)	/* print msgsys command */
int raw;
int val;
{
	register CONST char * s = raw? NULL : msgcmd(val);

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_msf(raw, val)	/* print msgsys flags */
int raw;
int val;
{
	register CONST char * s = raw? NULL : msgflags(val);

	if (s == NULL)
		prt_oct(0, val);
	else
		outstring(s);
}

static void
prt_smc(raw, val)	/* print semsys command */
int raw;
int val;
{
	register CONST char * s = raw? NULL : semcmd(val);

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_sef(raw, val)	/* print semsys flags */
int raw;
int val;
{
	register CONST char * s = raw? NULL : semflags(val);

	if (s == NULL)
		prt_oct(0, val);
	else
		outstring(s);
}

static void
prt_shc(raw, val)	/* print shmsys command */
int raw;
int val;
{
	register CONST char * s = raw? NULL : shmcmd(val);

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_shf(raw, val)	/* print shmsys flags */
int raw;
int val;
{
	register CONST char * s = raw? NULL : shmflags(val);

	if (s == NULL)
		prt_oct(0, val);
	else
		outstring(s);
}

static void
prt_sfs(raw, val)	/* print sysfs code */
int raw;
int val;
{
	register CONST char * s = raw? NULL : sfsname(val);

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_opn(raw, val)	/* print open code */
int raw;
int val;
{
	register CONST char * s = raw? NULL : openarg(val);

	if (s == NULL)
		prt_oct(0, val);
	else
		outstring(s);
}

static void
prt_sig(raw, val)	/* print signal name plus flags */
int raw;
int val;
{
	register CONST char * s = raw? NULL : sigarg(val);

	if (s == NULL)
		prt_hex(0, val);
	else
		outstring(s);
}

static void
prt_six(raw, val)	/* print signal name, masked with SIGNO_MASK */
int raw;
int val;
{
	register CONST char * s = raw? NULL : sigarg(val & SIGNO_MASK);

	if (s == NULL)
		prt_hex(0, val);
	else
		outstring(s);
}

static void
prt_act(raw, val)	/* print signal action value */
int raw;
int val;
{
	register CONST char * s;

	if (raw)
		s = NULL;
	else if (val == (int)SIG_DFL)
		s = "SIG_DFL";
	else if (val == (int)SIG_IGN)
		s = "SIG_IGN";
	else if (val == (int)SIG_HOLD)
		s = "SIG_HOLD";
	else
		s = NULL;

	if (s == NULL)
		prt_hex(0, val);
	else
		outstring(s);
}

static void
prt_smf(raw, val)	/* print streams message flags */
int raw;
int val;
{
	switch (val) {
	case 0:
		prt_dec(0, val);
		break;
	case RS_HIPRI:
		if (raw)
			prt_hhx(0, val);
		else
			outstring("RS_HIPRI");
		break;
	default:
		prt_hhx(0, val);
		break;
	}
}

static void
prt_plk(raw, val)	/* print plock code */
int raw;
int val;
{
	register CONST char * s = raw? NULL : plockname(val);

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_mtf(raw, val)	/* print mount flags */
int raw;
int val;
{
	register CONST char * s = raw? NULL : mountflags(val);

	if (s == NULL)
		prt_hex(0, val);
	else
		outstring(s);
}

static void
prt_mft(raw, val)	/* print mount file system type */
int raw;
int val;
{
	if (val >= 0 && val < 256)
		prt_dec(0, val);
	else if (raw)
		prt_hex(0, val);
	else
		prt_stg(raw, val);
}

static void
prt_iob(raw, val)	/* print contents of read() or write() I/O buffer */
register int val;	/* address of I/O buffer (sys_args[1]) */
int raw;
{
	register process_t *Pr = &Proc;
	register int fdp1 = sys_args[0]+1;
	register int nbyte =
	    (Pr->why.pr_what == SYS_write || Pr->why.pr_what == SYS_pwrite) ?
		sys_args[2] :
		((Errno || slowmode)? 0 : Rval1);
	register int elsewhere = FALSE;		/* TRUE iff dumped elsewhere */
	char buffer[IOBSIZE];

	iob_buf[0] = '\0';

	if (Pr->why.pr_why == PR_SYSEXIT && nbyte > IOBSIZE) {
		switch (Pr->why.pr_what) {
		case SYS_read:
		case SYS_pread:
			elsewhere = prismember(&readfd, fdp1);
			break;
		case SYS_write:
		case SYS_pwrite:
			elsewhere = prismember(&writefd, fdp1);
			break;
		}
	}

	if (nbyte <= 0 || elsewhere)
		prt_hex(0, val);
	else {
		register int nb = nbyte>IOBSIZE? IOBSIZE : nbyte;

		if (Pread(Pr, (long)val, buffer, nb) != nb)
			prt_hex(0, val);
		else {
			iob_buf[0] = '"';
			showbytes(buffer, nb, iob_buf+1);
			(void) strcat(iob_buf,
				(nb == nbyte)?
				    (CONST char *)"\"" : (CONST char *)"\"..");
			if (raw)
				prt_hex(0, val);
			else
				outstring(iob_buf);
		}
	}
}

static void
prt_idt(raw, val)	/* print idtype_t, waitid() argument */
int raw;
int val;
{
	register CONST char * s = raw? NULL : idtype_enum(val);

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_wop(raw, val)	/* print waitid() options */
int raw;
int val;
{
	register CONST char * s = raw? NULL : woptions(val);

	if (s == NULL)
		prt_oct(0, val);
	else
		outstring(s);
}

static void
prt_whn(raw, val)	/* print lseek() whence argument */
int raw;
int val;
{
	register CONST char * s = raw? NULL : whencearg(val);

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

/*ARGSUSED*/
static void
prt_spm(raw, val)	/* print sigprocmask argument */
int raw;
int val;
{
	register CONST char * s = NULL;

	if (!raw) {
		switch (val) {
		case SIG_BLOCK:		s = "SIG_BLOCK";	break;
		case SIG_UNBLOCK:	s = "SIG_UNBLOCK";	break;
		case SIG_SETMASK:	s = "SIG_SETMASK";	break;
		}
	}

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static CONST char *
mmap_protect(arg)
register int arg;
{
	register char * str = code_buf;

	if (arg & ~(PROT_READ|PROT_WRITE|PROT_EXEC))
		return (char *)NULL;
	
	if (arg == PROT_NONE)
		return "PROT_NONE";

	*str = '\0';
	if (arg & PROT_READ)
		(void) strcat(str, "|PROT_READ");
	if (arg & PROT_WRITE)
		(void) strcat(str, "|PROT_WRITE");
	if (arg & PROT_EXEC)
		(void) strcat(str, "|PROT_EXEC");
	return (CONST char *)(str+1);
}

static CONST char *
mmap_type(arg)
register int arg;
{
	register char * str = code_buf;

	switch (arg&MAP_TYPE) {
	case MAP_SHARED:
		(void) strcpy(str, "MAP_SHARED");
		break;
	case MAP_PRIVATE:
		(void) strcpy(str, "MAP_PRIVATE");
		break;
	default:
		(void) sprintf(str, "%d", arg&MAP_TYPE);
		break;
	}

	arg &= ~(_MAP_NEW|MAP_TYPE);

	if (arg & ~(MAP_FIXED|MAP_RENAME|MAP_NORESERVE))
		(void) sprintf(str+strlen(str), "|0x%X", arg);
	else {
		if (arg & MAP_FIXED)
			(void) strcat(str, "|MAP_FIXED");
		if (arg & MAP_RENAME)
			(void) strcat(str, "|MAP_RENAME");
		if (arg & MAP_NORESERVE)
			(void) strcat(str, "|MAP_NORESERVE");
	}

	return (CONST char *)str;
}

static void
prt_mpr(raw, val)	/* print mmap()/mprotect() flags */
int raw;
int val;
{
	register CONST char * s = raw? NULL : mmap_protect(val);

	if (s == NULL)
		prt_hhx(0, val);
	else
		outstring(s);
}

static void
prt_mty(raw, val)	/* print mmap() mapping type flags */
int raw;
int val;
{
	register CONST char * s = raw? NULL : mmap_type(val);

	if (s == NULL)
		prt_hhx(0, val);
	else
		outstring(s);
}

/*ARGSUSED*/
static void
prt_mcf(raw, val)	/* print memcntl() function */
int raw;
int val;
{
	register CONST char * s = NULL;

	if (!raw) {
		switch (val) {
		case MC_SYNC:		s = "MC_SYNC";		break;
		case MC_LOCK:		s = "MC_LOCK";		break;
		case MC_UNLOCK:		s = "MC_UNLOCK";	break;
		case MC_ADVISE:		s = "MC_ADVISE";	break;
		case MC_LOCKAS:		s = "MC_LOCKAS";	break;
		case MC_UNLOCKAS:	s = "MC_UNLOCKAS";	break;
		}
	}

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_mc4(raw, val)	/* print memcntl() (fourth) argument */
int raw;
int val;
{
	if (val == 0)
		prt_dec(0, val);
	else if (raw)
		prt_hhx(0, val);
	else {
		register char * s = NULL;

		switch (sys_args[2]) { /* cheating -- look at memcntl func */
		case MC_SYNC:
			if ((val & ~(MS_ASYNC|MS_INVALIDATE)) == 0) {
				*(s = code_buf) = '\0';
				if (val & MS_ASYNC)
					(void) strcat(s, "|MS_ASYNC");
				if (val & MS_INVALIDATE)
					(void) strcat(s, "|MS_INVALIDATE");
			}
			break;

		case MC_LOCKAS:
		case MC_UNLOCKAS:
			if ((val & ~(MCL_CURRENT|MCL_FUTURE)) == 0) {
				*(s = code_buf) = '\0';
				if (val & MCL_CURRENT)
					(void) strcat(s, "|MCL_CURRENT");
				if (val & MCL_FUTURE)
					(void) strcat(s, "|MCL_FUTURE");
			}
			break;
		}

		if (s == NULL)
			prt_hhx(0, val);
		else
			outstring(++s);
	}
}

static void
prt_mc5(raw, val)	/* print memcntl() (fifth) argument */
int raw;
int val;
{
	register char * s;

	if (val == 0)
		prt_dec(0, val);
	else if (raw || (val & ~VALID_ATTR))
		prt_hhx(0, val);
	else {
		s = code_buf;
		*s = '\0';
		if (val & SHARED)
			(void) strcat(s, "|SHARED");
		if (val & PRIVATE)
			(void) strcat(s, "|PRIVATE");
		if (val & PROT_READ)
			(void) strcat(s, "|PROT_READ");
		if (val & PROT_WRITE)
			(void) strcat(s, "|PROT_WRITE");
		if (val & PROT_EXEC)
			(void) strcat(s, "|PROT_EXEC");
		if (*s == '\0')
			prt_hhx(0, val);
		else
			outstring(++s);
	}
}

static void
prt_mad(raw, val)	/* print madvise() argument */
int raw;
int val;
{
	register CONST char * s = NULL;

	if (!raw) {
		switch (val) {
		case MADV_NORMAL:	s = "MADV_NORMAL";	break;
		case MADV_RANDOM:	s = "MADV_RANDOM";	break;
		case MADV_SEQUENTIAL:	s = "MADV_SEQUENTIAL";	break;
		case MADV_WILLNEED:	s = "MADV_WILLNEED";	break;
		case MADV_DONTNEED:	s = "MADV_DONTNEED";	break;
		}
	}

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_ulm(raw, val)	/* print ulimit() argument */
int raw;
int val;
{
	register CONST char * s = NULL;

	if (!raw) {
		switch (val) {
		case UL_GFILLIM:	s = "UL_GFILLIM";	break;
		case UL_SFILLIM:	s = "UL_SFILLIM";	break;
		case UL_GMEMLIM:	s = "UL_GMEMLIM";	break;
		case UL_GDESLIM:	s = "UL_GDESLIM";	break;
		}
	}

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_rlm(raw, val)	/* print get/setrlimit() argument */
int raw;
int val;
{
	register CONST char * s = NULL;

	if (!raw) {
		switch (val) {
		case RLIMIT_CPU:	s = "RLIMIT_CPU";	break;
		case RLIMIT_FSIZE:	s = "RLIMIT_FSIZE";	break;
		case RLIMIT_DATA:	s = "RLIMIT_DATA";	break;
		case RLIMIT_STACK:	s = "RLIMIT_STACK";	break;
		case RLIMIT_CORE:	s = "RLIMIT_CORE";	break;
		case RLIMIT_NOFILE:	s = "RLIMIT_NOFILE";	break;
		case RLIMIT_VMEM:	s = "RLIMIT_VMEM";	break;
		}
	}

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_cnf(raw, val)	/* print sysconfig code */
int raw;
int val;
{
	register CONST char * s = raw? NULL : sconfname(val);

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_inf(raw, val)	/* print systeminfo code */
int raw;
int val;
{
	register CONST char * s = NULL;

	if (!raw) {
		switch (val) {
		case SI_SYSNAME:	s = "SI_SYSNAME";	break;
		case SI_HOSTNAME:	s = "SI_HOSTNAME";	break;
		case SI_RELEASE:	s = "SI_RELEASE";	break;
		case SI_VERSION:	s = "SI_VERSION";	break;
		case SI_MACHINE:	s = "SI_MACHINE";	break;
		case SI_ARCHITECTURE:	s = "SI_ARCHITECTURE";	break;
		case SI_HW_SERIAL:	s = "SI_HW_SERIAL";	break;
		case SI_HW_PROVIDER:	s = "SI_HW_PROVIDER";	break;
		case SI_SRPC_DOMAIN:	s = "SI_SRPC_DOMAIN";	break;
		case SI_SET_HOSTNAME:	s = "SI_SET_HOSTNAME";	break;
		case SI_SET_SRPC_DOMAIN: s = "SI_SET_SRPC_DOMAIN"; break;
		case SI_PLATFORM:	s = "SI_PLATFORM";	break;
		}
	}

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_ptc(raw, val)	/* print pathconf code */
int raw;
int val;
{
	register CONST char * s = raw? NULL : pathconfname(val);

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_fui(raw, val)	/* print fusers() input argument */
int raw;
int val;
{
	register CONST char * s = raw? NULL : fuiname(val);

	if (s == NULL)
		prt_hhx(0, val);
	else
		outstring(s);
}

/* Until <lwp.h> is available */
#ifndef LWP_STOP
#	define	LWP_STOP	1
#	define	LWP_WAIT	8
#endif

static void
prt_lwf(raw, val)	/* print lwp_create() flags */
int raw;
int val;
{
	register char * s;

	if (val == 0)
		prt_dec(0, val);
	else if (raw || (val & ~(LWP_STOP|LWP_WAIT)))
		prt_hhx(0, val);
	else {
		s = code_buf;
		*s = '\0';
		if (val & LWP_STOP)
			(void) strcat(s, "|LWP_STOP");
		if (val & LWP_WAIT)
			(void) strcat(s, "|LWP_WAIT");
		outstring(++s);
	}
}

static void
prt_itm(raw, val)	/* print [get|set]itimer() arg */
int raw;
int val;
{
	register CONST char * s = NULL;

	if (!raw) {
		switch (val) {
		case ITIMER_REAL:	s = "ITIMER_REAL";	break;
		case ITIMER_VIRTUAL:	s = "ITIMER_VIRTUAL";	break;
		case ITIMER_PROF:	s = "ITIMER_PROF";	break;
#ifdef ITIMER_REALPROF
		case ITIMER_REALPROF:	s = "ITIMER_REALPROF";	break;
#endif
		}
	}

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_vtr(raw, val)	/* print vtrace() code */
int raw;
int val;
{
	register CONST char * s = NULL;

	if (!raw) {
		switch (val) {
		case VTR_INIT:		s = "VTR_INIT";		break;
		case VTR_FILE:		s = "VTR_FILE";		break;
		case VTR_EVENTMAP:	s = "VTR_EVENTMAP";	break;
		case VTR_EVENT:		s = "VTR_EVENT";	break;
		case VTR_START:		s = "VTR_START";	break;
		case VTR_PAUSE:		s = "VTR_PAUSE";	break;
		case VTR_RESUME:	s = "VTR_RESUME";	break;
		case VTR_INFO:		s = "VTR_INFO";		break;
		case VTR_FLUSH:		s = "VTR_FLUSH";	break;
		case VTR_RESET:		s = "VTR_RESET";	break;
		case VTR_TEST:		s = "VTR_TEST";		break;
		case VTR_PROCESS:	s = "VTR_PROCESS";	break;
		}
	}

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_mod(raw, val)	/* print modctl() code */
int raw;
int val;
{
	register CONST char * s = NULL;

	if (!raw) {
		switch (val) {
		case MODLOAD:		s = "MODLOAD";		break;
		case MODUNLOAD:		s = "MODUNLOAD";	break;
		case MODINFO:		s = "MODINFO";		break;
		case MODRESERVED:	s = "MODRESERVED";	break;
		case MODCONFIG:		s = "MODCONFIG";	break;
		case MODADDMAJBIND:	s = "MODADDMAJBIND";	break;
		case MODGETPATH:	s = "MODGETPATH";	break;
		case MODREADSYSBIND:	s = "MODREADSYSBIND";	break;
		case MODGETMAJBIND:	s = "MODGETMAJBIND";	break;
		case MODGETNAME:	s = "MODGETNAME";	break;
		}
	}

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_acl(raw, val)	/* print acl() code */
int raw;
int val;
{
	register CONST char * s = NULL;

	if (!raw) {
		switch (val) {
		case GETACL:		s = "GETACL";		break;
		case SETACL:		s = "SETACL";		break;
		case GETACLCNT:		s = "GETACLCNT";	break;
		}
	}

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_aio(raw, val)	/* print kaio() code */
int raw;
int val;
{
	register CONST char * s = NULL;
	char buf[32];

#ifdef SYS_kaio
	if (!raw) {
		switch (val & ~AIO_POLL_BIT) {
		case AIOREAD:		s = "AIOREAD";		break;
		case AIOWRITE:		s = "AIOWRITE";		break;
		case AIOWAIT:		s = "AIOWAIT";		break;
		case AIOCANCEL:		s = "AIOCANCEL";	break;
		case AIONOTIFY:		s = "AIONOTIFY";	break;
		}
		if (s != NULL && (val & AIO_POLL_BIT)) {
			(void) strcpy(buf, s);
			(void) strcat(buf, "|AIO_POLL_BIT");
			s = (CONST char *)buf;
		}
	}
#endif

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

static void
prt_aud(raw, val)	/* print auditsys() code */
int raw;
int val;
{
	register CONST char * s = NULL;

	if (!raw) {
		switch (val) {
		case BSM_GETAUID:	s = "BSM_GETAUID";	break;
		case BSM_SETAUID:	s = "BSM_SETAUID";	break;
		case BSM_GETAUDIT:	s = "BSM_GETAUDIT";	break;
		case BSM_SETAUDIT:	s = "BSM_SETAUDIT";	break;
		case BSM_GETUSERAUDIT:	s = "BSM_GETUSERAUDIT";	break;
		case BSM_SETUSERAUDIT:	s = "BSM_SETUSERAUDIT";	break;
		case BSM_AUDIT:		s = "BSM_AUDIT";	break;
		case BSM_AUDITUSER:	s = "BSM_AUDITUSER";	break;
		case BSM_AUDITSVC:	s = "BSM_AUDITSVC";	break;
		case BSM_AUDITON:	s = "BSM_AUDITON";	break;
		case BSM_AUDITCTL:	s = "BSM_AUDITCTL";	break;
		case BSM_GETKERNSTATE:	s = "BSM_GETKERNSTATE";	break;
		case BSM_SETKERNSTATE:	s = "BSM_SETKERNSTATE";	break;
		case BSM_GETPORTAUDIT:	s = "BSM_GETPORTAUDIT";	break;
		case BSM_REVOKE:	s = "BSM_REVOKE";	break;
		case BSM_AUDITSTAT:	s = "BSM_AUDITSTAT";	break;
		}
	}

	if (s == NULL)
		prt_dec(0, val);
	else
		outstring(s);
}

void
outstring(s)
register CONST char * s;
{
	register int len = strlen(s);

	GROW(len);
	(void) strcpy(sys_string+sys_leng, s);
	sys_leng += len;
}

static void
grow(nbyte)	/* reallocate format buffer if necessary */
register int nbyte;
{
	while (sys_leng+nbyte >= sys_ssize) {
		sys_string = realloc(sys_string, sys_ssize *= 2);
		if (sys_string == NULL)
			abend("cannot reallocate format buffer", 0);
	}
}


/* array of pointers to print functions, one for each format */

void (* CONST Print[])() = {
	prt_nov,	/* NOV -- no value */
	prt_dec,	/* DEC -- print value in decimal */
	prt_oct,	/* OCT -- print value in octal */
	prt_hex,	/* HEX -- print value in hexadecimal */
	prt_dex,	/* DEX -- print value in hexadecimal if big enough */
	prt_stg,	/* STG -- print value as string */
	prt_ioc,	/* IOC -- print ioctl code */
	prt_fcn,	/* FCN -- print fcntl code */
#if defined(i386)
	prt_s86,	/* S86 -- print sysi86 code */
#else /* !i386 */
	prt_s3b,	/* S3B -- print sys3b code */
#endif /* !i386 */
	prt_uts,	/* UTS -- print utssys code */
	prt_opn,	/* OPN -- print open code */
	prt_sig,	/* SIG -- print signal name plus flags */
	prt_act,	/* ACT -- print signal action value */
	prt_msc,	/* MSC -- print msgsys command */
	prt_msf,	/* MSF -- print msgsys flags */
	prt_smc,	/* SMC -- print semsys command */
	prt_sef,	/* SEF -- print semsys flags */
	prt_shc,	/* SHC -- print shmsys command */
	prt_shf,	/* SHF -- print shmsys flags */
	prt_plk,	/* PLK -- print plock code */
	prt_sfs,	/* SFS -- print sysfs code */
	prt_rst,	/* RST -- print string returned by syscall */
	prt_smf,	/* SMF -- print streams message flags */
	prt_ioa,	/* IOA -- print ioctl argument */
	prt_six,	/* SIX -- print signal, masked with SIGNO_MASK */
	prt_mtf,	/* MTF -- print mount flags */
	prt_mft,	/* MFT -- print mount file system type */
	prt_iob,	/* IOB -- print contents of I/O buffer */
	prt_hhx,	/* HHX -- print value in hexadecimal (half size) */
	prt_wop,	/* WOP -- print waitsys() options */
	prt_spm,	/* SPM -- print sigprocmask argument */
	prt_rlk,	/* RLK -- print readlink buffer */
	prt_mpr,	/* MPR -- print mmap()/mprotect() flags */
	prt_mty,	/* MTY -- print mmap() mapping type flags */
	prt_mcf,	/* MCF -- print memcntl() function */
	prt_mc4,	/* MC4 -- print memcntl() (fourth) argument */
	prt_mc5,	/* MC5 -- print memcntl() (fifth) argument */
	prt_mad,	/* MAD -- print madvise() argument */
	prt_ulm,	/* ULM -- print ulimit() argument */
	prt_rlm,	/* RLM -- print get/setrlimit() argument */
	prt_cnf,	/* CNF -- print sysconfig() argument */
	prt_inf,	/* INF -- print systeminfo() argument */
	prt_ptc,	/* PTC -- print pathconf/fpathconf() argument */
	prt_fui,	/* FUI -- print fusers() input argument */
	prt_idt,	/* IDT -- print idtype_t, waitid() argument */
	prt_lwf,	/* LWF -- print lwp_create() flags */
	prt_itm,	/* ITM -- print [get|set]itimer() arg */
	prt_llo,	/* LLO -- print long long offset arg */
	prt_vtr,	/* VTR -- print vtrace() code */
	prt_mod,	/* MOD -- print modctl() subcode */
	prt_whn,	/* WHN -- print lseek() whence arguiment */
	prt_acl,	/* ACL -- print acl() code */
	prt_aio,	/* AIO -- print kaio() code */
	prt_aud,	/* AUD -- print auditsys() code */
	prt_dec,	/* HID -- hidden argument, not normally called */
};
