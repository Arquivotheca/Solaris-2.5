/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef __RMM_INT_H
#define	__RMM_INT_H

#pragma ident	"@(#)rmm_int.h	1.11	94/11/22 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

struct ident_list {
	char	*i_type;		/* name of file system */
	char	*i_dsoname;		/* name of the shared object */
	char	**i_media;		/* list of appropriate media */
	bool_t	(*i_ident)(int, char *, int *, int);
};


#define A_PREMOUNT	0x01	/* execute action before mounting */

struct action_list {
	int		a_flag;		/* behavior flag */
	char		*a_dsoname;	/* name of the shared object */
	char		*a_media;	/* appropriate media */
	int		a_argc;		/* argc of action arg list */
	char		**a_argv;	/* argv of action arg list */
	bool_t		(*a_action)(struct action_arg **, int, char **);
};

#define	MA_CACHE	0x01	/* mount using cachefs */
#define MA_SUID		0x02	/* mount without nosuid flag */
#define MA_SHARE	0x04	/* export */
#define MA_AUTO		0x08	/* automount using autofs */
#define MA_NETWIDE	0x10	/* automount the export */
#define MA_READONLY	0x20	/* mount it readonly */

struct mount_args {
	char	*ma_namere;	/* regular expression */
	char	*ma_namerecmp;	/* compiled regular expression */
	u_int	ma_flags;
	char	*ma_cacheflags;	/* flags to cachefs mount */
	char	*ma_shareflags;	/* flags to share */
};

extern char	*rmm_dsodir;	/* directory for DSO */
extern char	*rmm_config;	/* config file path */
extern int	rmm_debug;	/* debug flag */

extern char	*prog_name;	/* name of the program */
extern pid_t	prog_pid;	/* pid of the program */

extern struct ident_list 	**ident_list;
extern struct action_list 	**action_list;
extern struct mount_args	**mount_args;

void			*dso_load(char *, char *, int);
void			dprintf(const char *fmt, ...);
char			*rawpath(char *);
void			config_read(void);
char			*sh_to_regex(char *);
void			cache_opts(char *, char *);
char			*getmntpoint(char *);
int			makepath(char *, mode_t);

#define	MAX_ARGC	300
#define	MAX_IDENTS	100
#define	MAX_ACTIONS	500
#define MAX_MOUNTS	100

#define	NULLC		'\0'

/* XXX: temporary ?? */
#ifndef DEFAULT_CACHEDIR
#define DEFAULT_CACHEDIR	"/export/cache"
#endif

#ifdef	__cplusplus
}
#endif

#endif /* __RMM_INT_H */
