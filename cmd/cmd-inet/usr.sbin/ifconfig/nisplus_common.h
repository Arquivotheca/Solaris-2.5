/* 
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */




/* NOTE: This file is copied from /usr/src/lib/nsswitch/nisplus/nisplus_common.h */
/*       to make use of useful nisplus programming routines. It should track modifications */
/*       to the original file */


#ifndef _NISPLUS_COMMON_H
#define _NISPLUS_COMMON_H

#include <sys/types.h>
#include <rpcsvc/nis.h>

/* ====== should group the ZNS table-names and column-names somewhere	*/
/*	  (not here)							*/

/*
 * ==== document:
 *	allocation, copying, freeing of netobjs used as placeholders by
 *	nis_first_entry() and nis_next_entry()
 * ==== General theme: these routines don't screw up malloc()s or free()s,
 *	however perversely you call them.
 * ==== rightly or wrongly, we treat a netobj whose n_len = 0 as a valid
 *	placeholder; it is *not* taken to mean "first".  We distinguish this
 *	case internally by pointing n_bytes at (zero bytes of) real memory,
 *	rather than having it be NULL.
 */
typedef struct {
	/* ==== ZNS table-name probably belongs here (or in a layer on top  */
	/*	of this), since this only makes sense (I think) wrt a given */
	/*	table.  Then nis_cursor_getXXXent() would take one parameter*/
	struct netobj no;
	u_int	  max_len;	/* no.n_bytes points to a region this big */
} nis_cursor;

/* ==== ?? Rebaptize create/destroy, since (esp) for destroy that's not */
/*	really what we mean.						*/
#ifdef __STDC__
void	nis_cursor_create   (nis_cursor *p);
void	nis_cursor_set_first(nis_cursor *p);
void	nis_cursor_set_next (nis_cursor *p, struct netobj *from);
/* ==== void	nis_cursor_destroy  (nis_cursor *p); */
void	nis_cursor_free     (nis_cursor *p);
nis_result *
	nis_cursor_getXXXent(nis_cursor *p, nis_name table_name);
#else !__STDC__
void	nis_cursor_create   ();
void	nis_cursor_set_first();
void	nis_cursor_set_next ();
/* ==== void	nis_cursor_destroy  (); */
void	nis_cursor_free     ();
nis_result *
	nis_cursor_getXXXent();
#endif __STDC__

#define	nis_cursor_is_first(p) ((p)->no.n_bytes == 0)

/*
 * The stuff above, bundled up for the benefit of library routines that want
 *   most or all of it.
 */
typedef struct {
	int		workable;	/* ==== unused? */
	nis_name	table_name;
	nis_cursor	cursor;
} nis_libcinfo;

#ifdef __STDC__
int	    nis_libcinfo_init	(nis_libcinfo *p, char *table_leaf);
nis_result *nisplus_match	(nis_libcinfo *p,
				 char *column_name, void *key, int keylen);
nis_result *nisplus_search	(char *column_name, char *table, nis_name key);
#else !__STDC__
int         nis_libcinfo_init();
nis_result *nisplus_match();
#endif __STDC__

#endif	_NISPLUS_COMMON_H
