#if !defined(lint) && defined(SCCSIDS)
static char	*bsm_sccsid = "@(#)audit_mountd.c 1.2 92/10/18 SMI; SunOS BSM";
#endif

#include <sys/types.h>
#include <stdio.h>
#include <sys/fcntl.h>
#include <bsm/audit.h>
#include <bsm/audit_record.h>
#include <bsm/audit_uevents.h>
#include <bsm/libbsm.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <netinet/in.h>
#include <unistd.h>
#include "generic.h"

#ifdef C2_DEBUG2
#define dprintf(x) { printf x; }
#else
#define	dprintf(x)
#endif

audit_mountd_setup()
{
	dprintf(("audit_mountd_setup()\n"));

	aug_save_namask();

	return (0);
}

audit_mountd_mount(clname, path, success)
char	*clname;	/* client name */
char	*path;		/* mount path */
int	success;	/* flag for success or failure */
{
	dprintf(("audit_mountd_mount()\n"));

	if (cannot_audit(0)) {
		return (0);
	}

	aug_save_event(AUE_mountd_mount);
	aug_save_sorf(!success);
	aug_save_text(clname);
	aug_save_path(path);
	aug_save_tid(aug_get_port(), aug_get_machine(clname));

	return (aug_audit());
}

audit_mountd_umount(clname, path)
char	*clname;	/* client name */
char	*path;		/* mount path */
{
	dprintf(("audit_mountd_mount()\n"));

	if (cannot_audit(0)) {
		return (0);
	}

	aug_save_event(AUE_mountd_umount);
	aug_save_sorf(0);
	aug_save_text(clname);
	aug_save_path(path);
	aug_save_tid(aug_get_port(), aug_get_machine(clname));

	return (aug_audit());
}
