/*
 * Copyright (c) 1995 by Sun Microsystems, Inc
 *
 * xcompile, xstep, xadvance - simulate compile(3g), step(3g), advance(3g)
 *	using regcomp(3c), regexec(3c) interfaces. This is an XCU4
 *	porting aid. switches out to libgen compile/step if collation
 *	table not present.
 *
 *	Goal is to work with vi and sed/ed.
 * 	Returns expbuf in dhl format (encoding of first two bytes).
 * 	Note also that this is profoundly single threaded.  You
 *	cannot call compile twice with two separate search strings
 *	because the second call will wipe out the earlier stored string.
 *	This must be fixed, plus a general cleanup should be performed
 *	if this is to be integrated into libc.
 *
 */

#ident  "@(#)compile.c 1.11     95/09/01 SMI"

#include <stdio.h>
#include <regex.h>
#include <locale.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <regexpr.h>

/*
 * psuedo compile/step/advance global variables
 */
extern int nbra;
extern char *locs; 		/* for stopping execess recursion */
extern char *loc1;  		/* 1st character which matched RE */
extern char *loc2; 		/* char after lst char in matched RE */
extern char *braslist[]; 	/* start of nbra subexp  */
extern char *braelist[]; 	/* end of nbra subexp    */
extern int regerrno;
extern int reglength;

char * _compile(const char *instr, char *ep, char *endbuf);
int _step(const char *str, const char *ep);
int _radvance(const char *str, const char *ep);
int _is_xpg_collate();		/* Undocumented libc global */
void regex_comp_free(void *a);
static int dhl_step(const char *str, const char *ep);
static int dhl_advance(const char *str, const char *ep);
static int use_new_stuff(int);
static int map_errnos(int);		/* Convert regcomp error */
static int dhl_doit(const char *, const regex_t *, const int flags);
static char * dhl_compile(const char *instr, char *ep, char *endbuf);
char errbuf[512];		/* We put errors from regerror here */

/*
 * # of sub re's: NOTE: For now limit on bra list defined here
 * but fix is to add maxbra define to to regex.h
 * One problem is that a bigger number is a performance hit since
 * regexec() has a slow initialization loop that goes around SEPSIZE times
 */
#define	SEPSIZE 20
static regmatch_t rm[SEPSIZE];		/* ptr to list of RE matches */

/*
 * Structure to contain dl encoded first two bytes for vi, plus hold two
 * regex structures, one for advance and one for step.
 */
static struct regex_comp {
	char 	r_head[2];		/* Header for DL encoding for vi */
	regex_t r_stp;			/* For use by step */
	regex_t r_adv;			/* For use by advance */
} reg_comp;

/*
 * global value for the size of a regex_comp structure:
 */
size_t regexc_size = sizeof (reg_comp);

/*
 * compile, step, advance - designed to be linked in ahead of libgen
 * 	and replaces compile/step/advance in libgen.  Internally calls
 *	dhl_compile if use_new_stuff returns true, otherwise calls
 *	libgen routines.
 */

char *
compile(const char *instr, char *expbuf, char *endbuf)
{
	if (use_new_stuff(1) == 1)
		return (dhl_compile(instr, expbuf, endbuf));
	else
		return (_compile(instr, expbuf, endbuf));
}

int
step(const char *instr, const char *expbuf)
{
	if (use_new_stuff(0) == 1)
		return (dhl_step(instr, expbuf));
	else
		return (_step(instr, expbuf));
}

int
advance(const char *instr, const char *expbuf)
{
	if (use_new_stuff(0) == 1)
		return (dhl_advance(instr, expbuf));
	else
		return (_radvance(instr, expbuf));
}

/*
 * Use_new_stuff returns true if C locale, en_us locale or is_xpg_collate
 * 	returns true.  is_xpg_collate returns true if the CollTable table
 *	is present.  This could be undesirable until all the bugs are out
 *	of libcollate and the multibyte regex code.
 *	Returns 0 for use the old compile/step. 
 *	Return 1 to use new regcomp/regexec.
 *
 */
