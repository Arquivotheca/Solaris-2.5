/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Sep.03.86		*/

#ident	"@(#)wdata.c	1.4	92/07/14 SMI"   /* from JAE2.0 1.0 */

#include "synonyms.h"
#include <euc.h>
eucwidth_t _eucwidth;   /* structure of character width		*/

/*	Character width in EUC		*/
int	_cswidth[4] = {
		0,	/* 1:_cswidth is set, 0: not set	*/
		1,	/* Code Set 1 */
		0,	/* Code Set 2 */
		0	/* Code Set 3 */
	};

/*	Mask from EUC to Process Code		*/
long int _pcmask[4] = {
		0x0000007F,	/* Set 0 */
		0x60003FFF,	/* Set 1 */
		0x201FFFFF,	/* Set 2 */
		0x40003FFF	/* Set 3 */
	};
