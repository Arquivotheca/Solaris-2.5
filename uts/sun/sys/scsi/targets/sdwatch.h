/*
 * Copyright (c) 1989-1991, by Sun Microsytems, Inc.
 */

#ifndef	_SYS_SCSI_TARGETS_SDWATCH_H
#define	_SYS_SCSI_TARGETS_SDWATCH_H

#pragma ident	"@(#)sdwatch.h	1.2	95/03/17 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

struct scsi_watch_result {
	struct scsi_status		*statusp;
	struct scsi_extended_sense	*sensep;
	u_char				actual_sense_length;
	struct scsi_pkt			*pkt;
};

/*
 * 120 seconds is a *very* reasonable amount of time for most slow CD.
 */

#define	SDWATCH_IO_TIME	120

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_TARGETS_SDWATCH_H */
