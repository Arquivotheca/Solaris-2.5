/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)status.c 1.18 94/08/25 SMI"

/*
 * Includes
 */

#include <string.h>
#include <libintl.h>

#include "prbutlp.h"
#include "dbg.h"


/*
 * prb_status_str() - this routine returns a pointer to a static string
 * describing the error argument.
 */

const char	 *
prb_status_str(prb_status_t prbstat)
{
	/* if this is in the errno range, use the errno string */
	if (prbstat >= PRB_STATUS_MINERRNO &&
		prbstat <= PRB_STATUS_MAXERRNO) {
		return (strerror(prbstat));
	} else {
		switch (prbstat) {
		case PRB_STATUS_OK:
			return (gettext("success"));
		case PRB_STATUS_NOPRBOBJ:
			return (gettext("can't find libprobe object"));
		case PRB_STATUS_BADELFVERS:
			return (gettext("bad elf version"));
		case PRB_STATUS_BADELFOBJ:
			return (gettext("bad elf object"));
		case PRB_STATUS_ALLOCFAIL:
			return (gettext("memory allocation failed"));
		case PRB_STATUS_SYMMISSING:
			return (gettext("couldn't find symbol"));
		case PRB_STATUS_BADARG:
			return (gettext("bad input argument"));
		case PRB_STATUS_MMAPFAIL:
			return (gettext("mmap (or munmap) failed"));
		case PRB_STATUS_BADDYN:
			return (gettext("corrupted .dynamic section"));
		case PRB_STATUS_NOTDYNAMIC:
			return (gettext("not a dynamic executable"));
		case PRB_STATUS_BADSYNC:
			return (gettext("couldn't sync with rtld"));
		case PRB_STATUS_NIY:
			return (gettext("not implemented yet"));
		case PRB_STATUS_BADLMAPSTATE:
			return (gettext("inconsistent link map"));
		case PRB_STATUS_NOTINLMAP:
			return (gettext("address not in link map"));
		default:
			return (gettext("<unknown status>"));
		}
	}

}				/* end prb_status_str */


/*
 * prb_status_map() - this routine converts an errno value into a
 * prb_status_t.
 */

prb_status_t
prb_status_map(int errno)
{
	return (errno);

}				/* end prb_status_map */


/*
 * prb_verbose_set() - turns verbose debugging to stderr on or off
 */

#if defined(DEBUG) || defined(lint)
int			 __prb_verbose = 0;
#endif

void
prb_verbose_set(int verbose)
{

#if defined(DEBUG) || defined(lint)
	__prb_verbose = verbose;
#endif

}				/* end prb_verbose_set */
