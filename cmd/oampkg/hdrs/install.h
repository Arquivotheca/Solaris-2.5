/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/
/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident	"@(#)install.h	1.13	95/02/01 SMI"	/* SVr4.0 1.3	*/

#ifndef __PKG_INSTALL_H__
#define	__PKG_INSTALL_H__

#include <sys/types.h>
#include <limits.h>
#include <pkgstrct.h>

/* Settings for procedure scripts */
#define	PROC_USER	"root"
#define	PROC_GRP	"other"
#define	PROC_STDIN	"/dev/null"
#define	PROC_XSTDIN	"/dev/tty"
#define	PROC_STDOUT	"/dev/tty"

/* Settings for class action scripts */
#define	CAS_USER	"root"
#define	CAS_GRP		"other"
#define	CAS_STDIN	"/dev/null"
#define	CAS_STDOUT	"/dev/tty"

/* Settings for non-privileged scripts */
#define	CHK_USER	"install"
#define	CHK_USER_ALT	"nobody"		/* alternate non-priv user */
#define	CHK_USER_NON	"root"		/* user for non-compliant pkg's */
#define	CHK_GRP		"other"
#define	CHK_STDIN	"/dev/null"
#define	CHK_STDOUT	"/dev/tty"

#define	OAMBASE	"/usr/sadm/sysadm"
#define	MAILCMD	"/usr/bin/mail"
#define	DATSTRM	"datastream"
#define	SHELL	"/sbin/sh"
#define	PKGINFO	"pkginfo"
#define	PKGMAP	"pkgmap"

/* Additional cfent/cfextra codes. */
#define	BADFSYS		(-1)	/* an fsys is needed */
#define	BADINDEX	(-1)	/* pkg class idx not yet set */

struct mergstat {
	char	setuid;		/* pkgmap entry has setuid */
	char	setgid;		/* ... and/or setgid bit set */
	char	contchg;	/* contents of the files are different */
	char	attrchg;	/* attributes are different */
	char	shared;		/* more than one pkg associated with this */
	char	osetuid;	/* an installed setuid or setgid process ... */
	char	osetgid;	/* ... is being overwritten by this pkg. */
	char	rogue;		/* conflicting file belonging to no package */
};

/* This holds admin file data. */
struct admin {
	char	*mail;
	char	*instance;
	char	*partial;
	char	*runlevel;
	char	*idepend;
	char	*rdepend;
	char	*space;
	char	*setuid;
	char	*conflict;
	char	*action;
	char	*basedir;
};


struct fstable {
	char	*name;  /* name of filesystem, (mount point) */
	int	namlen; /* The length of the name (mountpoint) */
	u_long	bsize;	/* fundamental file system block size */
	u_long	frsize;	/* file system fragment size */
	u_long	bfree;	/* total # of free blocks */
	u_long	bused;	/* total # of free blocks */
	u_long	ffree;	/* total # of free file nodes */
	u_long	fused;	/* total # of free file nodes */
	short	writeable;  /* access permission, 1=read/write, 0=read */
	char	*fstype; /* type of filesystem - nfs, lo, ... */
	char	*remote_name; /* client's mounted filesystem */
};

/*
 * This is information required by pkgadd for fast operation. A
 * cfextra struct is tagged to each cfent structure requiring
 * processing. This is how we avoid some unneeded repetition. The
 * entries incorporating the word 'local' refer to the path that
 * gets us to the delivered package file. In other words, to install
 * a file we usually copy from 'local' to 'path' below. In the case
 * of a link, where no actual copying takes place, local is the source
 * of the link. Note that environment variables are not evaluated in
 * the locals unless they are links since the literal path is how
 * pkgadd finds the entry under the reloc directory.
 */
struct cfextra {
	struct cfent cf_ent;	/* basic contents file entry */
	int	fsys_value;	/* fstab[] entry index */
	char	*client_path;	/* the client-relative path */
	char	*server_path;	/* the server-relative path */
	char	*map_path;	/* as read from the pkgmap */
	char	*client_local;	/* client_relative local */
	char	*server_local;	/* server relative local */
};

#define	ADM(x, y)	((adm.x != NULL) && (y != NULL) && \
			    strcmp(adm.x, y) == 0)
#define	RELATIVE(x)	(x[0] != '/')

#if defined(lint) && !defined(gettext)
#define	gettext(x)	x
#endif	/* defined(lint) && !defined(gettext) */

#endif	/* __PKG_INSTALL_H__ */
