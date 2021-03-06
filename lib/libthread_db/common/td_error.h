/*
 *      Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _TD_ERROR_H
#define	_TD_ERROR_H

#pragma ident	"@(#)td_error.h	1.19	94/12/31 SMI"

/*
* MODULE_td_error.h_________________________________________________
*
*  Description:
*	Error types for thread_db errors.
____________________________________________________________________ */


/*
*   Internal errors used by functions in td_to.h.
*/

enum td_terr_e {
	TD_TERR,		/* generic error. */
	TD_TOK,			/* generic "call succeeded" */
	TD_TSTATE,		/* error in thread state */
	TD_TNOSO,		/* no synchronization object */
	TD_TNOLWP,		/* no LWP */
	TD_TBADTA,		/* bad thread agent */
	TD_TBADTI,		/* bad thread info. struct */
	TD_TDBERR,		/* imported function error */
	TD_TBADTH		/* bad thread handle  */
};

typedef enum td_terr_e td_terr_e;

enum td_serr_e {
	TD_SERR,		/* generic error */
	TD_SOK,			/* generic "call succeeded" */
	TD_SBADMUTEX,		/* invalid mutex */
	TD_SBADRW,		/* invalid r/w lock */
	TD_SBADSEMA,		/* invalid semaphore */
	TD_SBADCOND,		/* invalid condition variable */
	TD_SBADSO,		/* invalid so */
	TD_SINCMUTEX,		/* incomplete mutex */
	TD_SINCRW,		/* incomplete r/w lock */
	TD_SINCSEMA,		/* incomplete semaphore */
	TD_SINCCOND		/* incomplete condition variable */
};

typedef enum td_serr_e td_serr_e;

enum td_ierr_e {
	TD_IERR,		/* generic error */
	TD_IOK,			/* generic "call succeeded" */
	TD_INULL,		/* NULL pointer */
	TD_IMALLOC		/* malloc error */
};

typedef enum td_ierr_e td_ierr_e;


#endif /* _TD_ERROR_H */
