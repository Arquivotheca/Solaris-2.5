/*
 *		    (c) 1994  Sun Microsystems, Inc.
 *			   All Rights Reserved
 */

#ident	"@(#)replace.c	1.5	95/03/23 SMI"

/*
 * COPYRIGHT NOTICE
 * 
 * This source code is designated as Restricted Confidential Information
 * and is subject to special restrictions in a confidential disclosure
 * agreement between HP, IBM, SUN, NOVELL and OSF.  Do not distribute
 * this source code outside your company without OSF's specific written
 * approval.  This source code, and all copies and derivative works
 * thereof, must be returned or destroyed at request. You must retain
 * this notice on any copies which you make.
 * 
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED 
 */
/*
 * OSF/1 1.2
 */
#if !defined(lint) && !defined(_NOIDENT)
static char rcsid[] = "@(#)$RCSfile: replace.c,v $ $Revision: 1.3.1.4 $ (OSF) $Date: 1992/03/23 19:14:56 $";
#endif
/* 
 * replace.c - regular expression pattern replacement functions
 *
 * DESCRIPTION
 *
 *	These routines provide for regular expression file name replacement
 *	as required by pax.
 *
 * AUTHORS
 *
 *	Mark H. Colburn, NAPS International
 *
 * Sponsored by The USENIX Association for public distribution. 
 *
 * Copyright (c) 1989 Mark H. Colburn.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice is duplicated in all such 
 * forms and that any documentation, advertising materials, and other 
 * materials related to such distribution and use acknowledge that the 
 * software was developed * by Mark H. Colburn and sponsored by The 
 * USENIX Association. 
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Revision 1.2  89/02/12  10:05:59  mark
 * 1.2 release fixes
 * 
 * Revision 1.1  88/12/23  18:02:36  mark
 * Initial revision
 * 
 */

/* Headers */

#include <wchar.h>
#include <wctype.h>
#include "pax.h"

/* Messages */

#define	RP_NOADD	"A replacement string was not added"
#define	RP_SPACE	"Unable to allocate enough memory."
#define	RP_MOD		"Invalid RE modifier"
#define	RP_TRLR		"An invalid trailing option was specified with the -s option."
#define	RP_BADDEL	"Bad delimeters"
#define	RP_BADMB	"An invalid multi-byte character was encountered."
#define	RP_ADD		"add %s ? "
#define	RP_EXTRACT	"extract %s ? "
#define	RP_PASS		"pass %s ? "
#define	RP_RENAME	"rename %s? "
#define	RP_BADREF	"An invalid backreference was specified."
#define	RP_STRLONG	"The new filename exceeded the path name limit.\n"
#define	RP_NOSUB	"         There was no substitution done on %s\n"
#define	RE_DAMAGE2	"regexp: damaged match string"

/* External declarations */

extern int
isyesno(const char *respMb, size_t testLen);

/* Internal routines */

static long
do_regsub(regmatch_t pmatch[], char *src, long offst, char *replace);

/*
 * sepsrch - search a wide char string for a wide char
 *
 * DESCRIPTION
 *
 *	Search the input string for the specified delimiter
 *	If the char is found then return a pointer into the
 *  input string where the character was found, otherwise
 *  return a NULL string.
 *
 * PARAMETERS
 *
 *	wcstr - the string to be searched
 *	sep	  - the char to search for
 */

wchar_t *
sepsrch(const wchar_t *wcstr, wchar_t sep)
{
	wchar_t *wc;

	wc = (wchar_t *)wcstr;
	if (*wc == (wchar_t)NULL)
		return ((wchar_t *)NULL);
	do {
		if ((*wc == sep) && (*(wc-1) != L'\\'))
			return (wc);
	} while (*(++wc));
	return((wchar_t *)NULL);
}


/*
 * chk_back_ref - check the back references in a replacement string
 *
 * DESCRIPTION
 *	Check for valid back references in the replacement string.  An
 *  invalid back reference will cause chk_back_ref to return -1.
 *  No back references will return 0, otherwise the highest back
 *  reference found will be returned.
 *
 * PARAMETERS
 *
 *	wchar_t *ptr - a wide char pointer into the replacement string.
 */

