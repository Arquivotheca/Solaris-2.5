/*
 * warmstart.c
 * Allows for gathering of registrations from a earlier dumped file.
 *
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ident	"@(#)warmstart.c	1.6	94/09/21 SMI"

#include <stdio.h>
#include <rpc/rpc.h>
#include <rpc/rpcb_prot.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef PORTMAP
#include <netinet/in.h>
#include <rpc/pmap_prot.h>
#endif
#include "rpcbind.h"
#include <sys/syslog.h>

/* These files keep the pmap_list and rpcb_list in XDR format */
#define	RPCBFILE	"/tmp/rpcbind.file"
#ifdef PORTMAP
#define	PMAPFILE	"/tmp/portmap.file"
#endif

static bool_t write_struct();
static bool_t read_struct();

static bool_t
write_struct(filename, structproc, list)
	char *filename;
	xdrproc_t structproc;
	void *list;
{
	FILE *fp;
	XDR xdrs;
	mode_t omask;

	omask = umask(077);
	fp = fopen(filename, "w");
	if (fp == NULL) {
		int i;

		for (i = 0; i < 10; i++)
			close(i);
		fp = fopen(filename, "w");
		if (fp == NULL) {
			syslog(LOG_ERR,
				"cannot open file = %s for writing", filename);
			syslog(LOG_ERR, "cannot save any registration");
			return (FALSE);
		}
	}
	(void) umask(omask);
	xdrstdio_create(&xdrs, fp, XDR_ENCODE);

	if (structproc(&xdrs, list) == FALSE) {
		syslog(LOG_ERR, "rpcbind: xdr_%s: failed", filename);
		fclose(fp);
		return (FALSE);
	}
	XDR_DESTROY(&xdrs);
	fclose(fp);
	return (TRUE);
}

static bool_t
read_struct(filename, structproc, list)
	char *filename;
	xdrproc_t structproc;
	void *list;
{
	FILE *fp;
	XDR xdrs;
	struct stat sbuf;

	if (stat(filename, &sbuf) != 0) {
		fprintf(stderr,
		"rpcbind: cannot stat file = %s for reading\n", filename);
		goto error;
	}
	if ((sbuf.st_uid != 0) || (sbuf.st_mode & S_IRWXG) ||
	    (sbuf.st_mode & S_IRWXO)) {
		fprintf(stderr,
		"rpcbind: invalid permissions on file = %s for reading\n",
			filename);
		goto error;
	}
	fp = fopen(filename, "r");
	if (fp == NULL) {
		fprintf(stderr,
		"rpcbind: cannot open file = %s for reading\n", filename);
		goto error;
	}
	xdrstdio_create(&xdrs, fp, XDR_DECODE);

	if (structproc(&xdrs, list) == FALSE) {
		fprintf(stderr, "rpcbind: xdr_%s: failed\n", filename);
		fclose(fp);
		goto error;
	}
	XDR_DESTROY(&xdrs);
	fclose(fp);
	return (TRUE);

error:	fprintf(stderr, "rpcbind: will start from scratch\n");
	return (FALSE);
}

void
write_warmstart()
{
	(void) write_struct(RPCBFILE, xdr_rpcblist_ptr, &list_rbl);
#ifdef PORTMAP
	(void) write_struct(PMAPFILE, xdr_pmaplist_ptr, &list_pml);
#endif

}

void
read_warmstart()
{
	rpcblist_ptr tmp_rpcbl = NULL;
#ifdef PORTMAP
	pmaplist_ptr tmp_pmapl = NULL;
#endif
	int ok1, ok2 = TRUE;

	ok1 = read_struct(RPCBFILE, xdr_rpcblist_ptr, &tmp_rpcbl);
	if (ok1 == FALSE)
		return;
#ifdef PORTMAP
	ok2 = read_struct(PMAPFILE, xdr_pmaplist_ptr, &tmp_pmapl);
#endif
	if (ok2 == FALSE) {
		xdr_free((xdrproc_t) xdr_rpcblist_ptr, (char *)&tmp_rpcbl);
		return;
	}
	xdr_free((xdrproc_t) xdr_rpcblist_ptr, (char *)&list_rbl);
	list_rbl = tmp_rpcbl;
#ifdef PORTMAP
	xdr_free((xdrproc_t) xdr_pmaplist_ptr, (char *)&list_pml);
	list_pml = (pmaplist *)tmp_pmapl;
#endif
}
