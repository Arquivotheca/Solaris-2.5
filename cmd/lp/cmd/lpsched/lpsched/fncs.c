/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)fncs.c	1.22	94/09/30 SMI"	/* SVr4.0 1.9.1.3	*/
/* EMACS_MODES: !fill, lnumb, !overwrite, !nodelete, !picture */

#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "errno.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"

#include "lpsched.h"

/**
 ** walk_ptable() - WALK PRINTER TABLE, RETURNING ACTIVE ENTRIES
 ** walk_ftable() - WALK FORMS TABLE, RETURNING ACTIVE ENTRIES
 ** walk_ctable() - WALK CLASS TABLE, RETURNING ACTIVE ENTRIES
 ** walk_pwtable() - WALK PRINT WHEEL TABLE, RETURNING ACTIVE ENTRIES
 **/


PSTATUS *
walk_ptable(int start)
{
	/* ENTRY ("walk_ptable") */

	static PSTATUS		*psend,
				*ps = 0;

	if (start || !ps) {
		ps = PStatus;
		psend = PStatus + PT_Size;
	}

	while (ps < psend && !ps->printer->name)
		ps++;

	if (ps >= psend)
		return (ps = 0);
	else
		return (ps++);
}

FSTATUS *
walk_ftable(int start)
{
	ENTRY ("walk_ftable")

	static FSTATUS		*psend,
				*ps = 0;

	if (start || !ps) {
		ps = FStatus;
		psend = FStatus + FT_Size;
	}

	while (ps < psend && !ps->form->name)
		ps++;

	if (ps >= psend)
		return (ps = 0);
	else
		return (ps++);
}

CSTATUS *
walk_ctable (int start)
{
	ENTRY ("walk_ctable")

	static CSTATUS		*psend,
				*ps = 0;

	if (start || !ps) {
		ps = CStatus;
		psend = CStatus + CT_Size;
	}

	while (ps < psend && !ps->class->name)
		ps++;

	if (ps >= psend)
		return (ps = 0);
	else
		return (ps++);
}

PWSTATUS *
walk_pwtable(int start)
{
	ENTRY ("walk_pwtable")

	static PWSTATUS		*psend,
				*ps = 0;

	if (start || !ps) {
		ps = PWStatus;
		psend = PWStatus + PWT_Size;
	}

	while (ps < psend && !ps->pwheel->name)
		ps++;

	if (ps >= psend)
		return (ps = 0);
	else
		return (ps++);
}


/**
 ** search_ptable() - SEARCH PRINTER TABLE
 ** search_ftable() - SEARCH FORMS TABLE
 ** search_ctable() - SEARCH CLASS TABLE
 ** search_pwtable() - SEARCH PRINT WHEEL TABLE
 **/

PSTATUS *
search_ptable(register char *name)
{ 
	ENTRY ("search_ptable")

	register PSTATUS	*ps,
				*psend; 

	for ( 
		ps = & PStatus[0], psend = & PStatus[PT_Size]; 
		ps < psend && !SAME(ps->printer->name, name); 
		ps++ 
	)
		; 

	if (ps >= psend) 
		ps = 0; 

	return (ps); 
}

PSTATUS *
search_ptable_remote(register char *name)
{ 
	ENTRY ("search_ptable_remote")

	register PSTATUS	*ps, *psend; 

	for (ps = & PStatus[0], psend = & PStatus[PT_Size]; 
	    ps < psend && !SAME(ps->remote_name, name); ps++);

	if (ps >= psend) 
		ps = 0; 

	return (ps); 
}

FSTATUS *
search_ftable(register char *name)
{ 
	ENTRY ("search_ftable")

	register FSTATUS	*ps, *psend; 

	for (ps = & FStatus[0], psend = & FStatus[FT_Size]; 
	    ps < psend && !SAME(ps->form->name, name); ps++); 

	if (ps >= psend) 
		ps = 0; 

	return (ps); 
}

FSTATUS *
search_fptable(register char *paper)
{ 
	ENTRY ("search_fptable")

	register FSTATUS	*ps,*cand, *psend; 

	cand = NULL;
	for (ps = & FStatus[0], psend = & FStatus[FT_Size]; ps < psend; ps++)
		if (SAME(ps->form->paper, paper)) {
			if (ps->form->isDefault) {
				cand = ps;
				break;
			} else if (!cand)
				cand = ps;
		}

	return (cand); 
}

CSTATUS *
search_ctable(register char *name)
{ 
	ENTRY ("search_ctable")

	register CSTATUS	*ps,
				*psend; 

	for ( 
		ps = & CStatus[0], psend = & CStatus[CT_Size]; 
		ps < psend && !SAME(ps->class->name, name); 
		ps++ 
	)
		; 

	if (ps >= psend) 
		ps = 0; 

	return (ps); 
}

