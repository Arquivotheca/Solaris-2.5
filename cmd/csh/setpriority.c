/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/* 	Portions Copyright(c) 1988, Sun Microsystems Inc.	*/
/*	All Rights Reserved					*/

#ident	"@(#)setpriority.c	1.3	94/04/11 SMI"	/* SVr4.0 1.1	*/

#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/procset.h>
#include <sys/priocntl.h>
#include <sys/tspriocntl.h>
#include <errno.h>

static int	prio_to_idtype();
static int	init_in_set();
static int	get_clinfo();

getpriority(which, who)
	int	which;
	int	who;
{
	id_t id;
	idtype_t idtype;
	pcinfo_t pcinfo;
	pcparms_t pcparms;
	tsparms_t *tsp;
	int scale;		/* amount to scale priority by */
	id_t tscid = 1;		/* time-sharing class ID */

	if (who == 0)
		id = P_MYID;
	else
		id = who;

	idtype = prio_to_idtype(which);
	if (idtype == -1) {
		errno = EINVAL;
		return (-1);
	}
	if (who < 0) {
		errno = ESRCH;
		return (-1);
	}

	/*
	 * get class ID, name, and attributes for the process
	 */
	if (get_clinfo(idtype, id, &pcinfo, &pcparms) == -1)
		return (-1);

	tscid = pcinfo.pc_cid;
	scale = ((tsinfo_t *)pcinfo.pc_clinfo)->ts_maxupri;

	tsp = (tsparms_t *)pcparms.pc_clparms;

	if (tsp->ts_upri > scale || tsp->ts_upri < -scale) {
		/* got upri out of valid range?? */
		errno = ESRCH;
		return (-1);
	}
	if (scale == 0)
		return (0);
	else
		return (-(tsp->ts_upri * 20) / scale);
}

setpriority(which, who, prio)
	int	which;
	int	who;
	int	prio;
{
	id_t id;
	idtype_t idtype;
	pcinfo_t pcinfo;
	pcparms_t pcparms;
	tsparms_t *tsp;
	procset_t procset;
	id_t tscid = 1;		/* time-sharing class ID */
	int scale;		/* amount to scale priority by */
	
	if (who == 0)
		id = P_MYID;
	else
		id = who;
	
	idtype = prio_to_idtype(which);
	if (idtype == -1) {
		errno = EINVAL;
		return (-1);
	}
	if (who < 0) {
		errno = ESRCH;
		return(-1);
	}

	/*
	 * get class ID, name, and attributes for the process
	 */
	if (get_clinfo(idtype, id, &pcinfo, &pcparms) == -1)
		return (-1);

	tscid = pcinfo.pc_cid;
	scale = ((tsinfo_t *)pcinfo.pc_clinfo)->ts_maxupri;

	if (prio > 19)
		prio = 19;
	else if (prio < -20)
		prio = -20;

	tsp = (tsparms_t *)pcparms.pc_clparms;
	tsp->ts_uprilim = tsp->ts_upri = -(scale * prio) / 20;
	
	setprocset(&procset, POP_AND, idtype, id, P_CID, tscid);
	if (priocntlset(&procset, PC_SETPARMS, (caddr_t)&pcparms) == -1)
		return (-1);

	if (init_in_set(idtype, id)) {
		setprocset(&procset, POP_AND, P_PID, P_INITPID,
		    P_CID, tscid);
		if (priocntlset(&procset, PC_SETPARMS, (caddr_t)&pcparms) == -1)
			return(-1);
	}

	return(0);

}

static int
prio_to_idtype(which)
	int which;
{
	switch (which) {
	case PRIO_PROCESS:
		return(P_PID);

	case PRIO_PGRP:
		return(P_PGID);

	case PRIO_USER:
		return(P_UID);

	default:
		return(-1);
	}
}


static int
init_in_set(idtype, id)
idtype_t	idtype;
id_t		id;
{
	switch (idtype) {

	case P_PID:
		if (id == P_INITPID)
			return(1);
		else
			return(0);

	case P_PGID:
		if (id == P_INITPGID)
			return(1);
		else
			return(0);

	case P_UID:
		if (id == P_INITUID)
			return(1);
		else
			return(0);

	default:
		return(0);
	}
}

static int
get_clinfo(idtype, id, pcinfo, pcparms)
	idtype_t idtype;
	id_t id;
	pcinfo_t *pcinfo;
	pcparms_t *pcparms;
{
	pcparms->pc_cid = PC_CLNULL;
	if (priocntl(idtype, id, PC_GETPARMS, (caddr_t)pcparms) == -1)
		return (-1);
	
	pcinfo->pc_cid = pcparms->pc_cid;
	if (priocntl(0, 0, PC_GETCLINFO, (caddr_t)pcinfo) == -1)
		return (-1);

	return (1);
}
