/*
 * Copyright (c) 1995 Sun Microsystems, Inc.  All Rights Reserved.
 */

#pragma ident "@(#)command.c	1.20	95/06/13 SMI"

/*
 * adb - command parser
 */

#include "adb.h"
#include <stdio.h>
#include "fio.h"
#include "fpascii.h"

char	eqformat[512] = "z";
char	stformat[512] = "X\"= \"^i";

#define	QUOTE	0200
#define	STRIP	0177

int	lastcom = '=';

command(buf, defcom)
	char *buf, defcom;
{
	int itype, ptype, modifier, reg;
	int fourbyte, eqcom, atcom;
	char wformat[1];
	char c, savc;
	int w, savdot;
	char *savlp = lp;
	int locval, locmsk;
#ifndef KADB
	extern char *prompt;
#endif

	db_printf(7, "command: buf=%X, defcom='%c'", buf, defcom);
	if (buf != NULL) {
		if (*buf == '\n') {
			db_printf(5, "command: returns 0");
			return 0;
		}
		lp = buf;
	}
	do {
		adb_raddr.ra_raddr = 0;		/* initialize to no */
		if (hadaddress = expr(0)) {
			dot = expv;
			ditto = dot;
		}
		address = dot;
		if (rdc() == ',' && expr(0)) {
			hadcount = 1;
			count = expv;
		} else {
			hadcount = 0;
			count = 1;
			lp--;
		}
		if (!eol(rdc()))
			lastcom = lastc;
		else {
			if (hadaddress == 0)
				dot = inkdot(dotinc);
			lp--;
			lastcom = defcom;
		}

		switch (lastcom & STRIP) {

		case '?':
#ifndef KADB
			if (kernel == NOT_KERNEL) {
				itype = ISP; ptype = ISYM;
				goto trystar;
			}	/* fall through in case of adb -k */
#endif !KADB

		case '/':
			itype = DSP; ptype = DSYM;
			goto trystar;

		case '=':
			itype = NSP; ptype = 0;
			goto trypr;

		case '@':
			itype = SSP; ptype = 0;
			goto trypr;

trystar:
			if (rdc() == '*')
				lastcom |= QUOTE;
			else
				lp--;
			if (lastcom & QUOTE) {
				itype |= STAR;
				ptype = (DSYM+ISYM) - ptype;
			}

trypr:
			fourbyte = 0;
			eqcom = lastcom=='=';
			atcom = lastcom=='@';
			c = rdc();
			if ((eqcom || atcom) && strchr("mLlWw", c))
				error(eqcom ?
				    "unexpected '='" : "unexpected '@'");
			switch (c) {

			case 'm': {
				int fcount, *mp;
				struct map *smap;
				struct map_range *mpr;

				/* need a syntax for setting any map range - 
				** perhaps ?/[*|0-9]m  b e f fn 
				** where the digit following the ?/ selects the map
				** range - but it's too late for 4.0
				*/
				smap = (itype&DSP?&datmap:&txtmap);
				mpr = smap->map_head;
				if (itype&STAR)
					mpr = mpr->mpr_next;
				mp = &(mpr->mpr_b); fcount = 3;
				while (fcount && expr(0)) {
					*mp++ = expv;
					fcount--;
				}
				if (rdc() == '?')
					mpr->mpr_fd = fsym;
				else if (lastc == '/')
					mpr->mpr_fd = fcor;
				else
					lp--;
				}
				break;
			case 'L':
				fourbyte = 1;
			case 'l':
				dotinc = (fourbyte ? 4 : 2);
				savdot = dot;
				(void) expr(1); locval = expv;
				locmsk = expr(0) ? expv : -1;
				if (!fourbyte) {
					locmsk = (locmsk << 16 );
					locval = (locval << 16 );
				}
				for (;;) {
#if defined(KADB)
					tryabort(1); /* Give user a chance to stop */
#endif
					w = get(dot, itype);
					if (errflg || interrupted)
						break;
					if ((w&locmsk) == locval)
						break;
					dot = inkdot(dotinc);
				}
				if (errflg) {
					dot = savdot;
					errflg = "cannot locate value";
				}
				psymoff(dot, ptype, "");
				break;
			case 'W':
				fourbyte = 1;
			case 'w':
				wformat[0] = lastc; (void) expr(1);
				do {
#if defined(KADB)
				    tryabort(1); /* Give user a chance to stop */
#endif
				    savdot = dot;
				    psymoff(dot, ptype, ":%16t");
				    (void) exform(1,wformat,itype,ptype);
				    errflg = 0; dot = savdot;
				    if (fourbyte)
					put(dot, itype, expv);
				    else {
					/*NONUXI*/
					long longvalue = get(dot, itype);

					*(short*)&longvalue = (short)expv;
					put(dot, itype, longvalue);
				    }
				    savdot = dot;
				    printf("=%8t");
				    (void) exform(1,wformat,itype,ptype);
				    newline();
				} while (expr(0) && errflg == 0);
				dot = savdot;
				chkerr();
				break;

			default:
			        if (ext_slash(c, buf, defcom, eqcom, atcom,
				    itype, ptype))
				        break;
				lp--;
				getformat(eqcom ? eqformat : stformat);
#if 0
				if (atcom) {
					if (indexf(XFILE(dot)) == 0)
						error("bad file index");
					printf("\"%s\"+%d:%16t",
					    indexf(XFILE(dot))->f_name,
					    XLINE(dot));
				} else
#endif 0

				if (!eqcom)
					psymoff(dot, ptype, ":%16t");
				scanform(count, (eqcom?eqformat:stformat),
				    itype, ptype);
			}
			break;

		case '>':
			lastcom = 0; savc = rdc();
			reg = getreg(savc);
			if (reg >= 0) {
				if (!writereg(reg, dot)) {
#ifndef KADB
					perror(regnames[reg]);
#else
					error("could not write");
#endif
					db_printf(3, "command: errno=%D",
									errno);
				}
				break;
			}
			modifier = varchk(savc);
			if (modifier == -1)
				error("bad variable");
			var[modifier] = dot;
			break;

#ifndef KADB
		case '!':
			lastcom = 0;
			shell();
			break;
#endif !KADB

		case '$':
			lastcom = 0;
			printtrace(nextchar());
			break;

		case ':':
			/* double colon means extended command */
			if (rdc() == ':') {
				extended_command();
				lastcom = 0;
				break;
			}
			lp--;
			if (!executing) {
				executing = 1;
				db_printf(9, "command: set executing=1");
				subpcs(nextchar());
				executing = 0;
				db_printf(9, "command: set executing=0");
				lastcom = 0;
			}
			break;
#ifdef  KADB
		case '[':
			if (!executing) {
				executing = 1;
				db_printf(9, "command: set executing=1");
				subpcs('e');
				executing = 0;
				db_printf(9, "command: set executing=0");
				lastcom = 0;
			}
			break;
			 
		case ']':
			if (!executing) {
				executing = 1;
				db_printf(9, "command: set executing=1");
				subpcs('s');
				executing = 0;
				db_printf(9, "command: set executing=0");
				lastcom = 0;
			}
			break;
#endif	/* KADB */

		case 0:
#ifndef	KADB
			if (prompt == NULL)
				(void) printf("adb");
#endif	/* !KADB */
			break;

		default:
			error("bad command");
		}
		flushbuf();
	} while (rdc() == ';');
	if (buf)
		lp = savlp;
	else
		lp--;
	db_printf(5, "command: returns %D", (hadaddress && (dot != 0)));
	return (hadaddress && dot != 0);		/* for :b */
}

