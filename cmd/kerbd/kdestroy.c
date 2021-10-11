#ident	"@(#)kdestroy.c	1.2	91/11/12 SMI"
/*
 * $Source: /mit/kerberos/src/kuser/RCS/kdestroy.c,v $
 * $Author: steiner $ 
 *
 * Copyright 1987, 1988 by the Massachusetts Institute of Technology. 
 *
 * For copying and distribution information, please see the file
 * <mit-copyright.h>. 
 *
 * This program causes Kerberos tickets to be destroyed.
 * Options are: 
 *
 *   -q[uiet]	- no bell even if tickets not destroyed
 *   -f[orce]	- no message printed at all 
 */

#include <kerberos/mit-copyright.h>

#ifndef	lint
static char rcsid_kdestroy_c[] =
"$Header: kdestroy.c,v 4.5 88/03/18 15:16:02 steiner Exp $";
#endif	lint

#include <stdio.h>
#include <kerberos/krb.h>
#include <rpc/types.h>
#include <rpc/auth.h>
#include <nfs/nfs.h>
#include <nfs/nfssys.h>
#ifdef BSD42
#include <strings.h>
#else
#include <string.h>
#define rindex strrchr
#endif BSD42


static char *pname;

/* for revoking kernel NFS credentials */
struct nfs_revauth_args nra;

static usage()
{
    fprintf(stderr, "Usage: %s [-fqn]\n", pname);
    exit(1);
}

main(argc, argv)
    char   *argv[];
{
    int     fflag=0, qflag=0, k_errno;
    int	    nflag=0, rev_err;
    register char *cp;

    cp = rindex (argv[0], '/');
    if (cp == NULL)
	pname = argv[0];
    else
	pname = cp+1;

    for (; argc > 1 && argv[1][0] == '-'; argc--, argv++) {
	for (cp = &argv[1][1]; *cp; cp++) {
	    switch(*cp) {
		case 'f':
		    ++fflag;			/* force: no output */
		    break;
		case 'q':
		    ++qflag;			/* quiet: don't ring bell */
		    break;
		case 'n':
		    ++nflag;			/* don't revoke NFS creds */
		    break;
		default:
		    usage();
		    break;
	    }
	}
    }
    if (argc > 1)
	usage();

    k_errno = dest_tkt();

    /*
     *  Now invalidate cached kernel credentials
     */
    rev_err = 0;
    if (!nflag) {
        nra.authtype = AUTH_KERB;		/* only revoke kerb creds */
        nra.uid = getuid();			/* use the real uid */
        rev_err = _nfssys(NFS_REVAUTH, &nra);	/* revoke the creds */
	if (rev_err < 0 && !fflag)
	    perror("Warning: NFS credentials NOT destroyed");
    }

    if (fflag) {
	if (rev_err < 0 || (k_errno != 0 && k_errno != RET_TKFIL))
	    exit(1);
	else
	    exit(0);
    } else {
	if (k_errno == 0)
	    printf("Tickets destroyed.\n");
	else if (k_errno == RET_TKFIL)
	    fprintf(stderr, "No tickets to destroy.\n");
	else {
	    fprintf(stderr, "Tickets NOT destroyed.\n");
	    if (!qflag)
		fprintf(stderr, "\007");
	    exit(1);
	}
    }
    if (rev_err < 0)
	exit(1);
    else
	exit(0);
}
