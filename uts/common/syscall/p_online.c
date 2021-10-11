/*
 * Copyright (c) 1992, 1994 Sun Microsystems, Inc.
 */

#ident	"@(#)p_online.c	1.3	94/12/23 SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/var.h>
#include <sys/thread.h>
#include <sys/cpuvar.h>
#include <sys/kstat.h>
#include <sys/uadmin.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/procset.h>
#include <sys/processor.h>
#include <sys/debug.h>

/*
 * p_online(2) - set processor online or offline or query its status.
 */
int
p_online(processorid_t cpun, int flag)
{
	int	status;
	cpu_t	*cp;

	/*
	 * Try to get a pointer to the requested CPU
	 * structure.
	 */
	if ((cp = cpu_get(cpun)) == NULL)
		return (set_errno(EINVAL));

	mutex_enter(&cpu_lock);		/* protects CPU states */

	status = cpu_status(cp);	/* get the processor status */

	/*
	 * Perform credentials check.
	 */
	switch (flag) {
	case P_STATUS:
		goto out;

	case P_ONLINE:
	case P_OFFLINE:
		if (!suser(CRED())) {
			status = set_errno(EPERM);
			goto out;
		}
		break;

	default:
		status = set_errno(EINVAL);
		goto out;
	}

	/*
	 * return if the CPU is already in the desired new state.
	 */
	if (status == flag) {
		goto out;
	}

	ASSERT(flag == P_ONLINE || flag == P_OFFLINE);

	/*
	 * Handle on-line request.
	 *	This code must put the new CPU on the active list before
	 *	starting it because it will not be paused, and will start
	 * 	using the active list immediately.  The real start occurs
	 *	when the CPU_QUIESCED flag is turned off.
	 */
	if (flag == P_ONLINE) {
		int	error;

		if (error = cpu_online(cp)) {
			(void) set_errno(error);
		}

	} else if (flag == P_OFFLINE) {
		int	error;

		if (error = cpu_offline(cp)) {
			(void) set_errno(error);
		}
	}
out:
	mutex_exit(&cpu_lock);
	return (status);
}
