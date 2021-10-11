/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)regdosba.c 1.1	94/10/12 SMI"

/*
 * regdosuba -- allocating version of regdosub for ed
 *
 * Copyright 1985, 1992 by Mortice Kern Systems Inc.  All rights reserved.
 *
 * This Software is unpublished, valuable, confidential property of
 * Mortice Kern Systems Inc.  Use is authorized only in accordance
 * with the terms and conditions of the source licence agreement
 * protecting this Software.  Any unauthorized use or disclosure of
 * this Software is strictly prohibited and will result in the
 * termination of the licence agreement.
 *
 * If you have any questions, please consult your supervisor.
 * 
 */
#ifdef M_RCSID
#ifndef lint
static char rcs__ID[] = "$Header: /u/rd/src/libc/regex/RCS/regdosba.c,v 1.10 1992/06/19 13:28:36 gord Exp $"; /* avoid collision with included file */
#endif
#endif

/*l
 * The source for this file is in regdosub.c.
 */

#define	EXPAND	1
#include "regdosub.c"
