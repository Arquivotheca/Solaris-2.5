/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef	_INET_OPTCOM_H
#define	_INET_OPTCOM_H

#pragma ident	"@(#)optcom.h	1.8	93/02/04 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(_KERNEL) && defined(__STDC__)

extern	void	optcom_err_ack(queue_t * q, mblk_t * mp, int t_error,
			int sys_error);

extern	void	optcom_req(queue_t * q, mblk_t * mp, pfi_t setfn,
			pfi_t getfn, pfb_t chkfn, int priv);

#endif	/* defined(_KERNEL) && defined(__STDC__) */

#ifdef	__cplusplus
}
#endif

#endif	/* _INET_OPTCOM_H */
