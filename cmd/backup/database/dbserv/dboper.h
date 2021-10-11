
/*	@(#)dboper.h 1.4 92/03/24	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#ifndef DB_OPER_H
#define	DB_OPER_H
#include <syslog.h>
#include <operator.h>

#define	DBOPER_TTL	(time_t)300	/* 5 minutes */
#define	DBOPER_FLAGS	MSG_DISPLAY	/* display msg, no reply needed */
extern char opserver[];
extern char mydomain[];
extern char *myname;
#endif
