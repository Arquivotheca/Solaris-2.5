/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)support.c 1.17	94/12/07 SMI"

/* from SunOS 4.1 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/user.h>
#include <sys/time.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/mmu.h>
#include <sys/pte.h>
#include <v7/sys/privregs.h>
#include <sys/bootconf.h>
#include <sys/debug/debugger.h>
#include <sys/ptrace.h>
#include <sys/sysmacros.h>
#include <sys/openprom.h>


#ifdef sparc
u_int npmgrps;
u_int segmask;
extern struct bootops *bootops;
extern int pagesize;
#endif

#ifdef CPR_DEBUG
#define	PRINTF prom_printf
#else
#define	PRINTF
#endif CPR_DEBUG

char *strcpy(char *, char *);

#define	MAX_READ	0x8000	/* prom_read() is reading 32k at a time */
#define	BUF_NEEDED	64	/* Might not need that much */
u_char	read_buf[BUF_NEEDED * 1024];	/* Change to a cont/define later */
u_char *read_ptr, *fptr, *e_buf_addr;
int initial;

_exit()
{
	(void) prom_enter_mon();
}

extern char target_bootname[];
extern char target_bootargs[];

/*
 * Property intercept routines for kadb, so that it can
 * tell unix it's real name, and it's real bootargs. We
 * also let it figure out our virtual start and end addresses
 * rather than hardcoding them somewhere nasty.
 */
int
cpr_getprop(struct bootops *bop, char *name, void *buf)
{
	extern char	start[];
	extern char	end[];
	u_int start_addr = (u_int)start;
	u_int end_addr = (u_int)end;

	if (strcmp("whoami", name) == 0) {
		(void) strcpy(buf, target_bootname);
	} else if (strcmp("boot-args", name) == 0) {
		(void) strcpy(buf, target_bootargs);
	} else if (strcmp("cprboot-start", name) == 0) {
		bcopy(&start_addr, buf, sizeof (caddr_t));
	} else if (strcmp("cprboot-end", name) == 0) {
		bcopy(&end_addr, buf, sizeof (caddr_t));
	} else
		return (BOP_GETPROP(bop->bsys_super, name, buf));
	return (0);
}

#ifdef NOTNOW
static int
cpr_getproplen(struct bootops *bop, char *name)
{
	if (strcmp("whoami", name) == 0) {
		return (strlen(target_bootname) + 1);
	} else if (strcmp("boot-args", name) == 0) {
		return (strlen(target_bootargs) + 1);
	} else if (strcmp("debugger-start", name) == 0) {
		return (sizeof (void *));
	} else
		return (BOP_GETPROPLEN(bop->bsys_super, name));
}

#endif NOTNOW

/* ARGSUSED3 */
void
early_startup(union sunromvec *rump, int shim, struct bootops *buutops)
{
	extern struct bootops *bootops;
	static struct bootops cpr_bootops;

	/*
	 * Save parameters from boot in obvious globals, and set
	 * up the bootops to intercept property look-ups.
	 */
	romp = rump;
	bootops = buutops;

	prom_init("cpr", rump);

	if (BOP_GETVERSION(bootops) != BO_VERSION) {
		prom_printf("WARNING: %d != %d => %s\n",
		    BOP_GETVERSION(bootops), BO_VERSION,
		    "mismatched version of /boot interface.");
	}

	bcopy((caddr_t)bootops, (caddr_t)&cpr_bootops,
	    sizeof (struct bootops));

	cpr_bootops.bsys_super = bootops;
	cpr_bootops.bsys_getprop = cpr_getprop;

	/*
	cpr_bootops.bsys_getproplen = cpr_getproplen;
	*/

	bootops = &cpr_bootops;

	/*
	(void) fiximp();
	*/
}

/*
 * A layer between cprboot and ufs read.
 * Force a big read to ufs, so that the
 * extra ufs copy can be by passed.
 */
int
cpr_read(int fd, caddr_t buf, u_int count, u_int *rtn_bufp)
{
	int size = 0;
	int n;		/* XXX: Debug only */

	if (initial == 0) {
		read_ptr = fptr = read_buf;
		e_buf_addr = read_ptr + MAX_READ - 1;
		initial = 1;
	}

	PRINTF("CPR_READ: Asked to read %d (init=%d)\n", count, initial);
	PRINTF("CPR_READ: fptr %x e_buf_addr %x\n", fptr, e_buf_addr);

	/*
	 * Force to read 32k data at a time.
	 */
read_again:
	/*
	 * ufs read detects read pass EOF.
	 */
	if (fptr == read_buf) {
		if ((n = read(fd, read_ptr, MAX_READ)) <= 0) {
			prom_printf("cpr_read: failed to read 32k\n");
			return (-1);
		} else {
			PRINTF("CPR_READ: Just read %d bytes\n", n);
		}
	}

	if ((fptr + count - 1) > e_buf_addr) {
		size = (e_buf_addr - fptr) + 1;
		bcopy(fptr, read_buf, size);
		e_buf_addr = (read_buf+size+MAX_READ-1);
		read_ptr = read_buf + size;

		PRINTF("Wrap around: size %d fptr %x e_buf_addr %x "
			"read_ptr %x\n", size, fptr, e_buf_addr, read_ptr);

		fptr = read_buf;
		goto read_again;
	}

	if (buf != NULL) {
		bcopy(fptr, buf, (unsigned)count);
	} else {
		*rtn_bufp = (u_int)fptr;
		PRINTF("CPR_READ: Return ptr %x\n", *rtn_bufp);
	}

	fptr += count;

	PRINTF("CPR_READ: After read fptr %x\n", fptr);

	if ((fptr - 1) == e_buf_addr) {
		fptr = read_ptr = read_buf;
		e_buf_addr = read_buf + MAX_READ - 1;
	PRINTF("CPR_READ: Exhausted buffer, reset fptr and e_buf_addr\n");
	}

	return (count);
}

char *
strcpy(char *s1, char *s2)
{
	char *os1 = s1;

	while(*s1++ = *s2++);

	return(os1);
}
