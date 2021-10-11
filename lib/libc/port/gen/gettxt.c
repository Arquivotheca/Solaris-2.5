/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/


/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)gettxt.c	1.3	93/10/13 SMI"

#ifdef __STDC__
	#pragma weak Msgdb = _Msgdb
	#pragma weak gettxt = _gettxt
#endif
#include "synonyms.h"
#include "shlib.h"
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fmtmsg.h>
#include "_locale.h"


#define	P_locale	"/usr/lib/locale/"
#define	L_locale	(sizeof (P_locale))
#define	MAXDB	10	/* maximum number of data bases per program */
#define	DEF_LOCALE	"/usr/lib/locale/C/LC_MESSAGES/"
#define	MESSAGES 	"/LC_MESSAGES/"
#define	DB_NAME_LEN	15
#define	NULL	0

extern  int	atoi();
extern	void	close();
extern  void	*malloc();
const	char 	*handle_return();

/* support multiple versions of a package */

char	*Msgdb = (char *)NULL;

static 	char	*saved_locale = "";
#ifdef __STDC__
static  const char	*not_found = "Message not found!!\n";
#else
static  char	*not_found = "Message not found!!\n";
#endif


static	struct	db_info {
	char	db_name[DB_NAME_LEN];	/* name of the message file */
	int	addr;			/* virtual memory address */
	size_t  length;
	int	fd;
} *db_info;

static	int	db_count;   	/* number of currently accessible data bases */

#ifdef __STDC__
const char *
gettxt(msg_id, dflt_str)
const char	*msg_id;
const char	*dflt_str;
#else
char *
gettxt(msg_id, dflt_str)
char	*msg_id;
char	*dflt_str;
#endif
{
	char  msgfile[DB_NAME_LEN];	/* name of static shared library */
	int   msgnum;			/* message number */
	char  pathname[128];		/* full pathname to message file */
	int   i;
	int   new_locale = 0;
	int   fd = -1;
	struct stat sb;
	caddr_t   addr;
	char  *tokp;
	int   name_len = 0;

	if ((msg_id == NULL) || (*msg_id == NULL)) {
		return (handle_return(dflt_str));
	}

	/* first time called, allocate space */
	if (!db_info)
		if ((db_info = (struct db_info *)
			malloc(MAXDB * sizeof (struct db_info) + 16)) == 0)
			return (handle_return(dflt_str));
		else
			saved_locale = (char *)(db_info + MAXDB);

	/* parse msg_id */

	if (((tokp = strchr(msg_id, ':')) == NULL) || *(tokp+1) == '\0')
		return (handle_return(dflt_str));
	if ((name_len = (tokp - msg_id)) >= DB_NAME_LEN)
		return (handle_return(dflt_str));
	if (name_len) {
		(void) strncpy(msgfile, msg_id, name_len);
		msgfile[name_len] = '\0';
	}
	else {
		if (Msgdb && strlen (Msgdb) <= (unsigned)14)
			(void) strcpy(msgfile, Msgdb);
		else {
			char *p;
			p = (char *)setcat ((const char *)0);
			if ((p != NULL) && strlen(p) <= (unsigned)14)
				(void) strcpy(msgfile, p);
			else
				return (handle_return(dflt_str));
		}
	}
	while (*++tokp)
		if (!isdigit(*tokp))
			return (handle_return(dflt_str));
	msgnum = atoi(msg_id + name_len + 1);

	/* Has locale been changed? */

	if (strcmp(_cur_locale[LC_MESSAGES], saved_locale) == 0) {
		for (i = 0; i < db_count; i++)
			if (strcmp(db_info[i].db_name, msgfile) == 0)
				break;
	} else { /* new locale - clear everything */
		(void) strcpy(saved_locale, _cur_locale[LC_MESSAGES]);
		for (i = 0; i < db_count; i++) {
			(void) munmap((caddr_t)db_info[i].addr,
			    db_info[i].length);
			close(db_info[i].fd);
			(void) strcpy(db_info[i].db_name, "");
			new_locale++;
		}
		db_count = 0;
	}
	if (new_locale || i == db_count) {
		if (db_count == MAXDB)
			return (handle_return(dflt_str));
		(void) strcpy(pathname, P_locale);
		(void) strcpy(&pathname[L_locale - 1], saved_locale);
		(void) strcat(pathname, MESSAGES);
		(void) strcat(pathname, msgfile);
		if ((fd = open(pathname, O_RDONLY)) == -1 ||
			fstat(fd, &sb) == -1 ||
				(addr = mmap(0, sb.st_size,
					PROT_READ, MAP_SHARED,
						fd, 0)) == (caddr_t)-1) {
			if (fd != -1)
				close(fd);
			if (strcmp(saved_locale, "C") == 0)
				return (handle_return(dflt_str));

			/* Change locale to C */

			(void) strcpy(pathname, DEF_LOCALE);
			(void) strcat(pathname, msgfile);
			for (i = 0; i < db_count; i++) {
				(void) munmap((caddr_t)db_info[i].addr,
							db_info[i].length);
				close(db_info[i].fd);
				(void) strcpy(db_info[i].db_name, "");
			}
			db_count = 0;
			fd = -1;
			if ((fd = open(pathname, O_RDONLY)) != -1 &&
				fstat(fd, &sb) != -1 &&
					(addr = mmap(0, sb.st_size,
						PROT_READ, MAP_SHARED,
						fd, 0)) != (caddr_t)-1)
				(void) strcpy(saved_locale, "C");
			else {
				if (fd != -1)
					close(fd);
				return (handle_return(dflt_str));
			}
		}

		/* save file name, memory address, fd and size */

		(void) strcpy(db_info[db_count].db_name, msgfile);
		db_info[db_count].addr = (int)addr;
		db_info[db_count].length = sb.st_size;
		db_info[db_count].fd = fd;
		i = db_count;
		db_count++;
	}
	/* check if msgnum out of domain */
	if (msgnum <= 0 || msgnum > *(int *)(db_info[i].addr))
		return (handle_return(dflt_str));
	/* return pointer to message */
	return ((char *)(db_info[i].addr + *(int *)(db_info[i].addr
		+ msgnum * sizeof (int))));
}

#ifdef __STDC__
const char *
handle_return (dflt_str)
	const char	*dflt_str;
#else
char *
handle_return (dflt_str)
	char	*dflt_str;
#endif
{
	return (dflt_str && *dflt_str ? dflt_str : not_found);
}
