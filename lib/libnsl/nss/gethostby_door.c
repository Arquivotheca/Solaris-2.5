
/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)gethostby_door.c	1.6	95/03/14 SMI"

#ifdef PIC

#include <pwd.h>
#include <sys/door.h>
#include <errno.h>
#include <fcntl.h>
#include <synch.h>
#include <getxby_door.h>

static struct hostent *
process_gethost(struct hostent *result, char *buffer, int buflen,
	int *h_errnop, nsc_data_t *sptr);

struct hostent *
_gethostbyname_r(const char *name, struct hostent *result,
	char *buffer, int buflen, int *h_errnop);

struct hostent *
_gethostbyaddr_r(const char *addr, int length, int type,
	struct hostent *result, char *buffer, int buflen, int *h_errnop);

struct hostent *
gethostbyname_r(const char *name, struct hostent *result,
	char *buffer, int buflen, int *h_errnop);

struct hostent *
gethostbyaddr_r(const char *addr, int length, int type,
	struct hostent *result, char *buffer, int buflen, int *h_errnop);


struct hostent *
_door_gethostbyname_r(const char *name, struct hostent *result, char *buffer,
	int buflen, int *h_errnop)
{

	/*
	 * allocate space on the stack for the nscd to return
	 * host and host alias information
	 */
	union {
		nsc_data_t 	s_d;
		char		s_b[8192];
	} space;
	nsc_data_t	*sptr;
	int		ndata;
	int		adata;
	struct	hostent *resptr = NULL;

	if (name == (const char *)NULL)
		return (NULL);

	adata = (sizeof (nsc_call_t) + strlen(name) + 1);
	ndata = sizeof (space);
	space.s_d.nsc_call.nsc_callnumber = GETHOSTBYNAME;
	strcpy(space.s_d.nsc_call.nsc_u.name, name);
	sptr = &space.s_d;

	switch (_nsc_trydoorcall(&sptr, &ndata, &adata)) {
	    case SUCCESS:	/* positive cache hit */
		break;
	    case NOTFOUND:	/* negative cache hit */
		if(h_errnop)
		    *h_errnop = space.s_d.nsc_ret.nsc_errno;
		return (NULL);
	    default:
		return ((struct hostent *)_switch_gethostbyname_r(name,
		    result, buffer, buflen, h_errnop));
	}
	resptr = process_gethost(result, buffer, buflen, h_errnop, sptr);

	/*
	 * check if doors realloced buffer underneath of us....
	 * munmap or suffer a memory leak
	 */

	if (sptr != &space.s_d) {
		munmap(sptr, ndata); /* return memory */
	}

	return (resptr);
}

struct hostent *
_door_gethostbyaddr_r(const char *addr, int length, int type,
	struct hostent *result, char *buffer, int buflen, int *h_errnop)
{
	/*
	 * allocate space on the stack for the nscd to return
	 * host and host alias information
	 */
	union {
		nsc_data_t 	s_d;
		char		s_b[8192];
	} space;
	nsc_data_t 	*sptr;
	int		ndata;
	int		adata;
	struct	hostent *resptr = NULL;

	if (addr == NULL)
		return (NULL);

	ndata = sizeof (space);
	adata = length + sizeof (nsc_call_t) + 1;
	sptr = &space.s_d;

	space.s_d.nsc_call.nsc_callnumber = GETHOSTBYADDR;
	space.s_d.nsc_call.nsc_u.addr.a_type = type;
	space.s_d.nsc_call.nsc_u.addr.a_length = length;
	memcpy(space.s_d.nsc_call.nsc_u.addr.a_data, addr, length);

	switch (_nsc_trydoorcall(&sptr, &ndata, &adata)) {
	    case SUCCESS:	/* positive cache hit */
		break;
	    case NOTFOUND:	/* negative cache hit */
		if(h_errnop)
		    *h_errnop = space.s_d.nsc_ret.nsc_errno;
		return (NULL);
	    default:
		return ((struct hostent *)_switch_gethostbyaddr_r(addr,
		    length, type, result, buffer, buflen, h_errnop));
	}

	resptr = process_gethost(result, buffer, buflen, h_errnop, sptr);

	/*
	 * check if doors realloced buffer underneath of us....
	 * munmap it or suffer a memory leak
	 */

	if (sptr != &space.s_d) {
		munmap(sptr, ndata); /* return memory */
	}

	return (resptr);

}

static struct hostent *
process_gethost(struct hostent *result, char *buffer, int buflen,
	int *h_errnop, nsc_data_t *sptr)
{
	int i;
	
	char * fixed;

	fixed = (char*) (((int)buffer +3) & ~3);
	buflen -= fixed - buffer;
	buffer = fixed;

	if (buflen + sizeof (nsc_data_t) < sptr->nsc_ret.nsc_bufferbytesused) {
		/*
		 * no enough space allocated by user
		 */
		errno = ERANGE;
		return (NULL);
	}

	memcpy(buffer, sptr->nsc_ret.nsc_u.buff + sizeof (struct hostent),
	    sptr->nsc_ret.nsc_bufferbytesused - sizeof (struct hostent));

	sptr->nsc_ret.nsc_u.hst.h_name += (int) buffer;
	sptr->nsc_ret.nsc_u.hst.h_aliases =
	    (char **) ((char *)sptr->nsc_ret.nsc_u.hst.h_aliases + (int)buffer);
	sptr->nsc_ret.nsc_u.hst.h_addr_list =
	    (char **) ((char *)sptr->nsc_ret.nsc_u.hst.h_addr_list +
	    (int)buffer);
	for (i = 0; sptr->nsc_ret.nsc_u.hst.h_aliases[i]; i++) {
		sptr->nsc_ret.nsc_u.hst.h_aliases[i] += (int) buffer;
	}
	for (i = 0; sptr->nsc_ret.nsc_u.hst.h_addr_list[i]; i++) {
		sptr->nsc_ret.nsc_u.hst.h_addr_list[i] += (int) buffer;
	}

	*result = sptr->nsc_ret.nsc_u.hst;

	return (result);
}

#endif PIC
