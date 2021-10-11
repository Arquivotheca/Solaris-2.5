/*
 *		    (c) 1994-1995  Sun Microsystems, Inc.
 *			   All Rights Reserved
 */

#ident	"@(#)list.c	1.4	95/03/20 SMI"

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
static char rcsid[] = "@(#)$RCSfile: list.c,v $ $Revision: 1.3.3.2 $ (OSF) $Date: 1992/10/30 20:15:42 $";
#endif
/* 
 * list.c - List all files on an archive
 *
 * DESCRIPTION
 *
 *	These function are needed to support archive table of contents and
 *	verbose mode during extraction and creation of achives.
 *
 * AUTHOR
 *
 *	Mark H. Colburn, NAPS International (mark@jhereg.mn.org)
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
 * Revision 1.2  89/02/12  10:04:43  mark
 * 1.2 release fixes
 * 
 * Revision 1.1  88/12/23  18:02:14  mark
 * Initial revision
 * 
 */

/* Headers */

#include <pwd.h>
#include <grp.h>
#include <sys/sysmacros.h>
#include "pax.h"

/* Messages */

#define	LS_SYM		"Unable to read a symbolic link"
#define	LS_LINK		" is linked to %s"
#define	LS_SYMLINK	" is a symbolic link to %s"
#define	LS_XSYM		"x %1$s is a symbolic link to %2$s\n"
#define	LS_LINK2	"%1$s is linked to %2$s\n"
#define	LS_SUM		"x %s, %ld bytes, %d tape blocks\n"
#define	LS_READ		"Unable to read a symbolic link"
#define	LS_ASYM		"a %1$s is a symbolic link to %2$s\n"
#define	LS_LINK3	"is a link to %s\n"
#define	LS_SUM2		"? %s %ld blocks\n"

/* Defines */

/*
 * isodigit returns non zero iff argument is an octal digit, zero otherwise
 */
#define	ISODIGIT(c)	(((c) >= '0') && ((c) <= '7'))

/*
 *  MAX_MONTH is the size needed to accomodate the month-name
 *  abbreviation in any locale (+ 1 for the trailing NUL).  If a value is
 *  defined by the standard at some point, it should be substituted for
 *  this.
 */

#define	MAX_MONTH	6

/* Function Prototypes */


static void cpio_entry(char *, Stat *);
static void tar_entry(char *, Stat *);
static void pax_entry(char *, Stat *);
static void print_mode(ushort);
static long from_oct(int digs, char *where);



/* Internal Identifiers */

static char       *monnames[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};


/* read_header - read a header record
 *
 * DESCRIPTION
 *
 * 	Read a record that's supposed to be a header record. Return its 
 *	address in "head", and if it is good, the file's size in 
 *	asb->sb_size.  Decode things from a file header record into a "Stat". 
 *	Also set "head_standard" to !=0 or ==0 depending whether header record 
 *	is "Unix Standard" tar format or regular old tar format. 
 *
 * PARAMETERS
 *
 *	char   *name		- pointer which will contain name of file
 *	Stat   *asb		- pointer which will contain stat info
 *
 * RETURNS
 *
 * 	Return 1 for success, 0 if the checksum is bad, EOF on eof, 2 for a 
 * 	record full of zeros (EOF marker). 
 */