int
chk_back_ref(const wchar_t *ptr)
{
	int		bkref;
	int		retval;

	retval = 0;
	while (*ptr != L'\0') {
		if (*ptr++ == L'\\') {
			/* get out quick if an invalid back reference */
			if (*ptr == L'0')
				return -1;
			if ((*ptr >= L'1') && (*ptr <= L'9')) {
				bkref = *ptr - '0';
				retval = (retval < bkref) ? bkref : retval;
			}
			ptr++;
		}
	}
	return retval;
}


/* add_replstr - add a replacement string to the replacement string list
 *
 * DESCRIPTION
 *
 *	Add_replstr adds a replacement string to the replacement string
 *	list which is applied each time a file is about to be processed.
 *
 * PARAMETERS
 *
 *	char	*pattern	- A regular expression which is to be parsed
 */


void
add_replstr(char *pattern)
{
    Replstr	*rptr;
    wchar_t	wc_pattern[PATH_MAX+1];
    wchar_t 	*wcs;
    wchar_t 	*ptr;
    wchar_t 	*wptr;
    wchar_t	sep;
    char    	old[PATH_MAX+1];
    char	*new;
    size_t	retval;
    int		highbkref;

    if ((rptr = (Replstr *) calloc(1,sizeof(Replstr))) == (Replstr *)NULL) {
	warn(MSGSTR(RP_NOADD, "Replacement string not added"),
	     MSGSTR(RP_SPACE, "No space"));
	return;
    }

    if ((new = (char *) malloc(strlen(pattern)+1)) == (char *)NULL) {
    warn(MSGSTR(RP_NOADD, "Replacement string not added"),
         MSGSTR(RP_SPACE, "No space"));
    free(rptr);
    return;
    }
    retval = mbstowcs(wc_pattern,pattern,PATH_MAX+1);
    switch (retval) {
	case -1:
		/*ERROR-Invalid mb char*/
		warn(MSGSTR(RP_NOADD, "Replacement string not added"),
		     MSGSTR(RP_BADMB, "Invalid multi-byte character"));
		goto quit;
	case PATH_MAX+1:
		/*ERROR-Name too large no terminating null */
		warn(MSGSTR(RP_NOADD, "Replacement string not added"),
		     MSGSTR(RP_STRLONG, "Pathname too long"));
		goto quit;
	default:
		break;
	}

    /* get the delimiter - the first character in wcpattern*/
    /* make wcs now point to the first character in old */
    sep = wc_pattern[0];
    wcs = wc_pattern+1;
    /* set wptr to point to old */
    wptr = wcs;
    retval = 0;

    /* find the second seperator */
    if ((ptr = sepsrch(wcs,sep)) == (wchar_t *)NULL) {
    	warn(MSGSTR(RP_NOADD, "Replacement string not added"),
        	MSGSTR(RP_BADDEL,  "Bad delimeters"));
    	goto quit;
    }

    /* old will point to the search string and be null terminated */
    wcs = ptr;
    *ptr = (wchar_t)L'\0';
    wcstombs(old,wptr,PATH_MAX+1);

    /* set wptr to point to new */
    wptr = ++wcs;

    /* find the third and last seperator */
    if ((ptr = sepsrch(wcs,sep)) == (wchar_t *)NULL) {
	    warn(MSGSTR(RP_NOADD, "Replacement string not added"),
	         MSGSTR(RP_BADDEL,  "Bad delimeters"));
	    goto quit;
    }
    /* new will point to the replacement string and be null terminated */
    wcs = ptr;
    *ptr = (wchar_t)L'\0';
    wcstombs(new,wptr,PATH_MAX+1);

    /* check for trailing g or p options */
    while (*++wcs) {
    	if (*wcs == (wchar_t)L'g')
	    rptr->global = 1;
	else if (*wcs == (wchar_t)L'p')
		rptr->print = 1;
	else {
		warn(MSGSTR(RP_NOADD, "Replacement string not added"),
		     MSGSTR(RP_TRLR, "Invalid trailing RE option"));
		goto quit;
	}
    }

    if ((highbkref = chk_back_ref(wptr)) == -1) {
    	/* ERROR-Invalid back references */
    	warn(MSGSTR(RP_NOADD, "Replacement string not added"),
	     MSGSTR(RP_BADREF, "Invalid RE backreference(s)"));
	goto quit;
    }

    /* Now old points to 'old' and new points to 'new' and both
	 * are '\0' terminated */
    if ((retval = regcomp(&rptr->comp, old, REG_NEWLINE)) != 0) {
	regerror(retval, &rptr->comp, old, sizeof(old));
	warn(MSGSTR(RP_NOADD, "Replacement string not added"),old);
	goto quit;
    }

    if (rptr->comp.re_nsub < highbkref) {
	warn(MSGSTR(RP_NOADD, "Replacement string not added"),
	     MSGSTR(RP_BADREF, "Invalid RE backreference(s)"));
	goto quit;
    }

    rptr->replace = new;
    rptr->next = NULL;
    if (rplhead == (Replstr *)NULL) {
	rplhead = rptr;
	rpltail = rptr;
    } else {
	rpltail->next = rptr;
	rpltail = rptr;
    }
	return;
quit:
    free(rptr);
    free(new);
    return;
}

