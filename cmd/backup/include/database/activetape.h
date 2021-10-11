
/*	@(#)activetape.h 1.4 92/05/07	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#ifndef ACTIVETAPE_H
#define	ACTIVETAPE_H

#include <sys/time.h>				/* for vnode.h */
#include <sys/vnode.h>				/* for inode.h */
#ifdef USG
#include <sys/fs/ufs_inode.h>			/* for dumprestore.h */
#else
#include <ufs/inode.h>				/* for dumprestore.h */
#endif
#include <protocols/dumprestore.h>		/* for LBLSIZE */

#define	DUMPS_PER_TAPEREC	10
#define	TAPE_FREELIST		(u_long)0
#define	TAPE_FIRSTDATA		(u_long)1

#define	TAPE_RECSIZE	sizeof (struct active_tape)

struct active_tape {
	u_long	tape_next;			/* circular list */
	char	tape_label[LBLSIZE];		/* sticky label of tape */
	u_long	tape_status;			/* defines below */
	struct dump_entry {
		u_long	host;			/* internet num of dumper */
		u_long	dump_id;		/* specific dump */
		u_long	tapepos;		/* file # of this dump */
	} dumps[DUMPS_PER_TAPEREC];
};

#define	TAPE_OFFSITE	0x00000001
#define	TAPE_NOLABEL	0x00000002	/* `c_label' on tape says "none" */

#define	NULL_TREC	(struct active_tape *)0
#define	NULL_DENT	(struct dump_entry *)0

#ifdef __STDC__
extern int tape_open(const char *);
extern int tape_newrec(const char *, u_long);
extern int tape_changehost(u_long, u_long);
extern int tape_freerec(u_long);
extern int tape_addent(u_long, u_long, u_long, u_long);
extern void tape_close(const char *);
extern void tape_remdump(u_long);
extern int tape_trans(const char *);
extern struct active_tape *tape_lookup(const char *, u_long *);
extern struct active_tape *tape_nextent(const char *, u_long);
#else
extern int tape_open();
extern int tape_newrec();
extern int tape_changehost();
extern int tape_freerec();
extern int tape_addent();
extern void tape_close();
extern void tape_remdump();
extern int tape_trans();
extern struct active_tape *tape_lookup();
extern struct active_tape *tape_nextent();
#endif
#endif
