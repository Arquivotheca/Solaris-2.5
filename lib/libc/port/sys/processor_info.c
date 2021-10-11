/*
 *	Copyright (c) Sun Microsystems Inc. 1991
 */

#ident	"@(#)processor_info.c	1.1	92/07/05 SMI"

#include "synonyms.h"

#include <sys/types.h>
#include <sys/procset.h>
#include <sys/processor.h>
#include <sys/syscall.h>

int
processor_info(processorid_t processorid, processor_info_t *infop)
{
	return (syscall(SYS_processor_info, processorid, infop));
}
