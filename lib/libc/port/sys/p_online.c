/*
 *	Copyright (c) Sun Microsystems Inc. 1991
 */

#ident	"@(#)p_online.c	1.1	92/07/05 SMI"

#include "synonyms.h"

#include <sys/types.h>
#include <sys/procset.h>
#include <sys/processor.h>
#include <sys/syscall.h>

int
p_online(processorid_t cpu, int flag)
{
	return (syscall(SYS_p_online, (int) cpu, flag));
}
