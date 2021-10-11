/*
 * Copyright (c) 1988-1994, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_SCSI_RESET_NOTIFY_H
#define	_SYS_SCSI_RESET_NOTIFY_H

#pragma ident	"@(#)reset_notify.h	1.3	95/05/12 SMI"

#include <sys/note.h>
#include <sys/scsi/scsi_types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * SCSI Control Information for Reset Notification.
 */

/*
 * adapter drivers use the following structure to record the notification
 * requests from target drivers.
 */
struct scsi_reset_notify_entry {
	struct scsi_address		*ap;
	void				(*callback)(caddr_t);
	caddr_t				arg;
	struct scsi_reset_notify_entry	*next;
};

_NOTE(SCHEME_PROTECTS_DATA("protected by lock passed as arg",
	scsi_reset_notify_entry:: {ap callback arg next}))

#ifdef	_KERNEL

#ifdef	__STDC__
extern int scsi_hba_reset_notify_setup(struct scsi_address *ap, int flag,
	void (*callback)(caddr_t), caddr_t arg, kmutex_t *mutex,
	struct scsi_reset_notify_entry **listp);
extern void scsi_hba_reset_notify_callback(kmutex_t *mutex,
	struct scsi_reset_notify_entry **listp);
#else	/* __STDC__ */
extern int scsi_hba_reset_notify_setup();
extern void scsi_hba_reset_notify_callback();
#endif	/* __STDC__ */

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_RESET_NOTIFY_H */
