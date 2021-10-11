/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)v7door.c	1.2	94/12/03 SMI"

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/privregs.h>

/*
 * We need this routines to be here so that they are access the
 * version of privregs.h
 */

/*
 * Paramters used/modified with door_call
 */
void
dr_set_buf_size(void *rp, u_int x)
{
	((struct regs *)rp)->r_o2 = x;
}

void
dr_set_actual_size(void *rp, u_int x)
{
	((struct regs *)rp)->r_o3 = x;
}

void
dr_set_actual_ndid(void *rp, u_int x)
{
	((struct regs *)rp)->r_o4 = x;
}

/*
 * Server side arguments created here
 */
void
dr_set_data_size(void *rp, u_int x)
{
	((struct regs *)rp)->r_o2 = x;
}

void
dr_set_door_ptr(void *rp, u_int x)
{
	((struct regs *)rp)->r_o3 = x;
}

void
dr_set_door_size(void *rp, u_int x)
{
	((struct regs *)rp)->r_o4 = x;
}

void
dr_set_pc(void *rp, u_int x)
{
	((struct regs *)rp)->r_o5 = x;
}

void
dr_set_sp(void *rp, u_int x)
{
	((struct regs *)rp)->r_sp = x;
}

void
dr_set_nservers(void *rp, u_int x)
{
	((struct regs *)rp)->r_g1 = x;
}