/*
 * rpl_name - possibly replace a name with a regular expression
 *
 * DESCRIPTION
 *
 *	The string name is searched for in the list of regular expression
 *	substituions.  Whenever the string matches one of the regular
 *	expressions, the string is modified as specified by the user.
 *
 * PARAMETERS
 *
 *	char	*name	- name to search for and possibly modify
 *			  should be a buffer of at least PATH_MAX
 */

void
rpl_name(char *name)
{
	long			offst = 0;
	int			found;
	Replstr			*rptr;
	static char		buff[PATH_MAX + 1];

	static regmatch_t	pmatch[10];
	size_t			nmatch = (sizeof (pmatch) / sizeof (pmatch[0]));


	strcpy(buff, name);

	for (rptr = rplhead, found = 0; !found && rptr; rptr = rptr->next) {
		if (regexec(&rptr->comp, buff, nmatch, pmatch, 0) == 0) {
			found = 1;
			offst = do_regsub(pmatch, buff, 0, rptr->replace);

			while (rptr->global && offst >= 0 &&
			       regexec(&rptr->comp, buff + offst, nmatch,
				       pmatch, REG_NOTBOL) == 0) {
				offst = do_regsub(pmatch, buff, offst,
						  rptr->replace);
			}
		}

		if (found && rptr->print) {
			fprintf(stderr, "%s >> %s\n", name, buff);
		}

		strcpy(name, buff);
	}
}

/*
 *  do_regsub - Substitute the replacement string for the matching
 *		portion of the original, expanding backreferences, etc,
 *		along the way.
 *
 *		Modifies the src parameter, and returns the offset (with
 *		respect to the beginning of the resulting value of src)
 *		of the point where the caller should continue searching.
 *
 *		Or returns -1 if there is a problem.
 */

static long
do_regsub(regmatch_t pmatch[], char *src, long offst, char *replace)
{
	static char	dest[PATH_MAX + 1];
	char		*dstptr;
	const char	*dstend = dest + sizeof (dest) - 1;
	char		*repptr;
	char		*repend;
	int		no;
	long		len;
	wchar_t		c[2] = {0};

	memset(dest, '\0', sizeof (dest));
	strncpy(dest, src, pmatch[0].rm_so  + offst);

	for (dstptr = dest + pmatch[0].rm_so + offst,
	     repptr = repend = replace, repend += strlen(repptr);
	     repptr < repend; ) {
		if ((len = mbtowc(&c[0], repptr, MB_CUR_MAX)) < 0) {
			fprintf(stderr, gettext("%s: bad multibyte "
						"character in "
						"replacement.\n"),
				myname);
			return (-1);
		}

		repptr += len;
		no = -1;

		if (c[0] == (wchar_t) '&') {
			no = 0;
		} else if (c[0] == (wchar_t) '\\') {
			if ((len = mbtowc(&c[0], repptr, MB_CUR_MAX)) < 0) {
				fprintf(stderr, gettext("%s: bad multibyte "
							"character in "
							"replacement.\n"),
					myname);
				return (-1);
			}

			repptr += len;

			if (iswdigit(c[0])) {
				no = watoi(c);
			}
		}

		if (no < 0) {
			if ((len = wctomb(dstptr, c[0])) < 0) {
				fprintf(stderr, gettext("%s: bad wide "
							"character in "
							"replacement.\n"),
					myname);
				return (-1);
			}

			dstptr += len;
		} else if (pmatch[no].rm_so != -1 &&
			   pmatch[no].rm_so != -1) {
			len = pmatch[no].rm_eo - pmatch[no].rm_so;
			strncpy(dstptr, src + offst + pmatch[no].rm_so, len);
			dstptr += len;

			if (len != 0 && *(dstptr - 1) == '\0') {
				warn(MSGSTR(RE_DAMAGE2, "regexp: damaged"
					    "match string"));
				return (-1);

			}
		}
	}

	strcpy(dstptr, src + offst + pmatch[0].rm_eo);
	strcpy(src, dest);
	offst = dstptr - dest;
	return (offst);
}

