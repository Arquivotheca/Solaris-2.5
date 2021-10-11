
/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ifndef _CTLR_XD7053_H
#define	_CTLR_XD7053_H

#pragma ident	"@(#)ctlr_xd7053.h	1.4	93/03/25 SMI"

#ifdef	__cplusplus
extern "C" {
#endif


int	xd_rdwr(int dir, int file, daddr_t blkno, int secnt,
		caddr_t bufaddr, int flags);
int	xd_ck_format(void);
int	xd_format(daddr_t start, daddr_t end, struct defect_list *list);
int	xd_ex_man(struct defect_list *list);
int	xd_ex_cur(struct defect_list *list);
int	xd_repair(daddr_t blkno, int flag);
int	xd_create_man(struct defect_list *list);
int	xd_fixblk(daddr_t bn, int index, int hdr_cnt, void *header_buf,
		int flags);
int	xd_isbad(int cyl, int head, int sect);
int	xd_addbad(daddr_t bn, daddr_t *newbn);
struct	xderror *xd_finderr(int errno);
void	xd_printerr(int severity, int type, char *msg, int cmd,
		daddr_t blkno);
int	xdcmd(int file, struct hdk_cmd *cmdblk, int flags);
u_int	xd_match(u_int hdr);
int	xd_index2ls(int index, u_int *buf);
int	xd_ls2index(int ls, u_int *buf);
int	xd_check_spare(int cnt, void *buf);
int	xd_readhdr(daddr_t bn, int cnt, void *buf);
int	xd_writehdr(daddr_t bn, int cnt, void *buf);
void	xd_getbad(daddr_t bn, int cnt, void *buf,
		struct defect_list *list);
int	xd_getdefs(int cyl, int head, struct defect_list *list);
int	xd_putdefs(int cyl, int head, struct defect_list *list);
void	setup_defect_info(int cyl, int head, u_char *ptr,
		struct defect_list *list);
int	xd_init_trk(int cyl, int head);
int	xd_write_defect(int cyl, int head, u_char *ptr);
int	xd_write_ext_defect(int cyl, int head, u_char *ptr);
int	xd_read_defect(int cyl, int head, u_char *ptr);
int	xd_read_ext_defect(int cyl, int head, u_char *ptr);
int	check_defect_list(struct defect_list *list);
struct	defect_entry *xd_get_next_defect(int cyl, int head,
		struct defect_list *list, struct defect_entry *def);

#ifdef	__cplusplus
}
#endif

#endif /* _CTLR_XD7053_H */
