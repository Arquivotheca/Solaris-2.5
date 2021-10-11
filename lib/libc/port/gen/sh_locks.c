/*	Copyright (c) 1992 SMI	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sh_locks.c	1.2	92/01/16 SMI"	/* SVr4.0 1.3	*/

#include <synch.h>
#include <thread.h>

/*  These locks are for internal library usage only  */

/*  Lock for sbrk/brk */
mutex_t __sbrk_lock = DEFAULTMUTEX;

/* lock for malloc() */
mutex_t __malloc_lock = DEFAULTMUTEX;

/* lock for utmp/utmpx */
mutex_t __utx_lock = DEFAULTMUTEX;
