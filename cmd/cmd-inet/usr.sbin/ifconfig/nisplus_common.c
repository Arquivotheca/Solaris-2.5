/* 
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */


/* NOTE: This file is copied from /usr/src/lib/nsswitch/nisplus/nisplus_common.c */
/*       to make use of useful nisplus programming routines. It should track modifications */
/*       to the original file */


#include <stdlib.h>
#include <string.h>
#include "nisplus_common.h"
#include <rpcsvc/nislib.h>

void
nis_cursor_create(p)
	nis_cursor *p;
{
	p->no.n_len	  = 0;
	p->no.n_bytes = 0;
	p->max_len	  = 0;
}

void
nis_cursor_set_first(p)
	nis_cursor *p;
{
	if (p->max_len != 0) {
		free(p->no.n_bytes);
	}
	p->no.n_bytes = 0;
	p->no.n_len	  = 0;
	p->max_len	  = 0;
}

void
nis_cursor_set_next(p, from)
	nis_cursor	  *p;
	struct netobj *from;
{
	if (from->n_len == 0) {
		/* Grunge to treat netobj with n_len == 0 as a distinct netobj */
		if (p->max_len == 0) {
			/* Could trust malloc(0) to do the right thing; would rather not */
			p->max_len = 1;
			p->no.n_bytes = malloc(p->max_len);
		}
	} else {
		if (p->max_len < from->n_len) {
			if (p->max_len != 0) {
				free(p->no.n_bytes);
			}
			p->max_len = from->n_len;
			p->no.n_bytes = malloc(p->max_len);
		}
		memcpy(p->no.n_bytes, from->n_bytes, from->n_len);
	}
	p->no.n_len = from->n_len;
}

#ifdef UNDEF
void
nis_cursor_destroy(p)
	nis_cursor *p;
{
	nis_cursor_set_first(p);
}
#endif UNDEF

void
nis_cursor_free(p)
	nis_cursor *p;
{
	nis_cursor_set_first(p);
}

nis_result *
nis_cursor_getXXXent(p, table_name)
	nis_cursor *p;
	nis_name   table_name;
{
	nis_result *res;
	
	if (nis_cursor_is_first(p)) {
		res = nis_first_entry(table_name);
	} else {
		res = nis_next_entry (table_name, &p->no);
	}
	
	if (res != 0) {	/* ==== ?? && NIS_STATUS(res) == NIS_SUCCESS ?? */
		nis_cursor_set_next(p, &res->cookie);
	}
	return res;
}

int
nis_libcinfo_init(ip, table_name)
	nis_libcinfo *ip;
	char *table_name;
{
	int  tn_len;
	
	ip->workable = 0;
	
	tn_len = strlen(table_name);

	/* Final "." means absolute name, otherwise relative */
	if (table_name[tn_len - 1] == '.') {
		/* Could probably use the string in-place, */
		/*   but let's play safe.		   */
		if (0 == (ip->table_name = malloc(tn_len + 1))) {
			return 0;
		} else {
			strcpy(ip->table_name, table_name);
		}
	} else {
		/*
		 * Absolute name = <table_name>.org_dir.<local_directory>
		 *   Effectively we hand-code nis_orgdir() here; ugh.
		 */
		char *directory;
		char *p;

#define	NIS_ORGDIR		"org_dir"
#define	STRLEN_NIS_ORGDIR	7

		if ((directory = nis_local_directory()) == 0 ||
		    (directory[0] == '.' && directory[1] == '\0') ||
		     /* ^^^^ Hack to cope with current NIS+ (7-Apr-92);  */
		     /* ==== NOTE this means we won't work in "." domain */
		    directory[0] == '\0') {
			return 0;
		}
		p = malloc(tn_len + STRLEN_NIS_ORGDIR + strlen(directory) + 3);
		if (p == 0) {
			return 0;
		} else {
			ip->table_name = p;

			strcpy(p, table_name);
			p += tn_len;
			*p++ = '.';
			strcpy(p, NIS_ORGDIR);
			p += STRLEN_NIS_ORGDIR;
			*p++ = '.';
			strcpy(p, directory);
		}
	}
	
	nis_cursor_create(&ip->cursor);
	
	ip->workable = 1;
/* ==== None of the client code tests workable at present.  Should it? ==== */
	return 1;
}

