#ident	"@(#)getsym.c	1.2	91/10/11 SMI"

/*
 *		Copyright (C) 1991  Sun Microsystems, Inc
 *			All rights reserved.
 *		Notice of copyright on this source code
 *		product does not indicate publication.
 *
 *		RESTRICTED RIGHTS LEGEND:
 *   Use, duplication, or disclosure by the Government is subject
 *   to restrictions as set forth in subparagraph (c)(1)(ii) of
 *   the Rights in Technical Data and Computer Software clause at
 *   DFARS 52.227-7013 and in similar clauses in the FAR and NASA
 *   FAR Supplement.
 */

#include <nlist.h>
#include <stdio.h>
#include "crash.h"

nl_getsym(symc, nl)
char *symc;
struct nlist *nl;
{
	struct nlist nla[2];
	int ret = 0;

	nla[0].n_name = symc;
	nla[1].n_name = 0;

	if ((ret = kvm_nlist(kd, nla)) == 0) {
		nl->n_name = symc;
		nl->n_value = nla[0].n_value;
		nl->n_type = nla[0].n_type;
		nl->n_scnum = nla[0].n_scnum;
		return (0);
	} else if (ret == -1)
		fprintf(stderr, "crash: kvm_nlist error\n");
	return (-1);
}
