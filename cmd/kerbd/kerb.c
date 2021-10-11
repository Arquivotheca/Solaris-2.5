#ident	"@(#)kerb.c	1.2	91/08/13 SMI"
/*
 *  Test client for kerbd.  This program is not shipped.
 *
 *  Copyright 1990,1991 Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <rpc/rpc.h>
#include "kerbd.h"

char	*Prog;
CLIENT	*Clnt, *getkerbd_handle();

main(argc, argv)
int argc;
char **argv;
{
	char *cmd;
	char *server = "localhost";

	Prog = argv[0];
	if (argc == 1)
		usage();
	argc--;
	argv++;
	
	if ((Clnt = getkerbd_handle()) == NULL) {
		clnt_pcreateerror(server);
		exit(1);
	}
	fprintf(stderr, "got clnt handle for %s...\n", server);

	if (argc == 0)
		usage();

	cmd = argv[0];
	argc--;
	argv++;

	if      (!strcmp(cmd, "getkcred") || !strcmp(cmd, "gk"))
		getkcred(argc, argv);
	else if (!strcmp(cmd, "setkcred") || !strcmp(cmd, "sk"))
		setkcred(argc, argv, 0);
	else if (!strcmp(cmd, "setget") || !strcmp(cmd, "sg"))
		setkcred(argc, argv, 1);
	else if (!strcmp(cmd, "getucred") || !strcmp(cmd, "gu"))
		getucred(argc, argv);
	else
		usage();
	exit(0);
}

getkcred(argc, argv)
char **argv;
{
	kgetkcred_arg	 gka;
	KTEXT_ST ticket;

	if (argc != 2) usage();

	ticket.length = 0;
	gka.ticket.TICKET_len = ticket.length;
	gka.ticket.TICKET_val = (char *)ticket.dat;
	gka.sname = argv[0];
	gka.sinst = argv[1];
	gka.faddr = 0x86427531;

	_getkcred(&gka);
}

_getkcred(gkp)
kgetkcred_arg *gkp;
{
	kgetkcred_res	*gkr;
	kgetkcred_resd	*gkd;

	gkr = kgetkcred_4(gkp, Clnt);
	if (gkr == NULL) {
		clnt_perror(Clnt, "getkcred");
	} else if (gkr->status == KSUCCESS) {
		gkd = &gkr->kgetkcred_res_u.res;
		printf("return `%s.%s@%s' chksum %x time %d\n",
			gkd->pname, gkd->pinst, gkd->prealm,
			gkd->checksum, gkd->time_sec);
		prtkt(&gkd->reply);
		printf("key %x.%x life %d addr %x\n",
			gkd->session.key.high,
			gkd->session.key.low,
			gkd->life, gkd->address);
	} else {
		printf("server ret err %d (%s)\n",
			gkr->status,
			gkr->status > 0 ? krb_err_txt[gkr->status]
					: "system error");
	}
}

setkcred(argc, argv, getit)
char **argv;
{
	ksetkcred_arg	 ska;
	ksetkcred_res	*skr;
	ksetkcred_resd	*skd;

	if (argc != 4) usage();

	ska.sname = argv[0];
	ska.sinst = argv[1];
	ska.srealm = argv[2];
	ska.cksum = atoi(argv[3]);
	(void)setuid(ska.cksum);
	auth_destroy(Clnt->cl_auth);
	Clnt->cl_auth = authunix_create_default();

	skr = ksetkcred_4(&ska, Clnt);
	if (skr == NULL) {
		clnt_perror(Clnt, "setkcred");
	} else if (skr->status == KSUCCESS) {
		skd = &skr->ksetkcred_res_u.res;
		prtkt(&skd->ticket);
		printf("key %x.%x\n",
			skd->key.key.high,
			skd->key.key.low);
	} else {
		printf("server ret err %d (%s)\n",
			skr->status,
			skr->status > 0 ? krb_err_txt[skr->status]
					: "system error");
	}

	/* test getting ticket */
	if (getit && skr->status == KSUCCESS) {
		kgetkcred_arg	 gka;

		gka.ticket = skd->ticket;
		gka.sname = ska.sname;
		gka.sinst = ska.sinst;
		gka.faddr = 0;		/* XXX */
		printf("Calling getkcred...\n");
		_getkcred(&gka);
	}
}

getucred(argc, argv)
char **argv;
{
	kgetucred_arg	 gua;
	kgetucred_res	*gur;
	u_int		 ngrps, *gp, *grps;

	if (argc != 1) usage();
	gua.pname = argv[0];

	gur = kgetucred_4(&gua, Clnt);
	if (gur == NULL) {
		clnt_perror(Clnt, "getucred");
	} else if (gur->status == UCRED_OK) {
		ngrps = gur->kgetucred_res_u.cred.grplist.grplist_len;
		printf("return: uid %d gid %d grplist_len %d\n",
			gur->kgetucred_res_u.cred.uid,
			gur->kgetucred_res_u.cred.gid,
			ngrps);
		if (ngrps) {
			printf("\tgroups:");
			grps = gur->kgetucred_res_u.cred.grplist.grplist_val;
			for (gp = grps; gp < &grps[ngrps]; gp++)
				printf(" %d", *gp);
			printf("\n");
		}
	} else {
		printf("username unknown\n");
	}
}

usage()
{
	fprintf(stderr, "\
usage: %s [opt] getkcred <name> <instance>\n\
       %s [opt] setkcred <name> <instance> <realm> <uid>\n\
       %s [opt] setget <name> <instance> <realm> <uid>\n\
       %s [opt] getucred <name>\n\
",
		Prog, Prog, Prog, Prog);
	exit(1);
}

prtkt(tkt)
TICKET *tkt;
{
        u_char *p = (u_char *)tkt->TICKET_val; 
        int len; 
 
        printf("TICKET len %d\n", tkt->TICKET_len); 
#ifdef notdef
	if (tkt->TICKET_len == 0)
		return;
        printf("  vers %d", *p++);   
        printf(" type %x ", *p++);
        printf(" kvno %d ", *p++);
        len = strlen(p); 
        printf(" realm len %d `%s'\n", len, p); 
        p += (len + 1); 
        printf("  tl %d", *p++); 
        printf(" idl %d\n", *p++);
#endif notdef
}
