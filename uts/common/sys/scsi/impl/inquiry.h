/*
 * Copyright (c) 1990 Sun Microsystems, Inc.
 */

#ifndef	_SYS_SCSI_IMPL_INQUIRY_H
#define	_SYS_SCSI_IMPL_INQUIRY_H

#pragma ident	"@(#)inquiry.h	1.5	92/07/14 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Implementation inquiry data that is not within
 * the scope of any released SCSI standard.
 */

/*
 * Minimum inquiry data length (includes up through RDF field)
 */

#define	SUN_MIN_INQLEN	4

/*
 * Inquiry data size definition
 */
#define	SUN_INQSIZE	(sizeof (struct scsi_inquiry))

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_IMPL_INQUIRY_H */
