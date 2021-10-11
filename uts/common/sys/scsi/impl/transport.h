/*
 * Copyright (c) 1989-1991 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_SCSI_IMPL_TRANSPORT_H
#define	_SYS_SCSI_IMPL_TRANSPORT_H

#pragma ident	"@(#)transport.h	1.27	95/01/13 SMI"



/*
 * Include the loadable module wrapper.
 */
#include <sys/modctl.h>


#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

/*
 * SCSI transport structures
 *
 *	As each Host Adapter makes itself known to the system,
 *	it will create and register with the library the structure
 *	described below. This is so that the library knows how to route
 *	packets, resource control requests, and capability requests
 *	for any particular host adapter. The 'a_hba_tran' field of a
 *	scsi_address structure made known to a Target driver will
 *	point to one of these transport structures.
 */

typedef struct scsi_hba_tran	scsi_hba_tran_t;

struct scsi_hba_tran {
	/*
	 * Ptr to the device info structure for this particular HBA.
	 */
	dev_info_t	*tran_hba_dip;

	/*
	 * Private fields for use by the HBA itself.
	 */
	void		*tran_hba_private;	/* HBA softstate */
	void		*tran_tgt_private;	/* target-specific info */

	/*
	 * Only used to refer to a particular scsi device
	 * if the entire scsi_hba_tran structure is "cloned"
	 * per target device, otherwise NULL.
	 */
	struct scsi_device	*tran_sd;

	/*
	 * Vectors to point to specific HBA entry points
	 */

	int		(*tran_tgt_init)(
				dev_info_t		*hba_dip,
				dev_info_t		*tgt_dip,
				scsi_hba_tran_t		*hba_tran,
				struct scsi_device	*sd);

	int		(*tran_tgt_probe)(
				struct scsi_device	*sd,
				int			(*callback)(
								void *));
	void		(*tran_tgt_free)(
				dev_info_t		*hba_dip,
				dev_info_t		*tgt_dip,
				scsi_hba_tran_t		*hba_tran,
				struct scsi_device	*sd);

	int		(*tran_start)(
				struct scsi_address	*ap,
				struct scsi_pkt		*pkt);

	int		(*tran_reset)(
				struct scsi_address	*ap,
				int			level);

	int		(*tran_abort)(
				struct scsi_address	*ap,
				struct scsi_pkt		*pkt);

	int		(*tran_getcap)(
				struct scsi_address	*ap,
				char			*cap,
				int			whom);

	int		(*tran_setcap)(
				struct scsi_address	*ap,
				char			*cap,
				int			value,
				int			whom);

	struct scsi_pkt	*(*tran_init_pkt)(
				struct scsi_address	*ap,
				struct scsi_pkt		*pkt,
				struct buf		*bp,
				int			cmdlen,
				int			statuslen,
				int			tgtlen,
				int			flags,
				int			(*callback)(
								caddr_t	arg),
				caddr_t			callback_arg);

	void		(*tran_destroy_pkt)(
				struct scsi_address	*ap,
				struct scsi_pkt		*pkt);

	void		(*tran_dmafree)(
				struct scsi_address	*ap,
				struct scsi_pkt		*pkt);

	void		(*tran_sync_pkt)(
				struct scsi_address	*ap,
				struct scsi_pkt		*pkt);

	int		(*tran_reset_notify)(
				struct scsi_address	*ap,
				int			flag,
				void			(*callback)(caddr_t),
				caddr_t			arg);

	/*
	 * Implementation-private specifics.
	 * No HBA should refer to any of the fields below.
	 * This information can and will change.
	 */

	int			tran_hba_flags;		/* flag options */

	/*
	 * min xfer and min/max burstsizes for DDI_CTLOPS_IOMIN
	 */
	u_int			tran_min_xfer;
	u_char			tran_min_burst_size;
	u_char			tran_max_burst_size;
};




/*
 * Prototypes for SCSI HBA interface functions
 *
 * All these functions are public interfaces, with the
 * exception of scsi_initialize_hba_interface() and
 * scsi_uninitialize_hba_interface(), called by the
 * scsi module _init() and _fini(), respectively.
 */

extern void		scsi_initialize_hba_interface(void);

#ifdef	NO_SCSI_FINI_YET
extern void		scsi_uninitialize_hba_interface(void);
#endif	/* NO_SCSI_FINI_YET */

extern int		scsi_hba_init(
				struct modlinkage	*modlp);

extern void		scsi_hba_fini(
				struct modlinkage	*modlp);

extern int		scsi_hba_attach(
				dev_info_t		*dip,
				ddi_dma_lim_t		*hba_lim,
				scsi_hba_tran_t		*hba_tran,
				int			flags,
				void			*hba_options);

extern int		scsi_hba_attach_setup(
				dev_info_t		*dip,
				ddi_dma_attr_t		*hba_dma_attr,
				scsi_hba_tran_t		*hba_tran,
				int			flags);

extern int		scsi_hba_detach(
				dev_info_t		*dip);

extern scsi_hba_tran_t	*scsi_hba_tran_alloc(
				dev_info_t		*dip,
				int			flags);

extern void		scsi_hba_tran_free(
				scsi_hba_tran_t		*hba_tran);

extern int		scsi_hba_probe(
				struct scsi_device	*sd,
				int			(*callback)());

extern struct scsi_pkt	*scsi_hba_pkt_alloc(
				dev_info_t		*dip,
				struct scsi_address	*ap,
				int			cmdlen,
				int			statuslen,
				int			tgtlen,
				int			hbalen,
				int			(*callback)(caddr_t),
				caddr_t			arg);

extern void		scsi_hba_pkt_free(
				struct scsi_address	*ap,
				struct scsi_pkt		*pkt);


extern int		scsi_hba_lookup_capstr(
				char			*capstr);

extern int		scsi_hba_in_panic(void);



/*
 * Flags for scsi_hba_attach
 */
#define	SCSI_HBA_TRAN_CLONE	0x01		/* clone scsi_hba_tran_t */
						/* structure per target */

/*
 * Flags for scsi_hba allocation functions
 */
#define	SCSI_HBA_CANSLEEP	0x01		/* can sleep */


#endif	/* _KERNEL */


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_IMPL_TRANSPORT_H */
