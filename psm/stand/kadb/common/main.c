/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 */

#ident	"@(#)main.c	1.45	95/04/11 SMI" /* from SunOS 4.1 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/fcntl.h>
#include <sys/reboot.h>
#include <sys/bootconf.h>
#include <sys/debug/debugger.h>
#ifdef i386
#undef _KERNEL
#include <sys/bootsvcs.h>			/* syscall dcl's */
#define	_KERNEL
#undef	printf
#undef	malloc
#undef  gets
#include <sys/reg.h>
#endif
#include "symtab.h"

#ifdef i386
int kcmntrap;
int do_trappass;
int kxc_call;
int *xc_initted;
extern void kadb_mpinit ();
unsigned int	_mmu_pageshift = 12;
unsigned int    _mmu_pageoffset = (4096-1);
extern struct regs *regsave;		/* temp reg save area on ministack */
int first_time;
extern int estack;
extern void init_bootops();
#else
extern char *malloc();
extern char estack[];
#endif	/* i386 */

extern char *rindex();
extern int debugcmd();

/*
 * These are here because the common code between adb and kadb in adb
 * uses them
 */
char *prompt = "kadb";
int cmd_line_prompt;
int kernelbase;
func_t go2;

#define	MALLOC_PAD	0x1000	/* malloc pad - XXX - should be PAGESIZE? */

int interactive = 0;			/* true if -d flag passed in */

#ifdef i386
main(i_fp)
struct regs *i_fp;
#else
main()
#endif	/* i386 */
{
	func_t load_it();
	char *arg;
#ifdef i386
	func_t fake_bp();
	extern	int	i_fparray[];
	extern struct bootops *bopp;
	reg = regsave = i_fp;
#endif

	ttysync();

#ifdef i386
	init_bootops(bopp);
#endif

	spawn((int *)estack, debugcmd);
	while ((go2 = load_it(&arg)) == (func_t)-1)
		continue;
	if (go2 == (func_t)-2)
		prom_exit_to_mon();

	free(malloc(MALLOC_PAD));
	printf("%s loaded - 0x%x bytes used\n",
	    arg, mmu_ptob(pagesused));

#ifdef i386
	if (lookup("kernelbase") != 0) {
		kernelbase = lookup("kernelbase")->s_value;
	} else
		kernelbase = 0xf8000000;
	if (lookup("cmntrap") != 0) {
		kcmntrap = lookup("cmntrap")->s_value;
		do_trappass = 1;
	}
	xc_initted = NULL;
	if (lookup("xc_call_debug") != 0) {
		kxc_call = lookup("xc_call_debug")->s_value;
		if (lookup("xc_initted") != 0) {
			xc_initted = (int *)(lookup("xc_initted")->s_value);
		}
	}
	if (lookup("kadb_mpinit_ptr") != 0) {
		*(int *)lookup("kadb_mpinit_ptr")->s_value = (int) kadb_mpinit;
	}
	if (lookup("i_fparray_ptr") != 0) {
		*(int *)lookup("i_fparray_ptr")->s_value = (int)i_fparray;
	}
	first_time++;
	i_fp->r_eip = (int)&fake_bp;
#else
	/*
	 * We cannot do lookup() here since the sparc will not let us
	 * do lookup util we switch stacks to our own (at this point we're
	 * still running on the PROM's stack). The switch is done in
	 * cmd().
	 *
	 * XXX - the above is not true - we currently do some lookups in
	 * readfile().
	 */
	kernelbase = KERNELBASE;	/* XXX FIXME */
	if (interactive) {
		(void) cmd();
		if (dotrace) {
			scbstop = 1;
			dotrace = 0;
		}
	}
#endif
	nobrk = 1;		/* no more sbrk's allowed */

	exitto(go2);
	/*NOTREACHED*/
}

/*
 * Just a funny way to `stat' the pathname...
 */
static int
test_file(char *pathname)
{
	int fd;

	if ((fd = open(pathname, O_RDONLY)) >= 0) {
		close(fd);
		return (1);
	} else {
		return (0);
	}
}

/*
 * Construct the appropriate pathname for the object to be debugged (usually
 * the kernel).  This is a highly heuristic process.  The algorithms attempted
 * are:
 *   1]	If the filename specifies a full path (begins with a '/'), attempt
 *	to load it.
 *   2]	Use the path to this object as a prefix to the filename.
 *   3] Use the path "/platform/<impl-arch-name>/" as a prefix to the given
 *	filename.
 *   4]	Use the path to this object as a prefix to "/kernel/unix".
 *   5] Use the path "/platform/<impl-arch-name>/kernel/unix".
 * Note that the second algorithm is the one that usually hits and that unless
 * this object (kadb) is relocated, the 2nd and 3rd as well as the 4th and 5th
 * attempts will yield the same pathname.
 */
int
get_path_name(char *filename)
{
	char pathname[LINEBUFSZ];
	char platform[50];
	char *p;

	/*
	 * First, if the name specified begins with a slash and the
	 * file exists, use that name without modification.  If the
	 * absolute path name is non-existent, return an error and
	 * have the user try again.
	 */
	if (*filename == '/')
		if (test_file(filename)) {
			return (0);
		} else {
			return (1);
		}

	/*
	 * Second, strip off the program name from myname (BOP "whoami")
	 * and use that path as a prefix to the file name.
	 */
	(void) strcpy(pathname, myname);
	if ((p = rindex(pathname, '/')) != NULL) {
		if (*filename != '/')
			p++;
		strcpy(p, filename);
		if (test_file(pathname)) {
			(void) strcpy(filename, pathname);
			return (0);
			}
	}

	/*
	 * Third, construct the path to the filename based on the
	 * implementation architecture name as provided by the boot program.
	 */
	(void) BOP_GETPROP(bootops, "impl-arch-name", platform);
	(void) sprintf(pathname, "/platform/%s/%s", platform, filename);
	if (test_file(pathname)) {
		(void) strcpy(filename, pathname);
		return (0);
	}

	/*
	 * Fourth, strip off the program name from myname (BOP "whoami") and
	 * use that path as a prefix to the default file name (kernel/unix).
	 */
	(void) strcpy(pathname, myname);
	if ((p = rindex(pathname, '/')) != NULL) {
		strcpy(p, "/kernel/unix");
		if (test_file(pathname)) {
			(void) strcpy(filename, pathname);
			return (0);
		}
	}

	/*
	 * Finally, construct the path based on the implementation
	 * architecture name as provided by the boot program and the
	 * default name.
	 */
	(void) sprintf(pathname, "/platform/%s/kernel/unix", platform);
	if (test_file(pathname)) {
		(void) strcpy(filename, pathname);
		return (0);
	}

	return (1);
}

#ifndef KERNEL_AGENT
ka_main_loop(char c)
{
	printf("Command not available\n");
}
#endif
