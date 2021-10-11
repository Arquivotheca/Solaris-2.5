/*
 *	Copyright (c) Sun Microsystems Inc. 1991
 */

#ident	"@(#)processor_bind.c	1.1	92/07/05 SMI"

#include "synonyms.h"

#include <sys/types.h>
#include <sys/procset.h>
#include <sys/processor.h>
#include <sys/syscall.h>

int     
processor_bind(idtype_t idtype, id_t id,
	processorid_t processorid, processorid_t *obind)
{
	return (syscall(SYS_processor_bind, idtype, id, processorid, obind));
}
