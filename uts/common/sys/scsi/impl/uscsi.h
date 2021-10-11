/*
 * Copyright (c) 1990 Sun Microsystems, Inc.
 */

/*
 * Defines for user SCSI commands					*
 */

#ifndef _SYS_SCSI_IMPL_USCSI_H
#define	_SYS_SCSI_IMPL_USCSI_H

#pragma ident	"@(#)uscsi.h	1.12	95/02/16 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * definition for user-scsi command structure
 */
struct uscsi_cmd {
	int	uscsi_flags;		/* read, write, etc. see below */
	short	uscsi_status;		/* resulting status  */
	short	uscsi_timeout;		/* Command Timeout */
	caddr_t	uscsi_cdb;		/* cdb to send to target */
	caddr_t	uscsi_bufaddr;		/* i/o source/destination */
	u_int	uscsi_buflen;		/* size of i/o to take place */
	u_int	uscsi_resid;		/* resid from i/o operation */
	u_char	uscsi_cdblen;		/* # of valid cdb bytes */
	u_char	uscsi_rqlen;		/* size of uscsi_rqbuf */
	u_char	uscsi_rqstatus;		/* status of request sense cmd */
	u_char	uscsi_rqresid;		/* resid of request sense cmd */
	caddr_t	uscsi_rqbuf;		/* request sense buffer */
	void   *uscsi_reserved_5;	/* Reserved for Future Use */
};

/*
 * flags for uscsi_flags field
 */
#define	USCSI_WRITE	0x00000	/* send data to device */
#define	USCSI_SILENT	0x00001	/* no error messages */
#define	USCSI_DIAGNOSE	0x00002	/* fail if any error occurs */
#define	USCSI_ISOLATE	0x00004	/* isolate from normal commands */
#define	USCSI_READ	0x00008	/* get data from device */
#define	USCSI_ASYNC	0x01000	/* Set bus to asynchronous mode */
#define	USCSI_SYNC	0x02000	/* Return bus to sync mode if possible */
#define	USCSI_RESET	0x04000	/* Reset target */
#define	USCSI_RESET_ALL	0x08000	/* Reset all targets */
#define	USCSI_RQENABLE	0x10000	/* Enable Request Sense extensions */

/*
 * the following flags should not be used at user level but may
 * be used by a scsi target driver for internal commands
 */
#define	USCSI_NOPARITY	0x00010	/* run command without parity */
#define	USCSI_NODISCON	0x00020	/* run command without disconnects */
#define	USCSI_NOINTR	0x00040	/* No interrupts, NEVER to use this flag */
#define	USCSI_NOTAG	0x00100	/* Disable tagged queueing */
#define	USCSI_OTAG	0x00200	/* ORDERED QUEUE tagged cmd */
#define	USCSI_HTAG	0x00400	/* HEAD OF QUEUE tagged cmd */
#define	USCSI_HEAD	0x00800	/* Head of HA que */


#define	USCSI_RESERVED	0xfffe0000	/* Reserved Bits, must be zero */


/*
 * User SCSI io control command
 */
#define	USCSIIOC	(0x04 << 8)
#define	USCSICMD	(USCSIIOC|201) 	/* user scsi command */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_IMPL_USCSI_H */
