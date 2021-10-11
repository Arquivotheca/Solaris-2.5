/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)_data2.c	1.10	94/01/27 SMI"	/* SVr4.0 1.1.1.1	*/

#include "timt.h"
#include <sys/param.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/tihdr.h>
#include <sys/timod.h>
#include <tiuser.h>
#include <stdio.h>

/*
 * State transition table for TLI user level
 */

char tiusr_statetbl[T_NOEVENTS][T_NOSTATES] = {

/* 0    1    2    3    4    5    6    7   8 */
{  1, nvs, nvs, nvs, nvs, nvs, nvs, nvs,  8},
{nvs,   2, nvs, nvs, nvs, nvs, nvs, nvs,  8},
{nvs, nvs,   2, nvs, nvs, nvs, nvs, nvs,  8},
{nvs, nvs,   1, nvs, nvs, nvs, nvs, nvs,  8},
{nvs,   0, nvs, nvs, nvs, nvs, nvs, nvs,  8},
{nvs, nvs,   2, nvs, nvs, nvs, nvs, nvs,  8},
{nvs, nvs,   2, nvs, nvs, nvs, nvs, nvs,  8},
{nvs, nvs,   2, nvs, nvs, nvs, nvs, nvs,  8},
{nvs, nvs,   5, nvs, nvs, nvs, nvs, nvs,  8},
{nvs, nvs,   3, nvs, nvs, nvs, nvs, nvs,  8},
{nvs, nvs, nvs,   5, nvs, nvs, nvs, nvs,  8},
{nvs, nvs,   4, nvs,   4, nvs, nvs, nvs,  8},
{nvs, nvs, nvs, nvs,   5, nvs, nvs, nvs,  8},
{nvs, nvs, nvs, nvs,   2, nvs, nvs, nvs,  8},
{nvs, nvs, nvs, nvs,   4, nvs, nvs, nvs,  8},
{nvs, nvs, nvs, nvs, nvs,   5, nvs,   7,  8},
{nvs, nvs, nvs, nvs, nvs,   5,   6, nvs,  8},
{nvs, nvs, nvs,   2,   2,   2,   2,   2,  8},
{nvs, nvs, nvs, nvs,   4, nvs, nvs, nvs,  8},
{nvs, nvs, nvs,   2, nvs,   2,   2,   2,  8},
{nvs, nvs, nvs, nvs,   2, nvs, nvs, nvs,  8},
{nvs, nvs, nvs, nvs,   4, nvs, nvs, nvs,  8},
{nvs, nvs, nvs, nvs, nvs,   6, nvs,   2,  8},
{nvs, nvs, nvs, nvs, nvs,   7,   2, nvs,  8},
{nvs, nvs,   5, nvs, nvs, nvs, nvs, nvs,  8},
};