nis_result *
nisplus_match(p, column_name, key, keylen)
	nis_libcinfo *p;
	char *column_name;
	void *key;
	int keylen;
{
#ifdef OLD_VERSION
	/*
	 * This worked with the 4.1 ZNS prototype of ZNS, but the interface
	 *     seems to have changed
	 */
	ib_request	req;
	nis_attr	fred;

	if (p == 0 || p->workable == 0 || p->table_name == 0) {
		return 0;
	}

	fred.zattr_ndx			= column_name;
	fred.zattr_val.zattr_val_val	= key;
	fred.zattr_val.zattr_val_len	= keylen;

	req.ibr_name			= p->table_name;
	req.ibr_srch.ibr_srch_len	= 1;
	req.ibr_srch.ibr_srch_val	= &fred;
	req.ibr_flags			= 0;
	req.ibr_obj.ibr_obj_len		= 0;
	req.ibr_obj.ibr_obj_val		= 0;
	req.ibr_cbhost.ibr_cbhost_len	= 0;
	req.ibr_cbhost.ibr_cbhost_val	= 0;
	req.ibr_bufsize			= ??????????;
	req.ibr_cookie.n_len		= 0;
	req.ibr_cookie.n_bytes		= 0;

	/* ===== Do we want HARD_LOOKUP too? */
	return nis_list(&req, FOLLOW_LINKS | FOLLOW_PATH);
#else	!OLD_VERSION
	/*
	 * ==== Hack-attack version to use the new interface.
	 * ==== Assumes that "key" is a null-terminated string.
	 *	This should be revisited
	 */
	char namebuf[1024];
	
	if (p == 0 || p->workable == 0 || p->table_name == 0 ||
	    key == 0 || keylen <= 0) {
		return 0;
	}

	sprintf(namebuf, "[%s=%s]%s", column_name, key, p->table_name);
	
	/* ===== Do we want HARD_LOOKUP too? */
	return nis_list(namebuf, FOLLOW_LINKS | FOLLOW_PATH, 0, 0);
#endif	OLD_VERSION
}

/*
 * nisplus_search() -- takes an unqualified or partially qualified name as a
 *   key and uses EXPAND_NAME to do a DNS-style lookup.  We map
 *	<name>.<domain> --> [<column_name>=<name>]<table>.org_dir.<domain>
 *   and in the (common) degenerate case
 *	<name>          --> [<column_name>=<name>]<table>.org_dir
 */

static int domain_ok = 0;

nis_result *
nisplus_search(column_name, table, key)
	char *column_name;
	char *table;
	nis_name key;
{
	/*
	 * ==== Hack-attack version to use the new interface.
	 * ==== Assumes that "key" is a null-terminated string.
	 *	This should be revisited
	 */
	char namebuf[1024];
	char *p;

	if (!domain_ok) {
		const char *directory;

		if ((directory = nis_local_directory()) == 0 ||
		    (directory[0] == '.' && directory[1] == '\0') ||
		     /* ^^^^ Hack to cope with current NIS+ (7-Apr-92);  */
		     /* ==== NOTE this means we won't work in "." domain */
		    directory[0] == '\0') {
			return 0;
		}
		domain_ok = 1;
	}
	/* ==== Efficiency be damned */
	sprintf(namebuf, "[%s=", column_name);
	p = strchr(key, '.');
	if (p == 0) {
		strcat(namebuf, key);
	} else {
		strncat(namebuf, key, p - key);
	}
	strcat(namebuf, "]");
	strcat(namebuf, table);
	strcat(namebuf, ".org_dir");
	if (p != 0) {
		strcat(namebuf, p);
	}
	/* ===== Do we want HARD_LOOKUP too? */
	return nis_list(namebuf, EXPAND_NAME | FOLLOW_LINKS | FOLLOW_PATH,
			0, 0);
}