int
read_header(char *name, Stat *asb)
{
    int             i;
    long            sum;
    long	    recsum;
    Link           *link;
    char           *p;
    char            hdrbuf[BLOCKSIZE];

    memset((char *)asb, 0, sizeof(Stat));

    if (f_append)
	lastheader = bufidx;		/* remember for backup */

    /* read the header from the buffer */
    if (buf_read(hdrbuf, BLOCKSIZE) != 0) {
	return (EOF);
    }

    /*
     * Construct the file name from the prefix and name fields, if
     * necessary.
     */

    name[0] = '\0';

    if (hdrbuf[TO_PREFIX] != '\0') {
        strcat(strncat(name, &hdrbuf[TO_PREFIX], TL_PREFIX), "/");
    }

    strncat(name, &hdrbuf[TO_NAME], TL_NAME);

    recsum = from_oct(8, &hdrbuf[TO_CHKSUM]);
    sum = 0;
    p = hdrbuf;
    for (i = 0 ; i < 500; i++) {

	/*
	 * We can't use unsigned char here because of old compilers, e.g. V7. 
	 */
	sum += 0xFF & *p++;
    }

    /* Adjust checksum to count the "chksum" field as blanks. */
    for (i = 0; i < 8; i++) {
	sum -= 0xFF & hdrbuf[TO_CHKSUM + i];
    }
    sum += ' ' * 8;

    if (sum == 8 * ' ') {

	/*
	 * This is a zeroed record...whole record is 0's except for the 8
	 * blanks we faked for the checksum field. 
	 */
	return (2);
    }
    if (sum == recsum) {
	/*
	 * Good record.  Decode file size and return. 
	 */
	if (hdrbuf[TO_TYPEFLG] != LNKTYPE) {
	    asb->sb_size = from_oct(1 + 12, &hdrbuf[TO_SIZE]);
	}
	asb->sb_mtime = from_oct(1 + 12, &hdrbuf[TO_MTIME]);
	asb->sb_mode = from_oct(8, &hdrbuf[TO_MODE]);
	asb->sb_atime = -1;	/* access time will be 'now' */

	if (strcmp(&hdrbuf[TO_MAGIC], TMAGIC) == 0) {
	    /* Unix Standard tar archive */
	    head_standard = 1;
#ifdef NONAMES
	    asb->sb_uid = from_oct(8, &hdrbuf[TO_UID]);
	    asb->sb_gid = from_oct(8, &hdrbuf[TO_GID]);
#else
	    asb->sb_uid = finduid(&hdrbuf[TO_UNAME]);
	    asb->sb_gid = findgid(&hdrbuf[TO_GNAME]);
#endif
	    switch (hdrbuf[TO_TYPEFLG]) {
	    case BLKTYPE:
	    case CHRTYPE:
		asb->sb_rdev = makedev(from_oct(8, &hdrbuf[TO_DEVMAJOR]),
				      from_oct(8, &hdrbuf[TO_DEVMINOR]));
		break;
	    default:
		/* do nothing... */
		break;
	    }
	} else {
	    /* Old fashioned tar archive */
	    head_standard = 0;
	    asb->sb_uid = from_oct(8, &hdrbuf[TO_UID]);
	    asb->sb_gid = from_oct(8, &hdrbuf[TO_GID]);
	}

	switch (hdrbuf[TO_TYPEFLG]) {
	case REGTYPE:
	case AREGTYPE:
	    /*
	     * Berkeley tar stores directories as regular files with a
	     * trailing /
	     */
	    if (name[strlen(name) - 1] == '/') {
		name[strlen(name) - 1] = '\0';
		asb->sb_mode |= S_IFDIR;
	    } else {
		asb->sb_mode |= S_IFREG;
	    }
	    break;
	case LNKTYPE:
	    asb->sb_nlink = 2;
	    /*
	     * We need to save the linkname so that it is available later
	     * when we have to search the link chain for this link.
	     */
	    asb->linkname = mem_rpl_name(&hdrbuf[TO_LINKNAME]);

	    linkto(&hdrbuf[TO_LINKNAME], asb);	/* don't use linkname here */
	    linkto(name, asb);
	    asb->sb_mode |= S_IFREG;
	    break;
	case BLKTYPE:
	    asb->sb_mode |= S_IFBLK;
	    break;
	case CHRTYPE:
	    asb->sb_mode |= S_IFCHR;
	    break;
	case DIRTYPE:
	    asb->sb_mode |= S_IFDIR;
	    break;
#ifdef S_IFLNK
	case SYMTYPE:
	    asb->sb_mode |= S_IFLNK;
	    strncpy(asb->sb_link, &hdrbuf[TO_LINKNAME], TL_LINKNAME);
	    break;
#endif
#ifdef S_IFIFO
	case FIFOTYPE:
	    asb->sb_mode |= S_IFIFO;
	    break;
#endif
#ifdef S_IFCTG
	case CONTTYPE:
	    asb->sb_mode |= S_IFCTG;
	    break;
#endif
	}
	return (1);
    }
    return (0);
}


/* print_entry - print a single table-of-contents entry
 *
 * DESCRIPTION
 * 
 *	Print_entry prints a single line of file information.  The format
 *	of the line is the same as that used by the LS command.  For some
 *	archive formats, various fields may not make any sense, such as
 *	the link count on tar archives.  No error checking is done for bad
 *	or invalid data.
 *
 * PARAMETERS
 *
 *	char   *name		- pointer to name to print an entry for
 *	Stat   *asb		- pointer to the stat structure for the file
 */


