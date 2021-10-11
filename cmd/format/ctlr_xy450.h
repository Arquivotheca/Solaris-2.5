
/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ifndef	_CTLR_XY450_H
#define	_CTLR_XY450_H

#pragma ident	"@(#)ctlr_xy450.h	1.5	93/09/07 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 *	Local prototypes for ANSI C compilers
 */
int	xy_rdwr(int, int, daddr_t, int, caddr_t, int);
int	xy_ck_format(void);
int	xy_format(daddr_t, daddr_t, struct defect_list *);
int	xy_ex_man(struct defect_list *);
int	xy_ex_cur(struct defect_list *);
int	xy_repair(daddr_t, int);
int	xy_fixblk(daddr_t, int, int hdr_cnt, void *header_buf, int);
int	xy_isbad(int cyl, int, int);
int	xy_addbad(daddr_t, daddr_t *);
struct xyerror *xy_finderr(u_int errno);
void	xy_printerr(u_int severity, u_int type, char *msg, u_int cmd, daddr_t);
int	xycmd(int, struct hdk_cmd *cmdblk, int);
u_int	xy_match(u_int hdr);
int	xy_index2ls(int, u_int *);
int	xy_ls2index(int ls, u_int *);
int	xy_check_skew(int, void *);
int	xy_readhdr(daddr_t, int, char *);
int	xy_writehdr(daddr_t, int, void *);
void	xy_getbad(daddr_t, int, void *, struct defect_list *);
int	xy_getdefs(int cyl, int, struct defect_list *);
void	xy_reverse(u_char *, int);
void  	check_xy450_drive_type(void);
int   	want_xy450_drive_type_change(daddr_t, daddr_t);
int   	change_xy450_drive_type(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _CTLR_XY450_H */
