/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)machlibthread.h	1.12	95/06/30	SMI"



typedef struct {
	long	rs_sp;
	long	rs_pc;
	long	rs_fsr;
	long	rs_fpu_en;
} resumestate_t;

#define	t_sp		t_resumestate.rs_sp
#define	t_pc		t_resumestate.rs_pc
#define	t_fsr		t_resumestate.rs_fsr
#define	t_fpu_en	t_resumestate.rs_fpu_en
