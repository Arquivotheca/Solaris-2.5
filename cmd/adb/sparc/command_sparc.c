/*
 * adb - sparc command parser
 */

#ident "@(#)command_sparc.c	1.2	93/08/11 SMI"

#include "adb.h"
#include <stdio.h>
#include "fio.h"
#include "fpascii.h"

#define	QUOTE	0200
#define	STRIP	0177

/* 
 * This file contains archecture specific extensions to the command parser
 * The following must be defined in this file:
 *
 *-----------------------------------------------------------------------
 * ext_slash:
 *   command extensions for /,?,*,= .
 *   ON ENTRY:
 *	cmd    - is the char format (ie /v would yeld v)
 *	buf    - pointer to the raw command buffer
 *	defcom - is the char of the default command (ie last command)
 *	eqcom  - true if command was =
 *	atcom  - true if command was @
 *	itype  - address space type (see adb.h)
 *	ptype  - symbol space type (see adb.h)
 *   ON EXIT:
 *      Will return non-zero if a command was found.
 *
 *-----------------------------------------------------------------------
 * ext_getstruct:
 *   return the address of the ecmd structure for extended commands (ie ::)
 *
 *-----------------------------------------------------------------------
 * ext_ecmd:
 *  extended command parser (ie :: commands)
 *   ON ENTRY:
 *	buf    - pointer to the extended command
 *   ON EXIT:
 *	Will exec a found command and return non-zero
 *
 *-----------------------------------------------------------------------
 * ext_dol:
 *   command extensions for $ .
 *   ON ENTRY:
 *	modif    - is the dollar command (ie $b would yeld b)
 *   ON EXIT:
 *      Will return non-zero if a command was found.
 *
 */


/* This is archecture specific extensions to the slash cmd.
 * On return this will return non-zero if there was a cmd!
 */
int
ext_slash(cmd, buf, defcom, eqcom, atcom, itype, ptype)
char	cmd;
char	*buf, defcom;
int	eqcom, atcom, itype, ptype;
{
	return (0);
}

/* breakpoints */
struct	bkpt *bkpthead;

/* This is archecture specific extensions to the $ cmd.
 * On return this will return non-zero if there was a cmd!
 */
int
ext_dol(modif)
int	modif;
{
	switch(modif) 
	{
	case 'b': {
		register struct bkpt *bkptr;

		printf("breakpoints\ncount%8tbkpt%24tcommand\n");
		for (bkptr = bkpthead; bkptr; bkptr = bkptr->nxtbkpt)
			if (bkptr->flag) {
		   		printf("%-8.8d", bkptr->count);
				psymoff(bkptr->loc, ISYM, "%24t");
				printf("%s", bkptr->comm);
			}
		}
		break;
	      default:
		return (0);
	}
	return (1);
}

/*
 * extended commands
 */

static struct ecmd sparc_ecmd[];

/*
 * This returns the address of the ext_extended command structure
 */
struct ecmd *
ext_getstruct()
{
	return (sparc_ecmd);
}

/* This is archecture specific extensions to extended cmds.
 * On return this will return non-zero if there was a cmd!
 */
int
ext_ecmd(buf)
char *buf;
{
	int i;

	i = extend_scan(buf, sparc_ecmd);
	if (i >= 0) 
	{
		(*sparc_ecmd[i].func)();
		return (1);
	}

	return (0);
}	

/*
 * all aval extended commands should go here
 */
static struct ecmd sparc_ecmd[] = {
{ 0 }
};
