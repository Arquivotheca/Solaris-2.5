/*
 *
 * 		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1991  Sun Microsystems, Inc
 *
 */

/* This is a modified getmaskbyaddr() that simulates nsswitch.conf behaviour */
/* without using dlopen() etc. */


#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <string.h>
#include <errno.h>

#include <limits.h>
#include <nsswitch.h>
#include <syslog.h>

static struct translator {
	char *(*lookup)();
	char	module_name[8];	/* see declarations below and make sure they fit */
	void	*module_handle;
	struct translator	*next;
};

static char *nisplus_getmaskbyaddr();
static char *nis_getmaskbyaddr();
static char *files_getmaskbyaddr();
static struct translator t3 = { nisplus_getmaskbyaddr, "nisplus", 0, 0};
static struct translator t2 = { nis_getmaskbyaddr, "nis", 0, &t3};
static struct translator t1 = { files_getmaskbyaddr, "files", 0, &t2};

/* default policy is similar to SunOS 4.1 */
static struct __nsw_lookup lkp2 = { "files", {1, 1, 1, 1}, NULL, NULL};
static struct __nsw_lookup lkp1 = { "nis", {1, 1, 0, 0}, NULL, &lkp2};
static struct __nsw_switchconfig netmasks_default =
	{ 0, "netmasks", 2, &lkp1 };

static struct translator *xlate_list = &t1;
static struct __nsw_switchconfig *conf = NULL;

static struct translator *load_xlate();


/*
 * get the appropriate network mask entry given an address
 */

	/* this subroutine is written like a dummy frontend routine to name service switch */
char *
getmaskbyaddr(netname, outbuf, outbuflen)
	char *netname;
	char *outbuf;
	int outbuflen;
{
	struct	translator   *xlate; /* pointer to translator list */
	char	*cur_module;
	struct	__nsw_lookup *lkp;
	int reason;
	enum	__nsw_parse_err pserr;
	int	nserr = __NSW_NOTFOUND;
	char	*rval = 0;	/* defualt error return */
#ifdef DEBUG
	char *dl;
#endif

	/* validate input */
	if (netname == (char *) 0)
		return (rval);	/* error return */

	if (!conf &&
	    (conf = __nsw_getconfig(__NSW_NETMASKS_DB, &pserr)) == NULL)
		conf = &netmasks_default;

	for (lkp = conf->lookups; lkp; lkp = lkp->next) {
		cur_module = lkp->service_name;
		nserr = __NSW_SUCCESS;
		xlate = load_xlate(&nserr, cur_module);
		if (!xlate && __NSW_ACTION(lkp, nserr) == __NSW_RETURN)
			break;
		if (!xlate)
			continue;
		nserr = __NSW_SUCCESS;
		if (!(xlate->lookup))
			nserr = __NSW_UNAVAIL;
		else
			rval = (char *) (*(xlate->lookup))(&nserr, netname, outbuf, outbuflen);

		if (__NSW_ACTION(lkp, nserr) == __NSW_RETURN)
			break;
	}
	return (rval);
}

/* this routine is a dummy load_xlate() in the style used by nsswitch frontends */
static struct translator *
load_xlate(nserr, module_name)
	int *nserr;
	char *module_name;
{
	struct  translator   *xlate;


	for (xlate = xlate_list; xlate; xlate = xlate->next)
		if (strcmp(module_name, xlate->module_name) == 0)
			return (xlate);

	*nserr = __NSW_UNAVAIL;
	return (NULL);
}


/* this routine is like a dummy nis backend to nsswitch() */
static char *
nis_getmaskbyaddr(errp,netname, outbuf, outbuflen)
     int *errp;
     char *netname;
     char *outbuf;
     int outbuflen;
{
	static char *ypDomain = NULL;
	char *out, *strtok();
	int keylen, outsize, stat;
	/* Try NIS now */
	if (ypDomain == NULL)
		yp_get_default_domain(&ypDomain);
	if (ypDomain == NULL) {
		*errp = __NSW_UNAVAIL;
		return(0);	/* error return */
	}
	keylen = strlen(netname);
	stat = yp_match(ypDomain, "netmasks.byaddr", 
			netname, keylen, &out, &outsize);
	if (stat == 0) {
		strncpy(outbuf, out, outbuflen);
		free(out); /* nis allocates for output */
		return(outbuf);
	}
	*errp = switch_err(stat);
	return(0);
}


/* this routine is like a dummy files backend to nsswitch */