PWSTATUS *
search_pwtable(register char *name)
{ 
	ENTRY ("search_pwtable")

	register PWSTATUS	*ps,
				*psend; 

	for ( 
		ps = & PWStatus[0], psend = & PWStatus[PWT_Size]; 
		ps < psend && !SAME(ps->pwheel->name, name); 
		ps++ 
	)
		; 

	if (ps >= psend) 
		ps = 0; 

	return (ps); 
}

SSTATUS *
search_stable(char *name)
{
	ENTRY ("search_stable")

	int			i;

	/* start of sunsoft bugfix 1103573	*/
	for (i = 0; SStatus[i] && !SAME(SStatus[i]->system->name, name); ) {
		i++;
	}
	/* end of sunsoft bugfix 1103573	*/
    
	/* start of sunsoft bugfix 1103573	*/
	if (SStatus[i] != NULL) {
		/* now see if we've found the proper system name	*/
		if (SAME(SStatus[i]->system->name, name)) {
			return (SStatus[i]);
		}
	}

	/* at this point, the remote system name hasn't been found
	 * within the stable. so we see if we have "open access".
	 */
	for (i = 0; SStatus[i] != NULL; i++) {
		if (SAME(SStatus[i]->system->name, "+") || 
		    SAME(name, Local_System)) {
			SSTATUS *ssp;

			ssp = (SSTATUS *)Calloc(1, sizeof(SSTATUS));
			ssp->system = (SYSTEM *)Calloc(1, sizeof(SYSTEM));
			*ssp->system = *SStatus[i]->system;
			ssp->exec = (EXEC *)Calloc(1, sizeof(EXEC));
			ssp->system->name = Strdup(name);
			ssp->system->passwd =
				Strdup(SStatus[i]->system->passwd);
			ssp->system->comment =
				Strdup(SStatus[i]->system->comment);
			addone((void ***)&SStatus, ssp, &ST_Size, &ST_Count);
			return (ssp); 
		}	/* if		*/
	}		/* for ()	*/
	/* end of sunsoft bugfix 1103573	*/

	return ((SSTATUS *) NULL);
}

/**
 ** load_str() - LOAD STRING WHERE ALLOC'D STRING MAY BE
 ** unload_str() - REMOVE POSSIBLE ALLOC'D STRING
 **/

void
load_str(char **pdst, char *src)
{
	ENTRY ("load_str")

	if (*pdst)
		Free (*pdst);
	*pdst = Strdup(src);
	return;
}

void
unload_str(char **pdst)
{
	ENTRY ("unload_str")

	if (*pdst)
		Free (*pdst);
	*pdst = 0;
	return;
}

/**
 ** unload_list() - REMOVE POSSIBLE ALLOC'D LIST
 **/

void
unload_list(char ***plist)
{
	ENTRY ("unload_list")

	if (*plist)
		freelist (*plist);
	*plist = 0;
	return;
}

/**
 ** load_sdn() - LOAD STRING WITH ASCII VERSION OF SCALED DECIMAL NUMBER
 **/

void
load_sdn(char **p, SCALED sdn)
{
	ENTRY ("load_sdn")

	if (!p)
		return;

	if (*p)
		Free (*p);
	*p = 0;

	if (sdn.val <= 0 || 999999 < sdn.val)
		return;

	*p = Malloc(sizeof("999999.999x"));
	sprintf (
		*p,
		"%.3f%s",
		sdn.val,
		(sdn.sc == 'c'? "c" : (sdn.sc == 'i'? "i" : ""))
	);

	return;
}

/**
 ** Getform() - EASIER INTERFACE TO "getform()"
 **/

_FORM *
Getform(char *form)
{
	ENTRY ("Getform")

	static _FORM		_formbuf;

	FORM			formbuf;

	FALERT			alertbuf;

	int			ret;


	while (
		(ret = getform(form, &formbuf, &alertbuf, (FILE **)0)) == -1
	     && errno == EINTR
	)
		;
	if (ret == -1)
		return (0);

	_formbuf.plen = formbuf.plen;
	_formbuf.pwid = formbuf.pwid;
	_formbuf.lpi = formbuf.lpi;
	_formbuf.cpi = formbuf.cpi;
	_formbuf.np = formbuf.np;
	_formbuf.chset = formbuf.chset;
	_formbuf.mandatory = formbuf.mandatory;
	_formbuf.rcolor = formbuf.rcolor;
	_formbuf.comment = formbuf.comment;
	_formbuf.conttype = formbuf.conttype;
	_formbuf.name = formbuf.name;
	_formbuf.paper = formbuf.paper;
	_formbuf.isDefault = formbuf.isDefault;

	if ((_formbuf.alert.shcmd = alertbuf.shcmd)) {
		_formbuf.alert.Q = alertbuf.Q;
		_formbuf.alert.W = alertbuf.W;
	} else {
		_formbuf.alert.Q = 0;
		_formbuf.alert.W = 0;
	}

	return (&_formbuf);
}

