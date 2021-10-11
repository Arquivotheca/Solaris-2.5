
/*	@(#)rpcdefs.h 1.3 91/12/20	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#ifndef	RPCDEFS_H
#define	RPCDEFS_H

#include <rpc/rpc.h>
/*
 * RPC definitions specific to the database server side
 */

#ifdef __STDC__
extern bool_t	xdr_fastfind(XDR *, struct db_findargs *);
extern bool_t	xdr_unavailable(XDR *);
extern bool_t	xdr_listem(XDR *, struct tapelistargs *);
extern bool_t	xdr_headerlist(XDR *, struct readdata *);
extern bool_t	xdr_mntptlist(XDR *, struct readdata *);
extern bool_t	xdr_dbinfo(XDR *, void *);

extern int		*start_update_1(char **);
extern int		*process_update_1(struct process *);
extern struct readdata	*read_dir_1(struct blk_readargs *);
extern struct readdata	*read_inst_1(struct blk_readargs *);
extern int		*delete_tape_1(char **);
extern struct readdata	*read_dnode_1(struct dnode_readargs *);
extern struct readdata	*read_dnodeblk_1(struct dnode_readargs *);
extern struct readdata	*read_linkval_1(struct dnode_readargs *);
extern struct readdata	*read_header_1(struct header_readargs *);
extern struct readdata	*read_fullheader_1(struct header_readargs *);
extern struct readdata	*read_fsheader_1(struct fsheader_readargs *);
extern struct readdata	*read_fullfsheader_1(struct fsheader_readargs *);
extern struct readdata	*read_tape_1(struct tape_readargs *);
extern struct readdata	*read_dumps_1(struct fsheader_readargs *);
extern struct readdata	*check_mntpt_1(struct fsheader_readargs *);
#else
extern bool_t	xdr_fastfind();
extern bool_t	xdr_unavailable();
extern bool_t	xdr_listem();
extern bool_t	xdr_headerlist();
extern bool_t	xdr_mntptlist();
extern bool_t	xdr_dbinfo();

extern int		*start_update_1();
extern int		*process_update_1();
extern struct readdata	*read_dir_1();
extern struct readdata	*read_inst_1();
extern int		*delete_tape_1();
extern struct readdata	*read_dnode_1();
extern struct readdata	*read_dnodeblk_1();
extern struct readdata	*read_linkval_1();
extern struct readdata	*read_header_1();
extern struct readdata	*read_fullheader_1();
extern struct readdata	*read_fsheader_1();
extern struct readdata	*read_fullfsheader_1();
extern struct readdata	*read_tape_1();
extern struct readdata	*read_dumps_1();
extern struct readdata	*check_mntpt_1();
#endif

#endif
