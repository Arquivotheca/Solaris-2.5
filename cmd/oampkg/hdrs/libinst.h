/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident	"@(#)libinst.h	1.20	95/01/18 SMI"

#ifndef	__PKG_LIBINST_H__
#define	__PKG_LIBINST_H__

#include <stdio.h>
#include <pkgstrct.h>
#include "install.h"

#define	DEF_NONE_SCR	"i.CompCpio"

/*
 * General purpose return codes used for functions which don't return a basic
 * success or failure. For those functions wherein a yes/no result is
 * possible, then 1 means OK and 0 means FAIL.
 */
#define	RESULT_OK	0x0
#define	RESULT_WRN	0x1
#define	RESULT_ERR	0x2

/* These are the file status indicators for the contents file */
#define	INST_RDY	'+'	/* entry is ready to installf -f */
#define	RM_RDY		'-'	/* entry is ready for removef -f */
#define	NOT_FND		'!'	/* entry (or part of entry) was not found */
#define	SHARED_FILE	'%'	/* using the file server's RO partition */
#define	STAT_NEXT	'@'	/* this is awaiting eptstat */
#define	DUP_ENTRY	'#'	/* there's a duplicate of this */
#define	CONFIRM_CONT	'*'	/* need to confirm contents */
#define	CONFIRM_ATTR	'~'	/* need to confirm attributes */
#define	ENTRY_OK	'\0'	/* entry is a confirmed file */

/* control bits for pkgdbmerg() */
#define	NO_COPY		0x0001
#define	CLIENT_PATHS	0x0002	/* working with a client database */

/* control bits for file verification by class */
#define	DEFAULT		0x0	/* standard full verification */
#define	NOVERIFY	0x1	/* do not verify */
#define	QKVERIFY	0x2	/* do a quick verification instead */

/* control bit for path type to pass to CAS */
#define	DEFAULT		0x0	/* standard server-relative absolute path */
#define	REL_2_CAS	0x1	/* pass pkgmap-type relative path */

/* findscripts() argument */
#define	I_ONLY		0x0	/* find install class action scripts */
#define	R_ONLY		0x1	/* find removal class action scripts */

struct cl_attr {
	char	name[CLSSIZ+1];	/* name of class */
	char	*inst_script;	/* install class action script */
	char	*rem_script;	/* remove class action script */
	unsigned	src_verify:3;	/* source verification level */
	unsigned 	dst_verify:4;	/* destination verification level */
	unsigned	relpath_2_CAS:1;	/* CAS gets relative paths */
};

#if defined(__STDC__)
#define	__P(protos) protos
#else	/* __STDC__ */
#define	__P(protos) ()
#endif	/* __STDC__ */

extern char	*pathdup __P((char *s));
extern char	*pathalloc __P((int n));
extern char	*fixpath __P((char *path));
extern char	*get_info_basedir __P((void));
extern char	*get_basedir __P((void));
extern char	*get_client_basedir __P((void));
extern int	set_basedirs __P((int reloc, char *adm_basedir,
		    char *pkginst, int nointeract));
extern int	eval_path __P((char **server_ptr, char **client_ptr,
		    char **map_ptr, char *path));
extern char	*get_inst_root __P((void));
extern char	*get_mount_point __P((int n));
extern char	*get_remote_path __P((int n));
extern void	set_env_cbdir __P((void));
extern int	set_inst_root __P((char *path));
extern void	put_path_params __P((void));
extern int	mkpath __P((char *p));
extern void	mkbasedir __P((int flag, char *path));
extern int	is_an_inst_root __P((void));
extern int	is_a_basedir __P((void));
extern int	is_a_cl_basedir __P((void));
extern int	is_relocatable __P((void));
extern char	*orig_path __P((char *path));
extern char	*orig_path_ptr __P((char *path));
extern char	*qreason __P((int caller, int retcode, int started));
extern char	*qstrdup __P((char *s));
extern char	*srcpath __P((char *d, char *p, int part, int nparts));
extern int	copyf __P((char *from, char *to, long mytime));
extern int	dockdeps __P((char *depfile, int rflag));
extern int	finalck __P((struct cfent *ept, int attrchg, int contchg));
extern int	fsys __P((char *path));
extern int	is_fs_writeable __P((char *path, int *fsys_value));
extern int	is_remote_fs __P((char *path, int *fsys_value));
extern int	is_fs_writeable_n __P((int n));
extern int	is_remote_fs_n __P((int n));
extern int	isreloc __P((char *pkginstdir));
extern int	ocfile __P((FILE **mapfp, FILE **tmpfp, ulong map_blks));
extern int	pkgdbmerg __P((FILE *mapfp, FILE *tmpfp,
		    struct cfextra **extlist, struct mergstat *mstat,
		    int notify));
extern int	swapcfile __P((FILE *mapfp, FILE *tmpfp, char *pkginst));
#ifdef PRESVR4
extern int	rename __P((char *x, char *y));
#endif	/* PRESVR4 */
extern long	nblk __P((long size, ulong bsize, ulong frsize));
extern struct	cfent **procmap __P((FILE *fp, int mapflag, char *ir));
extern struct	cfextra **procmap_x __P((FILE *fp, int mapflag, char *ir));
extern struct	pinfo *eptstat __P((struct cfent *entry, char *pkg, char c));
/*PRINTFLIKE1*/
extern void	echo __P((char *fmt, ...));
extern void	get_mntinfo __P((void));
extern void	notice __P((int n));
extern void	psvr4cnflct __P((void));
extern void	psvr4mail __P((char *list, char *msg, int retcode, char *pkg));
extern void	psvr4pkg __P((char **ppkg));
/*PRINTFLIKE2*/
extern void	ptext __P((FILE *fp, char *fmt, ...));
extern void	putparam __P((char *param, char *value));
extern void	setadmin __P((char *file));

extern char	*cl_iscript __P((int idx));
extern char	*cl_rscript __P((int idx));
extern void	find_CAS __P((int CAS_type, char *bin_ptr, char *inst_ptr));
extern int	setlist __P((struct cl_attr ***plist, char *slist));
extern void	addlist __P((struct cl_attr ***plist, char *item));
extern char	*cl_nam __P((int cl_idx));
extern char	*flex_device(char *device_name, int dev_ok);
extern int	cl_getn __P((void));
extern int	cl_idx __P((char *cl_nam));
extern void	cl_sets __P((char *slist));
extern void	cl_setl __P((struct cl_attr **cl_lst));
extern void	cl_putl __P((char *parm_name, struct cl_attr **list));
#if defined(lint) && !defined(gettext)
#define	gettext(x)	x
#endif	/* defined(lint) && !defined(gettext) */

#endif	/* __PKG_LIBINST_H__ */
