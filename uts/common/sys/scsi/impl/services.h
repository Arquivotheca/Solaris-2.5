/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_SYS_SCSI_IMPL_SERVICES_H
#define	_SYS_SCSI_IMPL_SERVICES_H

#pragma ident	"@(#)services.h	1.24	95/02/13 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Implementation services not classified by type
 */

#ifdef	_KERNEL

#ifdef	__STDC__

struct scsi_key_strings {
	int key;
	char *message;
};

extern int scsi_poll(struct scsi_pkt *);
extern struct scsi_pkt *get_pktiopb(struct scsi_address *,
    caddr_t *datap, int cdblen, int statuslen,
    int datalen, int readflag, int (*func)());
extern void free_pktiopb(struct scsi_pkt *, caddr_t datap, int datalen);
extern char *scsi_dname(int dtyp);
extern char *scsi_rname(u_char reason);
extern char *scsi_mname(u_char msg);
extern char *scsi_cname(u_char cmd, char **cmdvec);
extern char *scsi_cmd_name(u_char cmd, struct scsi_key_strings *cmdlist,
    char *tmpstr);
extern char *scsi_sname(u_char sense_key);
extern char *scsi_esname(u_int sense_key, char *tmpstr);
extern void scsi_errmsg(struct scsi_device *devp, struct scsi_pkt *pkt,
    char *label, int severity, int blkno, int err_blkno,
    struct scsi_key_strings *cmdlist, struct scsi_extended_sense *sensep);
extern void scsi_log(dev_info_t *dev, char *label,
    u_int level, const char *fmt, ...);

#else	/* __STDC__ */

extern int scsi_poll();
extern struct scsi_pkt *get_pktiopb();
extern void free_pktiopb();
extern char *scsi_dname(), *scsi_rname(), *scsi_cname(), *scsi_mname();
extern char *scsi_sname(), *scsi_esname();
extern void scsi_errmsg(), scsi_log();

#endif	/* __STDC__ */

extern char *scsi_state_bits;
extern char *sense_keys[NUM_SENSE_KEYS + NUM_IMPL_SENSE_KEYS];


#define	SCSI_DEBUG	0xDEB00000

#define	SCSI_ERR_ALL		0
#define	SCSI_ERR_UNKNOWN	1
#define	SCSI_ERR_INFO		2
#define	SCSI_ERR_RECOVERED	3
#define	SCSI_ERR_RETRYABLE	4
#define	SCSI_ERR_FATAL		5
#define	SCSI_ERR_NONE		6


/*
 * Common Capability Strings Array
 */
#define	SCSI_CAP_DMA_MAX		0
#define	SCSI_CAP_MSG_OUT		1
#define	SCSI_CAP_DISCONNECT		2
#define	SCSI_CAP_SYNCHRONOUS		3
#define	SCSI_CAP_WIDE_XFER		4
#define	SCSI_CAP_PARITY			5
#define	SCSI_CAP_INITIATOR_ID		6
#define	SCSI_CAP_UNTAGGED_QING		7
#define	SCSI_CAP_TAGGED_QING		8
#define	SCSI_CAP_ARQ			9
#define	SCSI_CAP_LINKED_CMDS		10
#define	SCSI_CAP_SECTOR_SIZE		11
#define	SCSI_CAP_TOTAL_SECTORS		12
#define	SCSI_CAP_GEOMETRY		13
#define	SCSI_CAP_RESET_NOTIFICATION	14
#define	SCSI_CAP_QFULL_RETRIES		15
#define	SCSI_CAP_QFULL_RETRY_INTERVAL	16

/*
 * Compatibility...
 */

#define	scsi_cmd_decode	scsi_cname

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_IMPL_SERVICES_H */
