/*	Copyright (c) 1984, 1986, 1987, 1988, 1989, 1990, 1991 SMI	*/
/*	  All Rights Reserved						*/


#pragma ident	"@(#)fslib.h	1.2	94/02/09 SMI"

#ifndef	_fslib_h
#define	_fslib_h

#include	<sys/mnttab.h>

/*
 * This structure is used to build a list of
 * mnttab structures from /etc/mnttab.
 */
typedef struct mntlist {
	int		mntl_flags;
	u_int		mntl_dev;
	struct mnttab	*mntl_mnt;
	struct mntlist	*mntl_next;
} mntlist_t;

/*
 * Bits for mntl_flags.
 */
#define	MNTL_UNMOUNT	0x01	/* unmount this entry */
#define	MNTL_DIRECT	0x02	/* direct mount entry */

/*
 * Routines available in fslib.c:
 */
int		fsaddtomtab(struct mnttab *);
int		fsrmfrommtab(struct mnttab *);
void		fsfreemnttab(struct mnttab *);
struct mnttab 	*fsdupmnttab(struct mnttab *);
void		fsfreemntlist(mntlist_t *);
int		fsputmntlist(FILE *, mntlist_t *);

mntlist_t	*fsmkmntlist(FILE *);
mntlist_t	*fsgetmntlist();
mntlist_t	*fsgetmlast(mntlist_t *, struct mnttab *);

int	fslock_mnttab();
void	fsunlock_mnttab(int);
int	fsgetmlevel(char *);
int	fsstrinlist(const char *, const char **);

#undef MIN
#undef MAX
#define	MIN(a, b)	((a) < (b) ? (a) : (b))
#define	MAX(a, b)	((a) > (b) ? (a) : (b))

#endif	/* !_fslib_h */