static int
use_new_stuff(check_locale)
	int check_locale;
{
	static int use_new = 0;
	char * locale;

	/*
	 * If the parameter check_locale is set call setlocale, otherwise
	 * use the cached value use_new.
	 */
	if (check_locale == 1) {
		if ((locale = setlocale(LC_COLLATE, 0)) == NULL)
			return (0);
		/*
		 * Returns true if in C or US locale or if we have an xpg
		 * collation table.  Turn that off if multibyte regex
		 * still broken when new xpg4 coll tables are available.
		 */
		if ((strcmp(locale, "C") == 0) ||
		    (strcmp(locale, "en_US") == 0) ||
		    (strcmp(locale, "POSIX") == 0) ||
		    (_is_xpg_collate())) {
			use_new = 1;	/* Cache the result */
			return (1);
		} else {
			use_new = 0;
			return (0);
		}
	} else
		return (use_new);	/* Skip checks, return cached value */
}

/*
 * the compile and step routines here simulate the old libgen routines of
 * compile/step Re: regexpr(3G). in order to do this, we must assume
 * that expbuf[] consists of the following format:
 *	1) the first two bytes consist of a special encoding - see below.
 *	2) the next part is a regex_t used by regexec()/regcomp() for step
 *	3) the final part is a regex_t used by regexec()/regcomp() for advance
 *
 * the special encoding of the first two bytes is referenced throughout
 * vi. apparently expbuf[0] is set to:
 *	= 0 upon initialization
 *	= 1 if the first char of the RE is a ^
 *	= 0 if the first char of the RE isn't a ^
 * and expbuf[1-35+]	= bitmap of the type of RE chars in the expression.
 * this is apparently 0 if there's no RE.
 * Here, we use expbuf[0] in a similar fashion; and expbuf[1] is non-zero
 * if there's at least 1 RE in the string.
 * I say "apparently" as the code to compile()/step() is poorly written.
 */
static char *
dhl_compile(instr, expbuf, endbuf)
const char *instr;		/* the regular expression		*/
char *expbuf;			/* where the compiled RE gets placed	*/
char *endbuf;			/* ending addr of expbuf		*/
{
	int rv;
	int alloc = 0;
	char adv_instr[4096];	/* PLENTY big temp buffer */
	char *instrp;		/* PLENTY big temp buffer */

	if (*instr == (char) NULL) {
		regerrno = 41;
		return (NULL);
	}

	/*
	 * Check values of expbuf and endbuf
	 */
	if (expbuf == NULL) {
		if ((expbuf = malloc(regexc_size)) == NULL) {
			regerrno = 50;
			return (NULL);
		}
		memset(&reg_comp, 0, regexc_size);
		alloc = 1;
		endbuf = expbuf + regexc_size;
	} else {		/* Check if enough memory was allocated */
		if (expbuf + regexc_size > endbuf) {
			regerrno = 50;
			return (NULL);
		}
		memcpy(&reg_comp, expbuf, regexc_size);
	}

	/*
	 * Clear global flags
	 */
	nbra = 0;
	regerrno = 0;

	/*
	 * Free any data being held for previous search strings
	 */
	regex_comp_free(&reg_comp);

	/*
	 * We call regcomp twice, once to get a regex_t for use by step()
	 * and then again with for use by advance()
	 */
	if ((rv = regcomp(&reg_comp.r_stp, instr, 0)) != 0) {
		regerror(rv, &reg_comp.r_stp, errbuf, sizeof (errbuf));
		regerrno = map_errnos(rv);	/* Convert regcomp error */
		goto out;
	}
	/*
	 * To support advance, which assumes an implicit ^ to match at start
	 * of line we prepend a ^ to the pattern by copying to a temp buffer
	 */

	if (instr[0] == '^')
		instrp = (char *) instr; /* String already has leading ^ */
	else {
		adv_instr[0] = '^';
		strncpy(&adv_instr[1], instr, 2048);
		instrp = adv_instr;
	}

	if ((rv = regcomp(&reg_comp.r_adv, instrp, 0)) != 0) {
		regerror(rv, &reg_comp.r_adv, errbuf, sizeof (errbuf));
		regerrno = map_errnos(rv);	/* Convert regcomp error */
		goto out;
	}

	/*
	 * update global variables
	 */
	nbra = (int) reg_comp.r_adv.re_nsub > 0 ?
	    (int) reg_comp.r_adv.re_nsub : 0;
	regerrno = 0;

	/*
	 * Set the header flags for use by vi
	 */
	if (instr[0] == '^') 		/* if beginning of string,	*/
		reg_comp.r_head[0] = 1;	/* set special flag		*/
	else
		reg_comp.r_head[0] = 0;	/* clear special flag		*/
	/*
	 * note that for a single BRE, nbra will be 0 here.
	 * we're guaranteed that, at this point, a RE has been found.
	 */
	reg_comp.r_head[1] = 1;	/* set special flag		*/
	/*
	 * Copy our reg_comp structure to expbuf
	 */
	(void) memcpy(expbuf, (char *) &reg_comp, regexc_size);

out:
	/*
	 * Return code from libgen regcomp with mods.  Note weird return
	 * value - if space is malloc'd return pointer to start of space,
	 * if user provided his own space, return pointer to 1+last byte
	 * of his space.
	 */
	if (regerrno != 0) {
		if (alloc)
			free(expbuf);
		return (NULL);
	}
	reglength = regexc_size;

	if (alloc)
		return (expbuf);
	else
		return (expbuf + regexc_size);
}