struct ecmd ecmd[];
static char ecmdbuf[100];


static
extended_command ()
{
	int i;

	i = extend_scan(NULL, ecmd);
	if (i >= 0) {
		(*ecmd[i].func)();
		return;
	}
	if (ext_ecmd(ecmdbuf) )
	        return;
	errflg = "extended command not found";
}

/*
 * This is used by both generic and arch specific extended cmd parsers.
 * buf:
 * If on input buf is null then scan will get chars from stdio else
 * buf points to the cmd that we will look for.
 * ecmd:
 * This points to an array of ecmd's to look in.
 *
 * On return we return the index into the ecmnd array of the found elemnt
 * If none was found return -1
 */
int
extend_scan(buf, ecmd)
char *buf;
struct ecmd *ecmd;
{
	int i, c;
	char *p;

	if (buf == NULL) 
	{
		p = ecmdbuf;

		/* rdc skips spaces, readchar doesn't */
		for (c = rdc (); c != '\n' && c != ' ' && c != '\t' ; c = readchar ())
		        *p++ = c;
		*p = 0;
		lp--;	/* don't swallow the newline here */

		p = ecmdbuf;
	} else
	        p = buf;
		
	for (i = 0; ecmd[i].name; i++) {
		if (ecmd[i].name && strcmp (p, ecmd[i].name) == 0) {
			return (i);
		}
 	}
	return (-1);
}


unsigned int max_nargs = 16;   /* max # of args to a function printable by $c */

static
ecmd_nargs(void)
{
	if (!hadaddress) {
		(void) printf("nargs = 0x%X\n", max_nargs);
		return 0;
	} else if (address < 0) {
		(void) printf("Invalid nargs = 0x%X\n", address);
		return -1;
	}
	max_nargs = address;
}

/*
 * Set level for debugging prints (1 = a little, 9 = a LOT).
 */
static
ecmd_dbprt(void)
{
	extern int adb_debug;

	if (!hadaddress) {	/* user just wants current level */
		(void) printf("adb_debug = 0x%X\n", adb_debug);
		return;
	} else if (address < 0) {
		(void) printf("Invalid debugging level = 0x%X\n", address);
		return;
	}
	adb_debug = address;
}

static
ecmd_help ()
{
	int i;
	struct ecmd *ecmdp;
	
	printf ("Commands:\n");
	printf ("addr,count?fmt           print instructions\n");
	printf ("addr,count/fmt           print data\n");
	printf ("addr,count$cmd           print debugging info based on cmd\n");
	printf ("addr,count:cmd           control debugging based on cmd\n");
#if	!defined(KADB)
	printf ("!shell-cmd               perform the cmd shell command\n");
#endif
	printf ("<num>::extcmd            call extended command\n");
	printf ("\nExtended Commands:\n");

	for (i = 0; ecmd[i].name; i++) 
		if (ecmd[i].help)
			printf ("%s\t%s", ecmd[i].name, ecmd[i].help);
	for (i = 0; ecmd[i].name; i++)
		if (ecmd[i].help == 0)
			printf ("%s ", ecmd[i].name);

	ecmdp = (struct ecmd *)ext_getstruct();
	for (i = 0; ecmdp[i].name; i++) 
		if (ecmdp[i].help)
			printf ("%s\t%s", ecmdp[i].name, ecmdp[i].help);
	for (i = 0; ecmdp[i].name; i++)
		if (ecmdp[i].name && ecmdp[i].help == 0)
			printf ("%s ", ecmdp[i].name);
	printf ("\n");
}



struct ecmd ecmd[] = {
{"nargs", ecmd_nargs, "set max # of args to functions in stack backtrace\n"},
{"dbprt", ecmd_dbprt, "set level for adb debugging printfs (1=some, 9=lots)\n"},
{"?", ecmd_help, "print this help display\n"},
{"help", ecmd_help, "print this help display\n"},
{0}
};