/* get_disposition - get a file disposition
 *
 * DESCRIPTION
 *
 *	Get a file disposition from the user.  If the user enters 
 *	the locales equivalent of yes the the file is processed, 
 * 	anything else and the file is ignored.
 * 
 * 	The functionality (-y option) is not in 1003.2 as of DRAFT 11 
 * 	but we leave it in just in case.
 * 
 *	If the user enters EOF, then the PAX exits with a non-zero return
 *	status.
 *
 * PARAMETERS
 *
 *	char	*mode	- string signifying the action to be taken on file
 *	char	*name	- the name of the file
 *
 * RETURNS
 *
 *	Returns 1 if the file should be processed, 0 if it should not.
 */


int
get_disposition(int mode, char *name)
{
    char	ans[10];
    char	buf[PATH_MAX + 11];

    if (f_disposition) {
	if (mode == ADD)
	    sprintf(buf, MSGSTR(RP_ADD, "add %s? "), name);
	else if (mode == EXTRACT)
	    sprintf(buf, MSGSTR(RP_EXTRACT, "extract %s? "), name);
	else	/* pass */
	    sprintf(buf, MSGSTR(RP_PASS, "pass %s? "), name);

	if (nextask(buf, ans, sizeof(ans)) == -1) {
	    exit(1);
	}
#if 0
	if (strlen(ans) == 0 || rpmatch(ans) != 1) {
#else /* 0 */
	if (strlen(ans) == 0 || isyesno(ans, 1) != 1) {
#endif /* 0 */
	    return(1);
	} 
    } 
    return(0);
}


/* get_newname - prompt the user for a new filename
 *
 * DESCRIPTION
 *
 *	The user is prompted with the name of the file which is currently
 *	being processed.  The user may choose to rename the file by
 *	entering the new file name after the prompt; the user may press
 *	carriage-return/newline, which will skip the file or the user may
 *	type an 'EOF' character, which will cause the program to stop.
 * 	The user can enter a single period ('.') and the name of the
 * 	file will remain unchanged.
 *
 * PARAMETERS
 *
 *	char	*name		- filename, possibly modified by user
 *	int	size		- size of allowable new filename
 *
 * RETURNS
 *
 *	Returns 0 if successfull, or -1 if an error occurred.
 *
 */


int
get_newname(char *name, int size)
{
    char	buf[PATH_MAX + 11];
    char	orig_name[PATH_MAX + 1];

    if (f_interactive) {
	sprintf(buf, MSGSTR(RP_RENAME, "rename %s? "), name);
	strcpy(orig_name, name);
	if (nextask(buf, name, size) == -1) {
	    exit(1);	/* EOF */
	}
	if (strlen(name) == 0) {
	    return(1);
	}
	if ((name[0] == '.') && (name[1] == '\0'))
	    strcpy(name, orig_name);	/* leave the name the same */
    }
    return(0);
}


/* mem_rpl_name - possibly replace a name with a regular expression
 *
 * DESCRIPTION
 *
 *	The string name is searched for in the list of regular expression
 *	substituions.  If the string matches any of the regular expressions
 *	then a new buffer is returned with the new string.
 *
 * PARAMETERS
 *
 *	char	*name	- name to search for and possibly modify
 * 
 * RETURNS
 * 	an allocated buffer with the new name in it
 */

char *
mem_rpl_name(char *name)
{
	char	namebuf[PATH_MAX + 1];

	if (rplhead == (Replstr *)NULL)
		return(mem_str(name));

	strcpy(namebuf, name);
	rpl_name(namebuf);	/* writes new string in namebuf */
	return (mem_str(namebuf));
}