/*
 * dhl_step: step through a string until a RE match is found, or end of str
 */
static int
dhl_step(str, ep)
const char *str;		/* characters to be checked for a match	*/
const char *ep;			/* compiled RE from dhl_compile()	*/
{
	/*
	 * Check if we're passed a null ep
	 */
	if (ep == NULL) {
		regerrno = 41;	/* No remembered search string error */
		return (0);
	}
	/*
	 * Call common routine with r_stp (step) structure
	 */
	return (dhl_doit(str, &(((struct regex_comp *) ep)->r_stp),
	    ((locs != NULL) ? REG_NOTBOL : 0)));
}

/*
 * dhl_advance: implement advance
 */
static int
dhl_advance(str, ep)
const char *str;		/* characters to be checked for a match	*/
const char *ep;			/* compiled RE from dhl_compile()	*/
{
	int rv;
	/*
	 * Check if we're passed a null ep
	 */
	if (ep == NULL) {
		regerrno = 41;	/* No remembered search string error */
		return (0);
	}
	/*
	 * Call common routine with r_adv (advance) structure
	 */
	rv = dhl_doit(str, &(((struct regex_comp *) ep)->r_adv), 0);
	loc1 = NULL;		/* Clear it per the compile man page */
	return (rv);
}

/*
 * dhl_doit - common code for step and advance
 */
static int
dhl_doit(str, rep, flags)
const char *str;		/* characters to be checked for a match	*/
const regex_t *rep;
const int flags;		/* flags to be passed to regexec directly */
{
	int rv;
	int i;
	regmatch_t *prm;	/* ptr to current regmatch_t		*/

	/*
	 * Check if we're passed a null regex_t
	 */
	if ((rep == NULL) || (rep->re_node == NULL)) {
		regerrno = 0;
		return (41);	/* No remembered search string error */
	}

	regerrno = 0;
	prm = &rm[0];

	if ((rv = regexec(rep, str, SEPSIZE, prm, flags)) != REG_OK) {
		regerror(rv, rep, errbuf, sizeof (errbuf));
		if (rv == REG_NOMATCH)
			return (0);
		regerrno = map_errnos(rv);
		return (0);
	}

	loc1 = (char *) prm->rm_sp;
	loc2 = (char *) prm->rm_ep;

	/*
	 * Now we need to fill up the bra lists with all of the sub re's
	 * Note we subtract nsub -1, and preincrement prm.
	 */
	for (i = 0; i <= rep->re_nsub; i++) {
		prm++;		/* XXX inc past first subexp */
		braslist[i] = (char *) prm->rm_sp;
		braelist[i] = (char *) prm->rm_ep;
		if (i >= SEPSIZE) {
			regerrno = 50; 	/* regex overflow */
			return (0);
		}
	}

	/*
	 * Inverse logic, a zero from regexec - success, is a 1
	 * from advance/step.
	 */

	return (rv == 0);
}


