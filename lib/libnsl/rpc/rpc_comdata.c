/*
 * Copyright (c) 1986-1991 by Sun Microsystems Inc.
 */

#ident	"@(#)rpc_comdata.c	1.10	92/09/15 SMI"

#include <rpc/rpc.h>
#include <rpc/trace.h>

/*
 * This file should only contain common data (global data) that is exported
 * by public interfaces
 */
struct opaque_auth _null_auth;
fd_set svc_fdset;
void (*_svc_getreqset_proc)();
