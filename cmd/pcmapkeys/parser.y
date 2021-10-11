%{

/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993 Sun Microsystems, Inc.
 * All Rights Reserved. 
 */

#pragma ident "@(#)parser.y	1.5      94/01/23 SMI"

#include <stdio.h>
#include <libintl.h>
#include <sys/types.h>
#include <sys/emap.h>
#include <sys/kd.h>

int i;
int pc_i;
int counter;
int yydebug = 0;
unsigned char pc_cc; 		/* current 1st char in compose sequences */
unsigned char pc_mpstring[30];
unsigned char * pc_mpstrp;
unsigned char stab[256], *pc_stab;
short pc_ndind;      		/* number of single deadkeys */
struct emap pc_map;
struct emind *pc_dind;
struct emind sind[256], *pc_sind;
struct emind dctab[256] , *pc_dctab;
struct kbentry kd_val;
struct fkeyarg kd_keyval;

static void kd_skb(const unsigned char, const short, const int);
static int isfkey(const unsigned char, const int, const int);
static void pc_imap(unsigned char, const unsigned char);
static void pc_omap(unsigned char, const unsigned char);
static void pc_istring(int, const unsigned char *);
static void pc_strncpy (unsigned char *, const unsigned char *, int);
void warning(const char *, ...);

extern int kd_fd;
extern int yydebug;
extern unsigned char emap_buf[];

%}

%union {
	int intval;
	unsigned char u_char;
	struct {
		short value;
		unsigned char string[30];
	}  u_strct;
}

%token <u_char> SCAN
%token <u_char> FKEY
%token <u_char> BYTE
%token <u_strct> STRING
%token SHFT
%token CAPS
%token NUM
%token FLAG
%token NOP
%token ESCN
%token ESCO
%token ESCL
%token CONTROL
%token INPUT
%token SCANS
%token OUTPUT
%token COMMENTS
%token COMPOSE
%token DEAD
%token NLINE
%token TOGGLE

%type <u_strct> value
%type <u_strct> esc
%type <u_strct> flag
%type <u_strct> lock
%type <u_char>  scan
%type <u_char>  fkey

%start S

%%

S:	nls input toggle nonspace compose output scans
	{
		pc_strncpy(emap_buf + pc_map.e_map->e_dctab,
			   (unsigned char *) dctab,
			   pc_map.e_map->e_sind - pc_map.e_map->e_dctab);
		pc_strncpy(emap_buf + pc_map.e_map->e_sind,
			   (unsigned char *) sind,
		   	   pc_map.e_map->e_stab - pc_map.e_map->e_sind);
		pc_strncpy(emap_buf + pc_map.e_map->e_stab,
			   (unsigned char *) stab, pc_stab - stab);
	}
	;

nls:	nls NLINE
    |	NLINE
    |	/* no comment section */
	;

scans:	SCANS NLINE nls klines;
scans:	/*empty */
	;

input:	INPUT NLINE  icouples nls
	{
		pc_mpstrp = &pc_mpstring[1];
		pc_map.e_map = (emp_t) emap_buf;
		pc_map.e_ndind = 0;
		pc_map.e_ncind = 0;
		pc_map.e_nsind = 0;
		pc_dind = (emip_t) (pc_map.e_map->e_dind);
		pc_sind = (emip_t) sind;
		pc_dctab = (emip_t) dctab;
		pc_stab = stab;
		counter = 0;
	}
	;

input:	/* empty */
	{
		pc_mpstrp = &pc_mpstring[1];
		pc_map.e_map = (emp_t) emap_buf;
		pc_map.e_ndind = 0;
		pc_map.e_ncind = 0;
		pc_map.e_nsind = 0;
		pc_dind = (emip_t) (pc_map.e_map->e_dind);
		pc_sind = (emip_t) sind;
		pc_dctab = (emip_t) dctab;
		pc_stab = stab;
		counter = 0;
	}
	;

icouples:	icouples  icouple
	 |	icouple
	 |	/*empty input section */
	;

icouple:	BYTE BYTE nls
	{
		pc_imap($1,$2);
	}
	;

output:	outhead oseqs nls
	{
		pc_sind->e_ind = counter;
		pc_map.e_map->e_stab = pc_map.e_map->e_sind +
				    (pc_map.e_nsind + 1) * sizeof(struct emind);
	}
	;

outhead:	OUTPUT nls
	{
		counter = 0;
	}
	;

outhead:	/* no output section */ 
	{
		counter = 0;
	}
	;

oseqs:	oseqs oseq
      | oseq
      | /*empty output section */
	;

oseq:	BYTE BYTE nls  
	{
		pc_omap($1,$2);
	}
	;