/*
 *	regerrno to compile/step error mapping:
 *	This is really a big compromise.  Some errors don't map at all
 *	like regcomp error 15 is generated by both compile() error types
 *  	44 & 46.  So which one should we map to?
 *	Note REG_ESUB Can't happen- 9 is no longer max num of subexpressions
 *	To do your errors right use xregerr() to get the regcomp error
 *	string and print that.
 *
 * |	regcomp/regexec		     | 	Compile/step/advance		    |
 * +---------------------------------+--------------------------------------+
 * 0 REG_OK	  Pattern matched	1  - Pattern matched
 * 1 REG_NOMATCH  No match		0  - Pattern didn't match
 * 2 REG_ECOLLATE Bad collation elmnt.	67 - Returned by compile on mbtowc err
 * 3 REG_EESCAPE  trailing \ in patrn	45 - } expected after \.
 * 4 REG_ENEWLINE \n before end pattrn	36 - Illegal or missing delimiter.
 * 5 REG_ENSUB	  Over 9 \( \) pairs 	43 - Too many \(
 * 6 REG_ESUBREG  Bad number in \[0-9]  25 - ``\digit'' out of range.
 * 7 REG_EBRACK   [ ] inbalance		49 - [ ] imbalance.
 * 8 REG_EPAREN   ( ) inbalance         42 - \(~\) imbalance.
 * 9 REG_EBRACE   \{ \} inbalance       45 - } expected after \.
 * 10 REG_ERANGE  bad range endpoint	11 - Range endpoint too large.
 * 11 REG_ESPACE  no memory for pattern 50 - Regular expression overflow.
 * 12 REG_BADRPT  invalid repetition	36 - Illegal or missing delimiter.
 * 13 REG_ECTYPE  invalid char-class    67 - illegal byte sequence
 * 14 REG_BADPAT  syntax error		50 - Regular expression overflow.
 * 15 REG_BADBR   \{ \} contents bad	46 - First number exceeds 2nd in \{~\}
 * 16 REG_EFATAL  internal error	50 - Regular expression overflow.
 * 17 REG_ECHAR   bad mulitbyte char	67 - illegal byte sequence
 * 18 REG_STACK   stack overflow	50 - Regular expression overflow.
 * 19 REG_ENOSYS  function not supported 50- Regular expression overflow.
 *
 *	For reference here's the compile/step errno's. We don't generate
 *	41 here - it's done earlier, nor 44 since we can't tell if from 46.
 *
 *	11 - Range endpoint too large.
 *	16 - Bad number.
 *	25 - ``\digit'' out of range.
 *	36 - Illegal or missing delimiter.
 *	41 - No remembered search string.
 *	42 - \(~\) imbalance.
 *	43 - Too many \(.
 *	44 - More than 2 numbers given in "\{~\}"
 *	45 - } expected after \.
 *	46 - First number exceeds 2nd in "\{~\}"
 *	49 - [ ] imbalance.
 *	50 - Regular expression overflow.
 */

static int
map_errnos(int errno)
{
	switch (errno) {
	case REG_ECOLLATE:
		regerrno = 67;
		break;
	case REG_EESCAPE:
		regerrno = 45;
		break;
	case REG_ENEWLINE:
		regerrno = 36;
		break;
	case REG_ENSUB:
		regerrno = 43;
		break;
	case REG_ESUBREG:
		regerrno = 25;
		break;
	case REG_EBRACK:
		regerrno = 49;
		break;
	case REG_EPAREN:
		regerrno = 42;
		break;
	case REG_EBRACE:
		regerrno = 45;
		break;
	case REG_ERANGE:
		regerrno = 11;
		break;
	case REG_ESPACE:
		regerrno = 50;
		break;
	case REG_BADRPT:
		regerrno = 36;
		break;
	case REG_ECTYPE:
		regerrno = 67;
		break;
	case REG_BADPAT:
		regerrno = 50;
		break;
	case REG_BADBR:
		regerrno = 46;
		break;
	case REG_EFATAL:
		regerrno = 50;
		break;
	case REG_ECHAR:
		regerrno = 67;
		break;
	case REG_STACK:
		regerrno = 50;
		break;
	case REG_ENOSYS:
		regerrno = 50;
		break;
	default:
		regerrno = 50;
		break;
	}
	return (regerrno);
}

/*
 *  This is a routine to clean up the subtle substructure of the struct
 *  regex_comp type for use by clients of this module.  Since the struct
 *  type is private, we use a generic interface, and trust the
 *  application to be damn sure that this operation is valid for the
 *  named memory.
 */

void
regex_comp_free(void * a)
{
	/*
	 * Free any data being held for previous search strings
	 */

	if (((struct regex_comp *) a) == NULL) {
		return;
	}

	if (((struct regex_comp *) a)->r_stp.re_node != NULL) {
		regfree(&((struct regex_comp *) a)->r_stp);
		((struct regex_comp *) a)->r_stp.re_node = NULL;
	}
	if (((struct regex_comp *) a)->r_adv.re_node != NULL) {
		regfree(&((struct regex_comp *) a)->r_adv);
		((struct regex_comp *) a)->r_adv.re_node = NULL;
	}
}
