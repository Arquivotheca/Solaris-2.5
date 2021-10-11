#ident  "@(#)iflist.c	1.1    95/02/25 SMI"

/* Copyright (c) 1995 by Sun Microsystems, Inc. */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stropts.h>

#include "iflist.h"
#include "ipd.h"
#include "ipd_ioctl.h"

struct iflist    *iflist = NULL;

void
add_interface(char *name)
{
	int		i = 0;
	struct iflist   *ifitem;

	if ((ifitem = (struct iflist *)malloc(sizeof (struct iflist))) == NULL)
		fail("add_interface: malloc failed\n");

	if ((ifitem->name = (char *)malloc(strlen(name)+1)) == NULL)
		fail("add_interface: malloc failed\n");

	for (i = 0; name[i] != '\0'; ++i)
	    ifitem->name[i] = tolower(name[i]);
	ifitem->name[i] = '\0';

	if (iflist)
		ifitem->next = iflist;
	else
		ifitem->next = NULL;

	iflist = ifitem;
}

void
register_interfaces(void)
{
	struct strioctl cmio;
	struct iflist	*ifitem;
	int		offset;
	ipd_register_t	req;

	req.msg = IPD_REGISTER;

	cmio.ic_cmd = IPD_REGISTER;
	cmio.ic_timout = 0;
	cmio.ic_dp = (char *)&req;

	ifitem = iflist;
	while (ifitem) {
		if (strncmp("ipdptp", ifitem->name, 6) == 0) {
			req.iftype = IPD_PTP;
			offset = 6;
		} else {
			req.iftype = IPD_MTP;
			offset = 3;
		}
		req.ifunit = atoi(ifitem->name + offset);

		cmio.ic_len = sizeof (ipd_register_t);
		if (ioctl(ipdcm, I_STR, &cmio) < 0)
		    /* perhaps we should fail, rather than just logging it */
		    log(0, "register_interfaces: IPD_REGISTER failed\n");

		ifitem = ifitem->next;
	}
}
