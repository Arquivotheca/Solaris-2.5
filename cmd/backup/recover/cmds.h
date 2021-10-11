
/*	@(#)cmds.h 1.7 91/12/20	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#define	CMD_LS		1
#define	CMD_LL		2
#define	CMD_CD		3
#define	CMD_PWD		4
#define	CMD_VERSIONS	5
#define	CMD_SETHOST	6
#define	CMD_SETDATE	7
#define	CMD_ADD		8
#define	CMD_LCD		9
#define	CMD_LPWD	10
#define	CMD_LIST	11
#define	CMD_EXTRACT	12
#define	CMD_DELETE	13
#define	CMD_ADDNAME	14
#define	CMD_SHOWDUMP	15
#define	CMD_XRESTORE	16
#define	CMD_RRESTORE	17
#define	CMD_SHOWSET	18
#define	CMD_NOTIFY	19
#define	CMD_FASTRECOVER	20
#define	CMD_SETDB	21
#define	CMD_FASTFIND	22
#define	CMD_SETMODE	23
#define	CMD_HELP	24

#define	CMD_AMBIGUOUS	96
#define	CMD_NULL	97
#define	CMD_INVAL	98
#define	CMD_QUIT	99

/*
 * the modes for performing dnode lookups.
 * DEFAULT: either opaque or translucent as set by user
 * OPAQUE: opaque (don't see beyond prior level 0) regardless of user setting
 * TRANSLUCENT: translucent (see back to beginning of DB) regardless of
 * user setting.
 */
#define	LOOKUP_DEFAULT		0
#define	LOOKUP_OPAQUE		1
#define	LOOKUP_TRANSLUCENT	2

/*
 * description of each command.  There is a statically
 * initialized array of these in `cmdtab.c'
 */
struct cmdinfo {
	int minlen;	/* minimum length that uniquely identifies */
	char *name;	/* the command name */
	int id;		/* as defined above */
	int minargs;	/* minimum # of arguments allowed */
	int maxargs;	/* maximum # of arguments allowed */
	int redir;	/* redirections allowed */
#define	REDIR_INPUT	1
#define	REDIR_OUTPUT	2
};

/*
 * The routines in `args.c' turn input lines into an argument list
 * of these structures.
 */
struct afile {
	u_long dir_blknum;
	struct dir_block *dbp;
	struct dir_entry *dep;
	char *name;
	int  expanded;
};

struct arglist {
	struct afile *head;
	struct afile *last;
	struct afile *base;
	int nent;
};

#ifdef __STDC__
extern int getcmd(char *, int *, char *, char *, char *, struct arglist *,
	char *, time_t);
extern void freeargs(struct arglist *);
extern void change_directory(char *, char *, int, struct afile *, time_t);
extern void showversions(char *, char *, int, struct arglist *);
extern void multifile_ls(char *, struct arglist *, time_t, int);
extern void listfiles(char *, struct afile *, time_t, int, char *);
extern void addfiles(char *, char *, struct arglist *, char *, time_t);
extern void deletefiles(char *, struct arglist *, time_t);
extern void showdump(char *, struct arglist *, time_t);
extern void fullrestore(char *, struct arglist *, char *, time_t, int, int);
#else
extern int getcmd();
extern void freeargs();
extern void change_directory();
extern void showversions();
extern void multifile_ls();
extern void listfiles();
extern void addfiles();
extern void deletefiles();
extern void showdump();
extern void fullrestore();
#endif
