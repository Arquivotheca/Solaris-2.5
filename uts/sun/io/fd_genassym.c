/*
 * Copyright (c) 1992-1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)fd_genassym.c	1.5	95/01/19 SMI"

#ifndef _GENASSYM
#define	_GENASSYM
#endif

#define	SIZES	1

#include <sys/types.h>
#include <sys/t_lock.h>

#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/fdvar.h>

#define	OFFSET(type, field)	((int)(&((type *)0)->field))


main()
{
	printf("#define\tFD_NEXT 0x%x\n", OFFSET(struct fdctlr, c_next));
	printf("#define\tFD_REG 0x%x\n", OFFSET(struct fdctlr, c_control));
	printf("#define\tFD_HIINTCT 0x%x\n", OFFSET(struct fdctlr, c_hiintct));
	printf("#define\tFD_SOFTIC 0x%x\n", OFFSET(struct fdctlr, c_softic));
	printf("#define\tFD_HILOCK 0x%x\n", OFFSET(struct fdctlr, c_hilock));
	printf("#define\tFD_OPMODE 0x%x\n",
	    OFFSET(struct fdctlr, c_csb.csb_opmode));
	printf("#define\tFD_RADDR 0x%x\n",
	    OFFSET(struct fdctlr, c_csb.csb_raddr));
	printf("#define\tFD_RLEN 0x%x\n",
	    OFFSET(struct fdctlr, c_csb.csb_rlen));
	printf("#define\tFD_RSLT 0x%x\n",
	    OFFSET(struct fdctlr, c_csb.csb_rslt[0]));
	printf("#define\tFD_AUXIOVA 0x%x\n",
		OFFSET(struct fdctlr, c_auxiova));
	printf("#define\tFD_AUXIODATA 0x%x\n",
		OFFSET(struct fdctlr, c_auxiodata));
	printf("#define\tFD_AUXIODATA2 0x%x\n",
		OFFSET(struct fdctlr, c_auxiodata2));
	printf("#define\tFD_FASTTRAP 0x%x\n",
		OFFSET(struct fdctlr, c_fasttrap));
	printf("#define\tFD_SOFTID 0x%x\n",
		OFFSET(struct fdctlr, c_softid));


	/*
	 * Gross hack... Although genassym is a user program and hence
	 * exit has one parameter, it is compiled with the kernel headers
	 * and the _KERNEL define so ANSI-C thinks it should have two!
	 */
	exit(0, 0);
}
