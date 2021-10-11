/*
 * Copyright (c) 1992 Sun Microsystems, Inc.  All Rights Reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident "@(#)prtconf.c	1.11	95/06/27 SMI"

#include	<stdio.h>
#include	<ctype.h>
#include	<sys/param.h>
#include	<sys/types.h>
#include	<sys/utsname.h>


extern void prtconf_devinfo();
struct utsname uts_buf;
long sysmem();

char	*progname;
char	*promdev = "/dev/openprom";
int	prominfo;
int	verbose;
int	noheader;
int	pseudodevs;
int	fbname;
int	promversion;

#if defined(i386)
	static char *usage = "%s [ -v ] [ -P ] [ -F ]\n";
#else	/* !i386 */
	static char *usage = "%s [ -v ] [ -p ] [ -F ] [ -P ] [ -V ]\n";
#endif	/* !i386 */


main(int argc, char *argv[])
{
	long long	ii;
	long pagesize, npages;
	int	c;
	extern char *optarg;
	extern void setprogname();

	setprogname(argv[0]);
#if defined(i386)
	while ((c = getopt(argc, argv, "vPF")) != -1)  {
#else	/* !i386 */
	while ((c = getopt(argc, argv, "vVpPFf:")) != -1)  {
#endif	/* !i386 */
		switch (c)  {
		case 'v':
			++verbose;
			break;
#if !defined(i386)
		case 'p':
			++prominfo;
			break;
		case 'f':
			promdev = optarg;
			break;
		case 'V':
			++promversion;
			break;
#endif	/* !i386 */
		case  'F':
			++fbname;
			++noheader;
			break;
		case 'P':
			++pseudodevs;
			break;
		default:
			(void) fprintf(stderr, usage, progname);
			exit(1);
			/*NOTREACHED*/
		}
	}


	(void) uname(&uts_buf);

	if (fbname)
		exit(do_fbname());

#if !defined(i386)
	if (promversion)
		exit(do_promversion());
#endif	/* !i386 */

	printf("System Configuration:  Sun Microsystems  %s\n",
	    uts_buf.machine);

	pagesize = sysconf(_SC_PAGESIZE);
	npages = sysconf(_SC_PHYS_PAGES);
	printf("Memory size: ");
	if (pagesize == -1 || npages == -1)
		printf("unable to determine\n");
	else {
		int kbyte = 1024;
		int mbyte = 1024 * 1024;

		ii = (long long) pagesize * npages;
		if (ii >= mbyte)
			printf("%d Megabytes\n", (int) ((ii+mbyte-1) / mbyte));
		else
			printf("%d Kilobytes\n", (int) ((ii+kbyte-1) / kbyte));
	}

	if (prominfo)  {
		printf("System Peripherals (PROM Nodes):\n\n");
		if (do_prominfo() == 0)
			exit(0);
		fprintf(stderr, "%s: Defaulting to non-PROM mode...\n",
		    progname);
	}

	printf("System Peripherals (Software Nodes):\n\n");

	(void) prtconf_devinfo();

	exit(0);
} /* main */

static void
setprogname(name)
char *name;
{
	register char *p;
	extern char *strrchr();

	if (p = strrchr(name, '/'))
		progname = p + 1;
	else
		progname = name;
}