void
print_entry(char *name, Stat *asb)
{
    switch (ar_interface) {
    case TAR:
	tar_entry(name, asb);
	break;
    case CPIO:
	cpio_entry(name, asb);
	break;
    case PAX: pax_entry(name, asb);
	break;
    }
}


/* cpio_entry - print a verbose cpio-style entry
 *
 * DESCRIPTION
 *
 *	Print_entry prints a single line of file information.  The format
 *	of the line is the same as that used by the traditional cpio 
 *	command.  No error checking is done for bad or invalid data.
 *
 * PARAMETERS
 *
 *	char   *name		- pointer to name to print an entry for
 *	Stat   *asb		- pointer to the stat structure for the file
 */


static void
cpio_entry(char *name, Stat *asb)
{
	struct tm      *atm;
	Link	       *from;
	struct passwd  *pwp;
	struct group   *grp;
	char	       mon[MAX_MONTH]; /* abreviated month name */

	if (f_list && f_verbose) {
		fprintf(msgfile, "%-7o", asb->sb_mode);
		atm = localtime(&asb->sb_mtime);
		if (pwp = getpwuid((int) USH(asb->sb_uid))) {
			fprintf(msgfile, "%-6s", pwp->pw_name);
		} else {
			fprintf(msgfile, "%-6u", USH(asb->sb_uid));
		}
		(void)strftime(mon, sizeof(mon), "%b", atm);
		fprintf(msgfile,
			dcgettext(NULL, "%7ld  %3s %2d %02d:%02d:%02d %4d  ",
				  LC_TIME),
			asb->sb_size, mon, 
			atm->tm_mday, atm->tm_hour, atm->tm_min, 
			atm->tm_sec, atm->tm_year + 1900);
	}
	fprintf(msgfile, "%s", name);
	if ((asb->sb_nlink > 1) && (from = islink(name, asb))) {
		fprintf(msgfile, MSGSTR(LS_LINK, " linked to %s"), from->l_name);
	}
#ifdef	S_IFLNK
	if ((asb->sb_mode & S_IFMT) == S_IFLNK) {
		fprintf(msgfile, MSGSTR(LS_SYMLINK, " symbolic link to %s"), asb->sb_link);
	}
#endif /* S_IFLNK */
	putc('\n', msgfile);
}


/* tar_entry - print a tar verbose mode entry
 *
 * DESCRIPTION
 *
 *	Print_entry prints a single line of tar file information.  The format
 *	of the line is the same as that produced by the traditional tar 
 *	command.  No error checking is done for bad or invalid data.
 *
 * PARAMETERS
 *
 *	char   *name		- pointer to name to print an entry for
 *	Stat   *asb		- pointer to the stat structure for the file
 */