oseq:	BYTE BYTE string nls
	{
		(pc_map.e_nsind)++;
		pc_omap($1,0);
		*pc_mpstrp = 0;
		pc_sind->e_key = (unsigned char) $1;
		pc_sind->e_ind = counter;
		pc_sind++;
		pc_mpstring[0] = $2;
		pc_i = strlen((char *)pc_mpstring);
		counter += pc_i;
		pc_mpstrp = &pc_mpstring[1];
		pc_istring(pc_i, pc_mpstring);
	}
	;

string:	string BYTE
	{
		*pc_mpstrp++ = $2;
		i++;
	}
	;

string:	BYTE
	{
		*pc_mpstrp++ = $1;
		i++;
	}
	;

nonspace:	dead  nls
	{
		/*
		 * Counter is not cleared, we will use it for compose seqs
		 */
		pc_map.e_map->e_cind = (pc_map.e_ndind +
			(emip_t) pc_map.e_map->e_dind - (emip_t) pc_map.e_map) *
			  	      sizeof(struct emind);
	}
	;

dead:	deadhead deadbody
dead:	dead deadhead deadbody;
dead:	;

deadhead:	DEAD BYTE NLINE 
	{
		pc_imap($2,0);
		pc_dind->e_key = (unsigned char) $2;
		(pc_map.e_ndind)++;
		pc_dind->e_ind = counter;
		pc_dind++;
		/*
		 * Offset is total number of PREVIOUS dead seqs
		 */
	}
	;

deadbody:	deadbody BYTE BYTE nls
	{
		counter++;
		pc_dctab->e_key = $2;
		pc_dctab->e_ind = $3;
		pc_dctab++;
	}
	;

deadbody:	BYTE BYTE nls
	{
		counter++;
		pc_dctab->e_key = $1;
		pc_dctab->e_ind = $2;
		pc_dctab++;
	}
	;

comphead:	COMPOSE BYTE nls
	{
		pc_map.e_map->e_comp = $2;		 /* compose character */
		pc_imap($2,0);
		pc_cc = 0;
	}
	;

compose:	comphead cseqs nls
	{
		/*
		 * All compose sequences seen, this offset is basically
		 * for the next but there is no next
		 */
		pc_dind->e_key = 0;
		pc_dind->e_ind = counter;
		pc_map.e_ncind++;
		pc_map.e_map->e_dctab = ((emip_t) pc_map.e_map->e_dind -
		      (emip_t) pc_map.e_map + pc_map.e_ndind + pc_map.e_ncind) *
				       sizeof (struct emind);
		pc_map.e_map->e_sind = ((emip_t) pc_map.e_map->e_dind -
					(emip_t) pc_map.e_map +
					(emip_t)pc_dctab -
				        (emip_t) dctab + pc_map.e_ndind +
				        pc_map.e_ncind) *
				       sizeof (struct emind);
	}
	;

compose:	comphead nls
	{
		/*
		 * After deadkeys and compose we should know the beginning
		 * of string indexes.  All compose sequences seen, this
		 * offset is basically for the next but there is no next
		 */
		pc_dind->e_key = 0;
		pc_dind->e_ind = counter;
		pc_map.e_ncind++;
		pc_map.e_map->e_dctab = ((emip_t)pc_map.e_map->e_dind -
					(emip_t)pc_map.e_map + pc_map.e_ndind +
					pc_map.e_ncind) *
				       sizeof (struct emind);
		pc_map.e_map->e_sind = ((emip_t)pc_map.e_map->e_dind -
				       (emip_t)pc_map.e_map + (emip_t)pc_dctab -
				       (emip_t) dctab + pc_map.e_ndind +
				       pc_map.e_ncind) *
				      sizeof (struct emind);
	}
	;

compose:	/* no compose section , need to fix */;
cseqs:	cseqs cseq
	;
cseqs:	cseq
	;
cseq:	BYTE BYTE BYTE nls   
	{
		if ($1 != pc_cc) {
			pc_dind->e_key = $1;
			pc_dind->e_ind = counter;
			pc_map.e_ncind++;
			pc_dind++;
			/*offset of all compose seqs for $1 stored */
			pc_cc = $1;
		}
		pc_dctab->e_key = $2;
		pc_dctab->e_ind = $3;
		pc_dctab++;
		counter++;
	}
	;

toggle:	TOGGLE BYTE nls
	{
		pc_map.e_map->e_toggle = $2; 		/* toggle character*/
		pc_imap($2,0);
	}
	;

toggle:	/*empty*/

klines:	 klines kline
       | kline
	;

kline:	fkey STRING nls
	{
		kd_keyval.keynum = $1;
		kd_keyval.flen = $2.value;
		(void) strcpy((char *)kd_keyval.keydef , (char *)$2.string);
		if (ioctl(kd_fd, SETFKEY , &kd_keyval) == -1)
			warning(gettext("failed to set function key %d to %s"), 
				kd_keyval.keynum, kd_keyval.keydef);
	}
	;

