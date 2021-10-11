/* @(#)genassym.c 1.1 93/08/25 SMI */
#include <sys/synch.h>
#include "synch32.h"

/*
 * This file generates two values used by _lwp_mutex_unlock.s:
 *	a) the byte offset (in lwp_mutex_t) of the word containing the lock byte
 *	b) a mask to extract the waiter field from the word containing it
 */

lwp_mutex_t lm;
#define default_waiter_mask 0xff000000

main()
{
	printf("#define MUTEX_LOCK_WORD 0x%x\n", 
	    (char *)&lm.mutex_lockword - (char *)&lm);
	printf("#define WAITER_MASK 0x%x\n",
	    (uint_t)default_waiter_mask >> 
	    (8*((char *)&lm.mutex_waiters - (char *)&lm.mutex_lockword)));
	return(0);
}