static void
tar_entry(char *name, Stat *asb)
{
	struct tm	*atm;
	int		i;
	int		mode;
	char            *symnam = "NULL";
	Link            *link;
	char		mon[MAX_MONTH];		/* abbreviated month name */
	
	if ((mode = asb->sb_mode & S_IFMT) == S_IFDIR) {
		return;		/* don't print directories */
	}
	if (f_extract) {
		switch (mode) {
#ifdef S_IFLNK
		case S_IFLNK: 	/* This file is a symbolic link */
			i = readlink(name, symnam, PATH_MAX);
			if (i < 0) { /* Could not find symbolic link */
				warn(MSGSTR(LS_SYM,
					    "can't read symbolic link"),
				     strerror(errno));
			} else { /* Found symbolic link filename */
				symnam[i] = '\0';
				fprintf(msgfile, MSGSTR(LS_XSYM, "x %s "
							"symbolic link to "
							"%s\n"),
					name,
					symnam);
			}
			break;
#endif
		case S_IFREG: 	/* It is a link or a file */
			if ((asb->sb_nlink > 1)
			    && (link = islink(name, asb))) {
				fprintf(msgfile,
					MSGSTR(LS_LINK2,
					       "%s linked to %s\n"),
					name, link->l_name); 
			} else {
				fprintf(msgfile,
					MSGSTR(LS_SUM,"x %s, %ld bytes, "
					       "%d tape blocks\n"),
					name, asb->sb_size,
					ROUNDUP(asb->sb_size,
						BLOCKSIZE) / BLOCKSIZE);
			}
		}
	} else if (f_append || f_create) {
		switch (mode) {
#ifdef S_IFLNK
		case S_IFLNK: 	/* This file is a symbolic link */
			i = readlink(name, symnam, PATH_MAX);
			if (i < 0) { /* Could not find symbolic link */
				warn(MSGSTR(LS_READ, "can't read symbolic link"), strerror(errno));
			} else { /* Found symbolic link filename */
				symnam[i] = '\0';
				fprintf(msgfile, MSGSTR(LS_ASYM, "a %s symbolic link to %s\n"), name, symnam);
			}
			break;
#endif
		case S_IFREG: 	/* It is a link or a file */
			fprintf(msgfile, "a %s ", name);
			if ((asb->sb_nlink > 1) && (link = islink(name, asb))) {
				fprintf(msgfile, MSGSTR(LS_LINK3, "link to %s\n"), link->l_name); 
			} else {
				fprintf(msgfile, MSGSTR(BLOCKS, "%ld Blocks\n"), 
					ROUNDUP(asb->sb_size, BLOCKSIZE) / BLOCKSIZE);
			}
			break;
		}
	} else if (f_list) {
		if (f_verbose) {
			atm = localtime(&asb->sb_mtime);
			(void)strftime(mon, sizeof(mon), "%b", atm);
			print_mode(asb->sb_mode);
			fprintf(msgfile,
				dcgettext(NULL,
					  " %d/%d %6d %3s "
					  "%2d %02d:%02d %4d %s",
					  LC_TIME),
				asb->sb_uid, asb->sb_gid, asb->sb_size,
				mon, atm->tm_mday, atm->tm_hour, 
				atm->tm_min, atm->tm_year + 1900, name);
		} else {
			fprintf(msgfile, "%s", name);
		}
		switch (mode) {
#ifdef S_IFLNK
		case S_IFLNK: 	/* This file is a symbolic link */
			i = readlink(name, symnam, PATH_MAX);
			if (i < 0) { /* Could not find symbolic link */
				warn(MSGSTR(LS_READ, "can't read symbolic "
					    "link"), strerror(errno));
			} else { /* Found symbolic link filename */
				symnam[i] = '\0';
				fprintf(msgfile,
					MSGSTR(LS_SYMLINK, " symbolic "
					       "link to %s"), symnam);
			}
			break;
#endif
		case S_IFREG: 	/* It is a link or a file */
			if ((asb->sb_nlink > 1) && (link = islink(name,
								  asb))) {
				fprintf(msgfile, MSGSTR(LS_LINK,
							" linked to %s"),
					link->l_name);
			}
			break;	/* Do not print out directories */
		}
		fputc('\n', msgfile);
	} else {
		fprintf(msgfile, MSGSTR(LS_SUM2, "? %s %ld blocks\n"), name,
			ROUNDUP(asb->sb_size, BLOCKSIZE) / BLOCKSIZE);
	}
}


/* pax_entry - print a verbose cpio-style entry
 *
 * DESCRIPTION
 *
 *	Print_entry prints a single line of file information.  The format
 *	of the line is the same as that used by the LS command.  
 *	No error checking is done for bad or invalid data.
 *
 * PARAMETERS
 *
 *	char   *name		- pointer to name to print an entry for
 *	Stat   *asb		- pointer to the stat structure for the file
 */


static void
pax_entry(char *name, Stat *asb)
{
    struct tm	       *atm;
    Link	       *from;
    struct passwd      *pwp;
    struct group       *grp;
    char		mon[MAX_MONTH];		/* abbreviated month name */
    time_t		six_months_ago;

    if (f_list && f_verbose) {
	print_mode(asb->sb_mode);
	fprintf(msgfile, " %2d", asb->sb_nlink);
	atm = localtime(&asb->sb_mtime);
	six_months_ago = now - 6L*30L*24L*60L*60L; /* 6 months ago */
	(void) strftime(mon, sizeof(mon), "%b", atm);
	if (pwp = getpwuid((int) USH(asb->sb_uid))) {
	    fprintf(msgfile, " %-8s", pwp->pw_name);
	} else {
	    fprintf(msgfile, " %-8u", USH(asb->sb_uid));
	}
	if (grp = getgrgid((int) USH(asb->sb_gid))) {
	    fprintf(msgfile, " %-8s", grp->gr_name);
	} else {
	    fprintf(msgfile, " %-8u", USH(asb->sb_gid));
	}
	switch (asb->sb_mode & S_IFMT) {
	case S_IFBLK:
	case S_IFCHR:
	    fprintf(msgfile, "\t%3d, %3d",
		           major(asb->sb_rdev), minor(asb->sb_rdev));
	    break;
	case S_IFREG:
	    fprintf(msgfile, "\t%8ld", asb->sb_size);
	    break;
	default:
	    fprintf(msgfile, "\t        ");
	}
	if ((asb->sb_mtime < six_months_ago) || (asb->sb_mtime > now)) {
	    fprintf(msgfile, dcgettext(NULL, " %3s %2d  %4d ", LC_TIME),
	            mon, atm->tm_mday, 
		    atm->tm_year + 1900);
	} else {
	    fprintf(msgfile," %3s %2d %02d:%02d ",
	            mon, atm->tm_mday, 
		    atm->tm_hour, atm->tm_min);
	}
	fprintf(msgfile, "%s", name);
	if ((asb->sb_nlink > 1) && (from = islink(name, asb))) {
	    fprintf(msgfile, " == %s", from->l_name);
	}
#ifdef	S_IFLNK
	if ((asb->sb_mode & S_IFMT) == S_IFLNK) {
	    fprintf(msgfile, " -> %s", asb->sb_link);
	}
#endif	/* S_IFLNK */
	putc('\n', msgfile);
    } else
	fprintf(msgfile, "%s\n", name);
}