kline:	fkey string nls 
	{
		kd_keyval.keynum = $1;
		kd_keyval.flen = i;
		(void) strcpy((char *)kd_keyval.keydef , (char *)pc_mpstring);
		if (ioctl(kd_fd, SETFKEY , &kd_keyval) == -1)
			warning(gettext("failed to set function key %d to %s"), 
				kd_keyval.keynum, kd_keyval.keydef);
	}
	;

kline:	scan value value value value lock nls
	{
		if (i= isfkey($1,0,$2.value))
			warning(gettext("use F%d for 0x%x and the "
					"normal table\n"), i, $1);
		else
			kd_skb($1 , $2.value|$6.value, 0);

		if (i= isfkey($1,1,$3.value))
			warning(gettext("use F%d for 0x%x and "
					"the SHIFT table\n"), i, $1);
		else
			kd_skb($1, $3.value|$6.value, 1);
		kd_skb($1, $4.value|$6.value, 2);
		kd_skb($1, $5.value|$6.value, 3);
	}
	;

lock:	lock CAPS
	{
		$$.value = $1.value | 0x4000;
	}
	;

lock:	lock NUM 
	{
		$$.value = $1.value | 0x8000;
	}
	;

lock:	/*empty */
	{
		$$.value = 0;
	}
	;

fkey:	FKEY
	{
		i = 0;
		pc_mpstrp=pc_mpstring;
	}
	;

value:	BYTE
	{
		$$.value = $1;
	}
	;

value:	BYTE flag
	{
		$$.value = $1| $2.value;
	}
	;

value:	NOP 
	{
		$$.value = 0;
	}
	;

scan:	BYTE;

flag:	flag esc
	{
		$$.value = $1.value | $2.value;
	}
	;

flag:	flag CONTROL
	{
		$$.value = $1.value | 0x2000;
	}
	;

flag:	/*empty */
	{
		$$.value = 0;
	}
	;

esc:	ESCN
	{
		$$.value = 0x0300;
	}
	;

esc:	ESCO
	{
		$$.value = 0x0400;
	}
	;

esc:	ESCL
	{
		$$.value = 0x0500;
	}
	;

%%

static void
kd_skb(const unsigned char x, const short y, const int z)
{
	if (y) {
		kd_val.kb_index = x;
		kd_val.kb_table = z;
		kd_val.kb_value = y;
		if (ioctl(kd_fd, KDSKBENT, &kd_val) == -1) {
			char *table = "?";

			switch(z) {
				/*
				 * Should not do gettext() on the values
				 * for table, as they are used the same
				 * way as they are used in the map files.
				 */
			case K_NORMTAB:
				table = "NORM";
				break;
			case K_SHIFTTAB:
				table = "SHIFT";
				break;
			case K_ALTTAB:
				table = "ALT";
				break;
			case K_ALTSHIFTTAB:
				table = "ALT_SHIFT";
				break;
			case K_SRQTAB:
				table = "SYSREQ";
				break;
			default:
				warning(gettext("invalid keyboard table"));
				break;
			}
			warning(gettext("failed to set scancode %#x to value "
					"%#x in %s table"), x, y, table);
		}
				
	}
}

/*
 * Determine if a scancode is also a functionkey.
 * XXX - this is a quick and dirty piece of code but it will do ...
 */

static int
isfkey(const unsigned char a, const int b, const int c)
{
	if (c == 0)
		return 0;
	if (b == 0) {
		if (0x3b <= a && 0x44 >= a)
			return(a - 0x3b + 1);
		if (0x47 <= a && 0x52 >= a)
			return a - 0x47 + 49;
		if (a == 0x58)
			return 12;
		if (a == 0x57)
			return 11;
	}
	if (b == 1) {
		if (0x3b <= a && 0x44 >= a)
			return a - 0x3b + 13;
		if (a == 0x58)
			return 24;
		if (a == 0x57)
			return 23;
	}
	return 0;
}

static void
pc_imap(register unsigned char p, const unsigned char a)
{
	if (emap_buf[p] !=  p) {
		warning(gettext("character %d has already been mapped to %d"),
		        p , emap_buf[p] );
		return;
	}
	emap_buf[p] = a;
}

static void
pc_omap(register unsigned char u, const unsigned char l)
{
	if (emap_buf[u + 256] !=  u) {
		warning(gettext("character %d has already been mapped to %d"),
		        u , emap_buf[u + 256] );
		return;
	}
	emap_buf[u + 256] = l;
}

static void
pc_istring(int w, const unsigned char *e)
{
	int i = w;

	while (w--)
		*pc_stab++ = *e++;
}

/*
 * Need to have our own strncpy(), because embedded zeroes should be
 * copied as well instead of interpreting them as the end of string.
 */
static void
pc_strncpy (unsigned char *l , const unsigned char *le, int n)
{
	while (n--)
		*l++ = *le++;
}
