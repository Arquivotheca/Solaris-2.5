/*
 *      Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _TD_H
#define	_TD_H

#pragma ident	"@(#)td.h	1.33	94/12/31 SMI"

/*
* MODULE_td.h_______________________________________________________
*
*  Description:
*	Header file for libthread_db
*____________________________________________________________________ */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
*  td_noop is going away.
*/

#ifdef TD_INITIALIZER
#define	EXTERN
#ifdef TEST_PS_CALLS
int	td_noop = 0;
#endif
#else
#define	EXTERN extern
#ifdef TEST_PS_CALLS
extern int	td_noop;
#endif
#endif

/*
*   TD_ALIEN_CODE include code that is somewhere in product but
* probably shouldn't be.  For example code that was taken from
* someplace else and should be include in some other fashion.
*/
#define	TD_ALIEN_CODE

/*
*   TD_NOT_TEST prevents compilation of code that does not work yet.
* for example, code that requires libthread support.
*/
#define	TD_NOT_TEST 0

/*
*   TD_DEBUG cause additional diagnostics to be printed.
*/
#define	TD_DEBUG 0

/*
*   TD_NOT_DONE is defined to whatever value should be returned from
* functions that are not yet implemented.  This has changed before
* and may change again.
*/
#define	TD_NOT_DONE TD_NOCAPAB

#include <sys/types.h>
#include <synch.h>

#include "proc_service.h"

/*
*   This is required for tdb functions that have ps_prochandle as
* a formal parameter.  It is in proc_service.h but is included here
* as a convenience.
*/
struct ps_prochandle;

#include "libthread.h"
#include "td_error.h"
#include "td_po.h"
#include "td_to.h"
#include "td_so.h"
#include "td_event.h"

#include "td.pubdcl.h"
#include "xtd.pubdcl.h"

#define	TD_MAX_BUFFER_SIZE 128

#ifndef NULL
#define	NULL 0
#endif

/*
*   These should be defined in pairs.
*/
#ifndef TRUE
#define	TRUE 1
#undef FALSE
#define	FALSE 0
#endif


/*
*   Global variables.
*/
#ifdef TD_INITIALIZER
int	(*diag_print) (const char *, ...) = printf;
/*
*   __td_debug - internal switch to control diagnostic printouts.
*/
int	__td_debug = 0;
/*
*   __td_debug - exported switch to control diagnostic printouts.
*/
int	__td_pub_debug = 0;
/*
*   __td_logging - global switch that controls logging to debugger
* (call of ps_plog).
*/
int	__td_logging = 0;
#ifdef TD_INTERNAL_TESTS
/*
*   td_report_error - switch that controls printing from error
* reporting package.
*/
int	td_report_error = 0;
#endif	/* TD_INTERNAL_TESTS */
/*
*   Global event mask that is OR'ed into all thread event masks.
* that are placed in thread struct.
*/
td_thr_events_t td_global_events = {
	0
};
#else
extern int	(*diag_print) (const char *, ...);
extern int	__td_debug;
extern int	__td_pub_debug;
extern int	__td_logging;
#ifdef TD_INTERNAL_TESTS
extern int	td_report_error;
#endif	/* TD_INTERNAL_TESTS */
extern td_thr_events_t td_global_events;
#endif

#define	td_assert_(x) assert(x)

#include "td.extdcl.h"

/*
*  Locks for protecting global data.
*/
EXTERN mutex_t  __gd_lock;

#endif /* _TD_H */