/* print_mode - fancy file mode display
 *
 * DESCRIPTION
 *
 *	Print_mode displays a numeric file mode in the standard unix
 *	representation, ala ls (-rwxrwxrwx).  No error checking is done
 *	for bad mode combinations.  FIFOS, sybmbolic links, sticky bits,
 *	block- and character-special devices are supported if supported
 *	by the hosting implementation.
 *
 * PARAMETERS
 *
 *	ushort	mode	- The integer representation of the mode to print.
 */


static void
print_mode(ushort mode)
{
    /* Tar does not print the leading identifier... */
    if (ar_interface != TAR) {
	switch (mode & S_IFMT) {
	case S_IFDIR: 
	    putc('d', msgfile); 
	    break;
#ifdef	S_IFLNK
	case S_IFLNK: 
	    putc('l', msgfile); 
	    break;
#endif	/* S_IFLNK */
	case S_IFBLK: 
	    putc('b', msgfile); 
	    break;
	case S_IFCHR: 
	    putc('c', msgfile); 
	    break;
#ifdef	S_IFIFO
	case S_IFIFO: 
	    putc('p', msgfile); 
	    break; 
#endif	/* S_IFIFO */ 
	case S_IFREG: 
	default:
	    putc('-', msgfile); 
	    break;
	}
    }
    putc(mode & 0400 ? 'r' : '-', msgfile);
    putc(mode & 0200 ? 'w' : '-', msgfile);
    putc(mode & 0100
	 ? mode & 04000 ? 's' : 'x'
	 : mode & 04000 ? 'S' : '-', msgfile);
    putc(mode & 0040 ? 'r' : '-', msgfile);
    putc(mode & 0020 ? 'w' : '-', msgfile);
    putc(mode & 0010
	 ? mode & 02000 ? 's' : 'x'
	 : mode & 02000 ? 'S' : '-', msgfile);
    putc(mode & 0004 ? 'r' : '-', msgfile);
    putc(mode & 0002 ? 'w' : '-', msgfile);
    putc(mode & 0001
	 ? mode & 01000 ? 't' : 'x'
	 : mode & 01000 ? 'T' : '-', msgfile);
}


/* from_oct - quick and dirty octal conversion
 *
 * DESCRIPTION
 *
 *	From_oct will convert an ASCII representation of an octal number
 *	to the numeric representation.  The number of characters to convert
 *	is given by the parameter "digs".  If there are less numbers than
 *	specified by "digs", then the routine returns -1.
 *
 * PARAMETERS
 *
 *	int digs	- Number to of digits to convert 
 *	char *where	- Character representation of octal number
 *
 * RETURNS
 *
 *	The value of the octal number represented by the first digs
 *	characters of the string where.  Result is -1 if the field 
 *	is invalid (all blank, or nonoctal). 
 *
 * ERRORS
 *
 *	If the field is all blank, then the value returned is -1.
 *
 */


static long
from_oct(int digs, char *where)
{
    long            value;

    while (isspace(*where)) {	/* Skip spaces */
	where++;
	if (--digs <= 0) {
	    return(-1);		/* All blank field */
	}
    }
    value = 0;
    while (digs > 0 && ISODIGIT(*where)) {	/* Scan til nonoctal */
	value = (value << 3) | (*where++ - '0');
	--digs;
    }

    if (digs > 0 && *where && !isspace(*where)) {
	return(-1);		/* Ended on non-space/nul */
    }
    return(value);
}
