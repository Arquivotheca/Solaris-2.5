
/*	@(#)dbserv.h 1.10 91/12/20	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

/*
 * RPC definitions common to server and client
 */

#ifndef DBSERV_H
#define	DBSERV_H

#include <rpc/rpc.h>

#define	MAXDBNAMELEN 1024

/*
 * return values for read operations
 */
#define	DBREAD_SUCCESS		0	/* requested data returned */
#define	DBREAD_NOHOST		1	/* requested host not in this DB */
#define	DBREAD_NODUMP		2	/* requested dump doesn't exist */
#define	DBREAD_NEWDATA		3	/* client cache may be out of date */
#define	DBREAD_SERVERDOWN	4	/* server unavailable */
#define	DBREAD_USERERROR	5	/* invalid user request */
#define	DBREAD_INTERNALERROR	6	/* unexpected database failure */
#define	DBREAD_NOTAPE		7	/* requested tape rec not found */

struct blast_arg {
	FILE	*fp;
	u_long	handle;
};

typedef char *nametype;

struct process {
	int	handle;
	int	filesize;
};
typedef struct process process;

struct blk_readargs {
	char	*host;
	u_long	recnum;
	int	blksize;
	long	cachetime;
};

struct readdata {
	int	readrc;		/* see defines above */
	int	blksize;	/* size of *retdata */
	char	*retdata;	/* pointer to returned data */
				/* this will actually be a dir_block, */
				/* instance_rec, dnode, header, etc   */
};

struct dnode_readargs {
	char *host;
	u_long	dumpid;
	u_long	recnum;
};

struct header_readargs {
	char *host;
	u_long	dumpid;
};

struct fsheader_readargs {
	char	*host;
	char	*mntpt;
	long	time;	/* XXX: time_t */
};

struct tape_readargs {
	char *label;
};

#define	MAXGROUPS	20
struct db_findargs {
	char	*host;
	int	opaque_mode;
	char	*arg;
	int	expand;
	char	*curdir;
	long	timestamp;
	char	*myhost;
	long	uid;
	long	gid;
	int	ngroups;
	int	gidlist[MAXGROUPS];
};

struct tapelistargs {
	char	*label;
	int	verbose;
};

#define	DNODE_READBLKSIZE	10

#ifdef __STDC__
extern bool_t xdr_nametype(XDR *, nametype *);
extern bool_t xdr_process(XDR *, process *);
extern bool_t xdr_datafile(XDR *, struct blast_arg *);
extern bool_t xdr_blkread(XDR *, struct blk_readargs *);

extern bool_t xdr_dnodeargs(XDR *, struct dnode_readargs *);
extern bool_t xdr_headerargs(XDR *, struct header_readargs *);
extern bool_t xdr_fsheaderargs(XDR *, struct fsheader_readargs *);
extern bool_t xdr_tapeargs(XDR *, struct tape_readargs *);
extern bool_t xdr_dbfindargs(XDR *, struct db_findargs *);
extern bool_t xdr_tapelistargs(XDR *, struct tapelistargs *);

extern bool_t xdr_dirread(XDR *, struct readdata *);
extern bool_t xdr_instread(XDR *, struct readdata *);
extern bool_t xdr_dnodeblkread(XDR *, struct readdata *);
extern bool_t xdr_dnoderead(XDR *, struct readdata *);
extern bool_t xdr_dheaderread(XDR *, struct readdata *);
extern bool_t xdr_fullheaderread(XDR *, struct readdata *);
extern bool_t xdr_acttaperead(XDR *, struct readdata *);
extern bool_t xdr_tapelabel(XDR *, char **);
extern bool_t xdr_linkval(XDR *, struct readdata *);
#else
extern bool_t xdr_nametype();
extern bool_t xdr_process();
extern bool_t xdr_datafile();
extern bool_t xdr_blkread();

extern bool_t xdr_dnodeargs();
extern bool_t xdr_headerargs();
extern bool_t xdr_fsheaderargs();
extern bool_t xdr_tapeargs();
extern bool_t xdr_dbfindargs();
extern bool_t xdr_tapelistargs();

extern bool_t xdr_dirread();
extern bool_t xdr_instread();
extern bool_t xdr_dnodeblkread();
extern bool_t xdr_dnoderead();
extern bool_t xdr_dheaderread();
extern bool_t xdr_fullheaderread();
extern bool_t xdr_acttaperead();
extern bool_t xdr_tapelabel();
extern bool_t xdr_linkval();
#endif

#define	DBSERV ((u_long)100089)
#define	DBVERS ((u_long)1)
#define	START_UPDATE ((u_long)1)
#define	UPDATE_DATA ((u_long)2)
#define	PROCESS_UPDATE ((u_long)3)
#define	BLAST_FILE	((u_long)4)
#define	READ_DIR	((u_long)5)
#define	READ_INST	((u_long)6)
#define	READ_DNODE	((u_long)7)
#define	DELETE_BYTAPE	((u_long)8)
#define	READ_DNODEBLK	((u_long)9)
#define	READ_HEADER	((u_long)10)
#define	READ_TAPE	((u_long)11)
#define	READ_FULLHEADER	((u_long)12)
#define	QUIESCE_OPERATION	((u_long)13)
#define	RESUME_OPERATION	((u_long)14)
#define	READ_FSHEADER	((u_long)15)
#define	READ_FULLFSHEADER	((u_long)16)
#define	READ_DUMPS		((u_long)17)
#define	DB_FIND			((u_long)18)
#define	READ_LINKVAL		((u_long)19)
#define	DB_TAPELIST		((u_long)20)
#define	CHECK_MNTPT		((u_long)21)
#define	DB_DBINFO		((u_long)22)
#endif
