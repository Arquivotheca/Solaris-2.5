/*
 * Copyright (c) 1988-1994, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)reset_notify.c 1.3	94/04/05 SMI"

/*
 * support functions for hba drivers to handle scsi reset notifications.
 */

#include <sys/scsi/scsi.h>
#include <sys/scsi/adapters/reset_notify.h>

/*
 * routine for reset notification setup.
 * The function is entered without adapter driver mutex being held.
 */

int
scsi_hba_reset_notify_setup(struct scsi_address *ap, int flag,
	void (*callback)(caddr_t), caddr_t arg, kmutex_t *mutex,
	struct scsi_reset_notify_entry **listp)
{
	struct scsi_reset_notify_entry	*p, *beforep;
	int				rval = DDI_FAILURE;

	mutex_enter(mutex);
	p = *listp;
	beforep = NULL;
	while (p) {
		if (p->ap == ap)
			break;		/* An entry exist for this target */
		beforep = p;
		p = p->next;
	}

	if ((flag & SCSI_RESET_CANCEL) && (p != NULL)) {
		if (beforep == NULL) {
			*listp = p->next;
		} else {
			beforep->next = p->next;
		}
		kmem_free((caddr_t)p, sizeof (struct scsi_reset_notify_entry));
		rval = DDI_SUCCESS;

	} else if ((flag & SCSI_RESET_NOTIFY) && (p == NULL)) {
		p = (struct scsi_reset_notify_entry *)kmem_zalloc(
			sizeof (struct scsi_reset_notify_entry), KM_SLEEP);
		p->ap = ap;
		p->callback = callback;
		p->arg = arg;
		p->next = *listp;
		*listp = p;
		rval = DDI_SUCCESS;
	}
	mutex_exit(mutex);
	return (rval);
}

/*
 * routine to perform the notification callbacks after a reset.
 * The function is entered with adapter driver mutex being held.
 */
void
scsi_hba_reset_notify_callback(kmutex_t *mutex,
	struct scsi_reset_notify_entry **listp)
{
	int	i, count;
	struct	scsi_reset_notify_entry	*p;
	struct	notify_entry {
		void	(*callback)(caddr_t);
		caddr_t	arg;
	} *list;

	if ((p = *listp) == NULL)
		return;

	count = 0;
	while (p != NULL) {
		count++;
		p = p->next;
	}

	list = (struct notify_entry *)kmem_alloc(
		(size_t)(count * sizeof (struct notify_entry)), KM_NOSLEEP);
	if (list == NULL) {
		cmn_err(CE_WARN, "scsi_reset_notify: kmem_alloc failed");
		return;
	}

	for (i = 0, p = *listp; i < count; i++, p = p->next) {
		list[i].callback = p->callback;
		list[i].arg = p->arg;
	}

	mutex_exit(mutex);
	for (i = 0; i < count; i++) {
		if (list[i].callback != NULL)
			(void) (*list[i].callback)(list[i].arg);
	}
	kmem_free((caddr_t)list,
	    (size_t)(count * sizeof (struct notify_entry)));
	mutex_enter(mutex);
}
