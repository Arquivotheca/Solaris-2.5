/*
 * Copyright (c) 1988-1991, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)scsi_confdata.c	1.16	94/08/24 SMI"

#ifdef	_KERNEL

#include <sys/scsi/conf/autoconf.h>

/*
 * Autoconfiguration Dependent Data
 */

/*
 * SCSI options word- defines are kept in <scsi/conf/autoconf.h>
 *
 * All this options word does is to enable such capabilities. Each
 * implementation may disable this word, or ignore it entirely.
 * Changing this word after system autoconfiguration is not guaranteed
 * to cause any change in the operation of the system.
 */

int scsi_options =
	SCSI_OPTIONS_PARITY	|
	SCSI_OPTIONS_SYNC	|
	SCSI_OPTIONS_LINK	|
	SCSI_OPTIONS_TAG	|
	SCSI_OPTIONS_DR		|
	SCSI_OPTIONS_FAST	|
	SCSI_OPTIONS_WIDE;

/*
 * Scsi bus or device reset recovery time (milli secondss.)
 */
unsigned int	scsi_reset_delay = 3000;

/*
 * Default scsi host id.  Note, this variable is only used if the
 * "scsi-initiator-id" cannot be retrieved from openproms.  This is only
 * a problem with older platforms which don't have openproms and usage
 * of the sport-8 with openproms 1.x.
 */
int	scsi_host_id = 7;

/*
 * Maximum tag age limit.
 * Note exceeding tag age limit of 2 is fairly common;
 * refer to 1164758
 */
int	scsi_tag_age_limit = 2;

/*
 * scsi watchdog tick (secs)
 * Note: with tagged queueing, timeouts are highly inaccurate and therefore
 *	 it doesn't make sense to monitor every second.
 */
int	scsi_watchdog_tick = 10;

#endif	/* _KERNEL */
