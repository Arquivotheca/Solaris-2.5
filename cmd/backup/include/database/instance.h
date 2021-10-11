
/*	@(#)instance.h 1.4 92/05/07	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#ifndef INSTANCE_H
#define	INSTANCE_H

#include <sys/types.h>

#define	INSTANCE_FREEREC	(u_long)0
#define	INSTANCE_FIRSTDATA	(u_long)1

#define	NULL_IREC	(struct instance_record *)0
#define	NULL_IENT	(struct instance_entry *)0

/*
 * number of entries per record is fixed for any single instance file,
 * but the number is not determined until the time the file is created.
 *
 * Records in the file may be linked using the `ir_next' field.  The
 * first record in the file (`INSTANCE_FREEREC') is treated as the
 * head of a list of free blocks.  If this record's `ir_next' field is
 * equal to INSTANCE_FREEREC, the free list is empty.  The free list
 * header is also used to specify the record size in this particular
 * file.  In the freelist record, `i_entry[0].ie_dumpid' is equal to the
 * number of `instance_entry' structs in a single record and the
 * `i_entry[0].ie_dnode_index' field is equal to the total record size.
 * The other `i_entry' elements of the freelist record are unused.
 */
struct instance_record {
	u_long ir_next;			/* index of next record */
	struct instance_entry {
		u_long	ie_dumpid;	/* dump description file id */
		u_long	ie_dnode_index;	/* index in that dump's dnode file */
	} i_entry[1];
};

int	entries_perrec;		/* freelistrec.i_entry[0].ie_dumpid */
int	instance_recsize;	/* freelistrec.i_entry[1].ie_dnode_index */

extern	char *inst_dblockmap;
extern	int inst_dblockmapsize;

/*
 * the default for entries per record is 20.  This will allow the database
 * to optimally keep track of the following dumps:
 *
 *	2  yearly
 *	3  quarterly
 *	3  recent fulls
 *	10 recent incrementals
 *
 *	(plus two others for good measure)
 *
 * 3/12/91: Now the default for entries per instance is 5.  The belief
 * is that most files will not appear on all incrementals.
 */
#define	COMPUTE_INST_RECSIZE(nent)	sizeof (struct instance_record)+\
	((nent-1)*sizeof (struct instance_entry))

#define	DEFAULT_INSTANCE_ENTRIES	5

#define	DEFAULT_INSTANCE_RECSIZE	\
	COMPUTE_INST_RECSIZE(DEFAULT_INSTANCE_ENTRIES)

#ifdef __STDC__
extern int instance_open(const char *);
extern int instance_newrec(u_long);
extern int instance_addent(u_long, u_long, u_long);
extern int unused_instance(u_long, int *);
extern void instance_close(const char *);
extern void instance_freerec(u_long, struct instance_record *);
extern int instance_trans(const char *);
extern struct instance_record *instance_getrec(u_long);

#else
extern int instance_open();
extern int instance_newrec();
extern int instance_addent();
extern int unused_instance();
extern void instance_close();
extern void instance_freerec();
extern int instance_trans();
extern struct instance_record *instance_getrec();
#endif
#endif
