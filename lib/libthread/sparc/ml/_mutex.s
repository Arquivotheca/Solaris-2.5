#ident "@(#)_mutex.s  1.1     95/08/08"
        .file "_mutex.s"
 
#include <sys/asm_linkage.h>
 
#include "SYS.h"
#include "synch32.h"
#include "../assym.s"
 
/*
 * Returns > 0 if there are waiters for this lock.
 * Returns 0 if there are no waiters for this lock.
 * Could seg-fault if the lock memory which contains waiter info is freed.
 * The seg-fault is handled by libthread and the PC is advanced beyond faulting
 * instruction.
 *
 * int
 * _mutex_unlock_asm (mp)
 *      mutex_t *mp;
 */
        .global __wrd
        ENTRY(_mutex_unlock_asm)
        clrb    [%o0 + MUTEX_LOCK]      ! clear lock byte
        ldstub  [%sp - 4], %g0          ! flush CPU store buffer
        clr     %o1                     ! clear to return correct waiters
__wrd:  ldub    [%o0+MUTEX_WAITERS], %o1! read waiters into %o1: could seg-fault
        retl                            !
        mov     %o1, %o0                ! return waiters into %o0
        SET_SIZE(_mutex_unlock_asm)
