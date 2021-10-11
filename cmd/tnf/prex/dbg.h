/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _DBG_H
#define	_DBG_H

#pragma ident  "@(#)dbg.h 1.7 94/08/25 SMI"

#ifdef DEBUG
#define	DBG(x)		(x)
#else
#define	DBG(x)
#endif

#if defined(DEBUG) || defined(lint)
#include <stdio.h>
extern int	  __prb_verbose;
#endif

#endif				/* _DBG_H */