/**
 ** Getprinter()
 ** Getrequest()
 ** Getuser()
 ** Getclass()
 ** Getpwheel()
 ** Getsecure()
 ** Getsystem()
 ** Loadfilters()
 **/

PRINTER *
Getprinter(char *name)
{
	ENTRY ("Getprinter")

	register PRINTER	*ret;

	while (!(ret = getprinter(name)) && errno == EINTR)
		;
	return (ret);
}

REQUEST *
Getrequest(char *file)
{
	ENTRY ("Getrequest")

	register REQUEST	*ret;

	while (!(ret = getrequest(file)) && errno == EINTR)
		;
	return (ret);
}

USER *
Getuser(char *name)
{
	ENTRY ("Getuser")

	register USER		*ret;

	while (!(ret = getuser(name)) && errno == EINTR)
		;
	return (ret);
}

CLASS *
Getclass(char *name)
{
	ENTRY ("Getclass")

	register CLASS		*ret;

	while (!(ret = getclass(name)) && errno == EINTR)
		;
	return (ret);
}

PWHEEL *
Getpwheel(char *name)
{
	ENTRY ("Getpwheel")

	register PWHEEL		*ret;

	while (!(ret = getpwheel(name)) && errno == EINTR)
		;
	return (ret);
}

SECURE *
Getsecure(char *file)
{
	ENTRY ("Getsecure")

	register SECURE		*ret;
	register SSTATUS        *pstat; /* fix for bugid 1103890 */

	while (!(ret = getsecure(file)) && errno == EINTR)
		;

	/* Modification for bugid 1103890. We check and make sure that
	 * the secure structure contains a system name that we expect.
	 * If ret is non-null, we inspect the system element, and see
	 * if it belongs to any system that we know. If it doesn't,
	 * we return NULL.
	 */

	if (ret == (SECURE *) NULL) {
		return ((SECURE *) ret);
	}

	pstat = search_stable(ret->system);
	if (pstat == ((SSTATUS *) NULL)) {
		note("Getsecure: bad system name!\n");
		return ((SECURE *) NULL);
	}
 
        return ((SECURE *) ret);
}

SYSTEM *
Getsystem(char *file)
{
	ENTRY ("Getsystem")

	SYSTEM		*ret;

	while (!(ret = getsystem(file)) && errno == EINTR)
		;
	return (ret);
}

int
Loadfilters(char *file)
{
	ENTRY ("Loadfilters")

	register int		ret;

	while ((ret = loadfilters(file)) == -1 && errno == EINTR)
		;
	return (ret);
}

/**
 ** free_form() - FREE MEMORY ALLOCATED FOR _FORM STRUCTURE
 **/

void
free_form(register _FORM *pf)
{
	ENTRY ("free_form")

	if (!pf)
		return;
	if (pf->chset)
		Free (pf->chset);
	if (pf->rcolor)
		Free (pf->rcolor);
	if (pf->comment)
		Free (pf->comment);
	if (pf->conttype)
		Free (pf->conttype);
	if (pf->name)
		Free (pf->name);
	if (pf->paper)
		Free (pf->paper);
	pf->name = 0;
	if (pf->alert.shcmd)
		Free (pf->alert.shcmd);
	return;
}

/**
 ** getreqno() - GET NUMBER PART OF REQUEST ID
 **/

char *
getreqno(char *req_id)
{
	/* causes problems with child and postscript printers
	 * ENTRY ("getreqno")
	 */

	register char		*cp;


	if (!(cp = strrchr(req_id, '-')))
		cp = req_id;
	else
		cp++;
	return (cp);
}

/* Start Modifications for bugid 1103890. */

/* Putsecure():	Insurance for writing out the secure request file.
 *	input:	char ptr to name of the request file,
 *		ptr to the SECURE structure to be written.
 *	ouput:	0 if successful, -1 otherwise.
 *
 *	Description:
 *		The normal call to putsecure() is woefully lacking.
 *		See bugid 1103890. The bottom line here is that there
 *		is no way to make sure that the file has been written out
 *		as expected. This can cause rude behaviour later on.
 *
 *		This routine calls putsecure(), and then does a getsecure().
 *		The results are compared to the original structure. If the
 *		info obtained by getsecure() doesn't match, we retry a few
 *		times before giving up (presumably something is very seriously
 *		wrong at that point).
 */


