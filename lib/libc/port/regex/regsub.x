/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)regsub.x 1.3	95/03/08 SMI"


/*
 * POSIX 1003.2 regex(3) package.
 * Perform substitutions.
 *
 * Copyright 1985, 1992 by Mortice Kern Systems Inc.  All rights reserved.
 *
 * This Software is unpublished, valuable, confidential property of
 * Mortice Kern Systems Inc.  Use is authorized only in accordance
 * with the terms and conditions of the source licence agreement
 * protecting this Software.  Any unauthorized use or disclosure of
 * this Software is strictly prohibited and will result in the
 * termination of the licence agreement.
 *
 * If you have any questions, please consult your supervisor.
 */

/*l
 * This is compiled into two functions:
 * regdosub if EXPAND is 0, and regdosuba if EXPAND is 1.
 * regdosub assumes a fixed-length destination string
 * is provided and returns REG_ESPACE if it overflows.
 * regdosuba allocates the destination string and expands
 * it as necessary.
 * Since this file is included from regdosub/regdosba, in both narrow
 * and wide modes, it will actually compile into 2 or 4 of 6 functions:
 * regsbdosub, regsbdosuba, regwdosub, regwdosuba
 * or, if compiled on a single byte system into regdosub and regdosuba.
 */
#ifdef EXPAND
int
regdosuba(rp, rpl, src, dstp, len, globp)
#else
/*f
 * reg(w|)dosub(a|) - substitute Nth or every occurence
 * of compiled pattern with replacement string
 *
 * Assuming we have a compiled RE (pat) in *rp,
 * do the "ed" substitution "s/pat/rpl/glob",
 * with input in "src" and output in "dst" (of length "len").
 * Return REG_OK or REG_NOMATCH accoringly.
 * Return REG_ESPACE if "len" is not large enough.
 */
int
regdosub(rp, rpl, src, dst, len, globp)
#endif
register regex_t *rp;		/* compiled RE: Pattern */
const CHARTYPE *rpl;		/* replacement string: /rpl/ */
const CHARTYPE *src;		/* source string */
#ifdef EXPAND
CHARTYPE **dstp;
#else
CHARTYPE *dst;			/* destination string */
#endif
int len;			/* destination length */
int *globp;		/* IN: occurence, 0 for all; OUT: substitutions */
{
#ifdef EXPAND
	CHARTYPE *dst, *odst;
#endif
	register const CHARTYPE *ip, *xp;
	register CHARTYPE *op;
	register int i;
	register CHARTYPE c;
	int glob, iglob = *globp, oglob = 0;
	REGMATCH rm[NSUB], *rmp;
	int flags;
	CHARTYPE *end;
	int regerr;

#ifdef EXPAND
/* handle overflow of dst. we need "i" more bytes */
#define	OVERFLOW(i) if (1) { \
		int pos = op - dst; \
		dst = realloc(odst = dst, (len += len + i) * sizeof(CHARTYPE)); \
		if (dst == NULL) \
			goto nospace; \
		op = dst + pos; \
		end = dst + len; \
	} else
#else
#define	OVERFLOW(i) \
		return REG_ESPACE
#endif

#ifdef EXPAND
	*dstp = dst = (CHARTYPE *)malloc(len * sizeof(CHARTYPE));
	if (dst == NULL)
		return REG_ESPACE;
#endif
	if (rp == NULL || rpl == NULL || src == NULL || dst == NULL)
		return REG_EFATAL;

	glob = 0;			/* match count */
	ip = src;			/* source position */
	op = dst;			/* destination position */
	end = dst + len;

	flags = 0;
	while ((regerr = REGEXEC(rp, ip, NSUB, rm, flags)) == REG_OK) {
		/* Copy text preceding match */
		if (op + (i = rm[0].rm_sp - ip) >= end)
			OVERFLOW(i);
		while (i--)
			*op++ = *ip++;

		if (iglob == 0 || ++glob == iglob) {
			oglob++;
			xp = rpl;		/* do substitute */
		} else
#ifdef	MB
			xp = L"&";		/* preserve text */
#else
			xp = "&";		/* preserve text */
#endif

		/* Perform replacement of matched substing */
		while ((c = *xp++) != '\0') {
			rmp = NULL;
			if (c == '&')
				rmp = &rm[0];
			else if (M_INVARIANT(c) == '\\') {
				if ('0'<=*xp && *xp<='9')
					rmp = &rm[*xp++ - '0'];
				else if (*xp != '\0')
					c = *xp++;
			}

			if (rmp == NULL) {	/* Ordinary character. */
				*op++ = c;
				if (op >= end)
					OVERFLOW(1);
			} else if (rmp->rm_sp != NULL && rmp->rm_ep != NULL) {
				ip = rmp->rm_sp;
				if (op + (i = rmp->rm_ep - rmp->rm_sp) >= end)
					OVERFLOW(i);
				while (i--)
					*op++ = *ip++;
			}
		}

		ip = rm[0].rm_ep;
		if (*ip == '\0')	      /*If at end break */
			break;
		else if (rm[0].rm_sp == rm[0].rm_ep) {
			/*If empty match copy next char */
			*op++ = *ip++;
			if (op >= end)
				OVERFLOW(1);
		}
		flags = REG_NOTBOL;
	}

	if (regerr != REG_OK && regerr != REG_NOMATCH)
		return regerr;

	/* Copy rest of text */
	if (op + (i = STRLEN(ip)) >= end)
		OVERFLOW(i);
	while (i--)
		*op++ = *ip++;
	*op++ = '\0';
#ifdef EXPAND
	if ((*dstp = dst = (CHARTYPE *)realloc(odst = dst,
	    sizeof(CHARTYPE) * (size_t)(op - dst))) == NULL) {
	  nospace:
		free(odst);
		return REG_ESPACE;
	}
#endif

	*globp = oglob;

	return ((oglob==0) ? REG_NOMATCH : REG_OK);
}