static char *
files_getmaskbyaddr(errp,netname, outbuf, outbuflen)
     int *errp;
     char *netname;
     char *outbuf;
     int outbuflen;
{
	char *out;
	FILE *f;
	ulong netmask, compare;


	f = fopen("/etc/netmasks", "r");
	if (f == NULL) {
		*errp = __NSW_UNAVAIL;
		return(0);	/* error return */
	}
	netmask = inet_addr(netname);
	while (fgets(outbuf, outbuflen-1, f)) {
		out = strtok(outbuf, " \t\n");
		if (!out)
			continue;
		compare = inet_addr(out);
		if (netmask == compare) {
			out = strtok(NULL, " \t\n");
			*errp = __NSW_SUCCESS;

			return(out);
		}
	}
	fclose(f);
	*errp = __NSW_NOTFOUND;
	return(0);
}

/* following code for nisplus dummy nsswitch backend  */
#include "nisplus_common.h"
#include "nisplus_tables.h"


static struct netmaskdata {
	nis_libcinfo	ni;
} *netmaskdata, *_netmaskdata();

static struct netmaskdata *
_netmaskdata()
{
	register struct netmaskdata *d = netmaskdata;

	if (d == 0) {
		d = (struct netmaskdata *)calloc(1, sizeof (struct netmaskdata));
		if (d == 0) {
			return 0;
		}
		if (nis_libcinfo_init(&d->ni, NT_NETMASK_RDN) == 0) {
			free(d);
			return 0;
		}
		netmaskdata = d;
	}
	return d;
}
/* this routine is like a dummy nisplus backend to nsswitch */
static char *
nisplus_getmaskbyaddr(errp,netname, outbuf, outbuflen)
     int *errp;
     char *netname;
     char *outbuf;
     int outbuflen;
{
	struct netmaskdata *d = _netmaskdata();
	nis_result	*r = 0;
	nis_object	*obj;
	int		nobj;
	struct entry_col *ecol;
	char		*val;
	int		len;

	if (d == 0) {
		*errp = __NSW_UNAVAIL;
		goto error_ret;
	}
	r = nisplus_match(&d->ni, NT_NETMASK_TAG_ADDR, netname, strlen(netname) + 1);
	if (r == 0) {
		*errp = __NSW_UNAVAIL;
		goto error_ret;
	}

	/* ==== should use new table_error_to_switch() */
	switch (NIS_RES_STATUS(r)) {
	    case NIS_SUCCESS:
	    case NIS_S_SUCCESS:
		break;

	    case NIS_NOTFOUND:
	    case NIS_S_NOTFOUND:	/* ==== ?? right thing to do ?? */
		*errp = __NSW_NOTFOUND;
		goto error_ret;

	    case NIS_TRYAGAIN:		/* ==== ?? right thing to do ?? */
		*errp = __NSW_TRYAGAIN;
		goto error_ret;

	    default:
		*errp = __NSW_UNAVAIL;
		goto error_ret;
	}

	obj  = NIS_RES_OBJECT(r);
	nobj = NIS_RES_NUMOBJ(r);

	if (nobj == 0) {
		/* NIS+ must be curdled, no? */
		*errp = __NSW_UNAVAIL;
		goto error_ret;
	}

	/* ==== If we got multiple objects back, blithely ignore extras */

#define EC_LEN(ecp,ndx)		((ecp)[ndx].ec_value.ec_value_len)
#define EC_VAL(ecp,ndx)		((ecp)[ndx].ec_value.ec_value_val)
#define EC_SET(ecp,ndx,l,v)	((l) = EC_LEN(ecp,ndx), (v) = EC_VAL(ecp,ndx))

	if (obj->zo_data.zo_type != ENTRY_OBJ ||
	    /* ==== should check entry type? */
	    obj->EN_data.en_cols.en_cols_len < NT_NETMASK_COL) {
		/* ==== ?? namespace is curdled, should do UNAVAIL ??*/
		*errp = __NSW_NOTFOUND;
		goto error_ret;
	}
	ecol = obj->EN_data.en_cols.en_cols_val;

	EC_SET(ecol, NT_NETMASK_NDX_MASK, len, val);
	if (len < 2) {
		/* zero length or a null string return */
		*outbuf = 0;	/* making it a null for sure */
	} else {
		val[len - 1] = 0;
		strncpy(outbuf, val, outbuflen);
	}
	nis_freeresult(r);
	*errp = __NSW_SUCCESS;
	return(outbuf);

      error_ret:
	if (r != 0) {
		nis_freeresult(r);
	}
	return(0);		/* error return */
}