int
Putsecure(char *file, SECURE *secbufp)
{
	ENTRY ("Putsecure")

	SECURE	*pls;
	int	retries = 5;	/* # of attempts			*/
	int	status;		/*  0 = success, nonzero otherwise	*/


	while (retries--) {
		status = 1;	/* assume the worst, hope for the best	*/
		if (putsecure(file, secbufp) == -1) {
			rmsecure(file);
			continue;
		}

		if ((pls = getsecure(file)) == (SECURE *) NULL) {
			rmsecure(file);
			status = 2;
			continue;
		}

		/* now compare each field	*/
		if (strcmp(pls->req_id, secbufp->req_id) != 0) {
			rmsecure(file);
			status = 3;
			continue;
		}

		if (pls->uid != secbufp->uid) {
			rmsecure(file);
			status = 4;
			continue;
		}

		if (strcmp(pls->user, secbufp->user) != 0) {
			rmsecure(file);
			status = 5;
			continue;
		}

		if (pls->gid != secbufp->gid) {
			rmsecure(file);
			status = 6;
			continue;
		}

		if (pls->size != secbufp->size) {
			rmsecure(file);
			status = 7;
			continue;
		}

		if (pls->date != secbufp->date) {
			rmsecure(file);
			status = 8;
			continue;
		}

		if (strcmp(pls->system, secbufp->system) != 0) {
			rmsecure(file);
			status = 9;
			continue;
		}
		status = 0;
		freesecure(pls);
		break;
	}

	if (status != 0) {
		note("Putsecure failed, status=%d\n", status);
		return -1;
	}

	return 0;
}

/* End Modifications for bugid 1103890. */

void GetRequestFiles(REQUEST *req, char *buffer, int length)
{
	char buf[BUFSIZ];

	memset(buf, 0, sizeof(buf));

	if (req->title) {
		char *r = req->title;
		char *ptr = buf;

		while ( *r && strncmp(r,"\\n",2)) {
		  	*ptr++ = *r++;
		}
	} else if (req->file_list)
		strcpy(buf, *req->file_list);
	
	if (*buf == NULL || !strncmp(buf, SPOOLDIR, sizeof(SPOOLDIR)-1))
		strcpy(buf, "<File name not available>");

	if (strlen(buf) > (size_t) 24) {
		char *r;

		if (r = strrchr(buf, '/'))
			r++;
		else
			r = buf;
	
		sprintf(buffer,"%-.24s", r);	
	} else
		strcpy(buffer, buf);	
	return;
}


/**
 ** _Malloc()
 ** _Realloc()
 ** _Calloc()
 ** _Strdup()
 ** _Free()
 **/

void			(*lp_alloc_fail_handler)( void ) = 0;

typedef void *alloc_type;
extern etext;

alloc_type
_Malloc(size_t size, const char *file, int line)
{
	alloc_type		ret;

	ret = malloc(size);
	if (!ret) {
		if (lp_alloc_fail_handler)
			(*lp_alloc_fail_handler)();
		errno = ENOMEM;
	}
	return (ret);
}

alloc_type
_Realloc(void *ptr, size_t size, const char *file, int line)
{
	alloc_type		ret	= realloc(ptr, size);

	if (!ret) {
		if (lp_alloc_fail_handler)
			(*lp_alloc_fail_handler)();
		errno = ENOMEM;
	}
	return (ret);
}

alloc_type
_Calloc(size_t nelem, size_t elsize, const char *file, int line)
{
	alloc_type		ret	= calloc(nelem, elsize);

	if (!ret) {
		if (lp_alloc_fail_handler)
			(*lp_alloc_fail_handler)();
		errno = ENOMEM;
	}
	return (ret);
}

char *
_Strdup(const char *s, const char *file, int line)
{
	char *			ret;

	if (!s)
		return( (char *) 0);

	ret = strdup(s);

	if (!ret) {
		if (lp_alloc_fail_handler)
			(*lp_alloc_fail_handler)();
		errno = ENOMEM;
	}
	return (ret);
}

void
_Free(void *ptr, const char *file, int line)
{

#ifdef DEBUG
	if (ptr < (void *) etext || ptr > sbrk(0)) {
		note("_Free %x %s %d\n", ptr, file, line);
		note("%x %lxd\n", etext, sbrk(0));
		fail("Freeing garbage.\n");
	}
#endif

	free (ptr);
	return;
}
