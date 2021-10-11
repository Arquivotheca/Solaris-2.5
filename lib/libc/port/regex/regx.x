/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)regx.x 1.7	95/05/18 SMI"

/*
 * regsbexec, regwexec: This file is #included by regexec.c with different
 * #defines!
 *
 * Copyright (c) 1986 by University of Toronto.
 *	Written by Henry Spencer.  Not derived from licensed software.
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
 * 
 *	Modifications by Eric Gisin and Alex White.
 *
 */
#ifdef M_RCSID
#ifndef lint
static char rcsID[] = "$Header: /u/rd/src/libc/regex/rcs/regx.x 1.21 1995/02/21 21:23:47 ant Exp $";
#endif
#endif /*lint*/


/*f
 * regexec - match compiled RE against string
 *
 * Basically scan along both the string and the NFA (via the next pointer).
 * Fail if an NFA node doesn't match the string;
 * succeed if we reach the end of the NFA (the RETURN 1 node).
 * We could handle branches like this:
 *	case BRANCH:
 *		return regmatch(r, np->u.np, sub, cp, flags) ||
 *		       regmatch(r, np->next, sub, cp, flags);
 * Instead we avoid recursion and use a backtracking stack.
 * The second alternative (np->next) is pushed and we continue with
 * the first alternative (np->u.np). On failure we pop the stack.
 * We also use the stack to save subexpression positions.
 */
static int
regexec(r, astring, nsub, sub, flags)
const regex_t *r;			/* compiled RE */
const CHARTYPE *astring;		/* subject string */
size_t nsub;				/* number of subexpressions */
REGMATCH *sub;				/* subexpression pointers */
int flags;
{
	typedef union stack {		/* back-track stack element */
	    struct {
		node *np;
		const UCHARTYPE *cp;
	    } bt;			/* pushed node and position */
	    struct {
		node *np;
		node *ref;
	    } xr;			/* x-ref'ing a node; REPEATPOP */
	    struct {
		const CHARTYPE *sp, *ep;
	    } rm;			/* pushed subexpression */
	} stack;

	typedef struct {
		int ss;
		node *id;
	} minmaxstack;

	const UCHARTYPE *string = (UCHARTYPE *)astring;
	const UCHARTYPE *cp = string;	/* current string position */
	node *np = r->re_node;		/* current NFA node */
	node repeatpop;			/* for popping correct {,} count */
	node repeatpop2;		/* for popping correct cp ptr for {,} */
	stack bstack[REG_BTSIZE*2];	/* back-track stack */
	stack *bp;			/* top of back-track stack */
	minmaxstack mmstack[REG_BTSIZE];/* minmax checking stack */
	int mmp = -1;			/* ptr to top of minmax stack */
	node mmsize;			/* for bktrk'g mmstack */
	REGMATCH *subs;			/* subexpr list being used */
	REGMATCH dsubs[10];		/* default subexprs for \[0-9] */
	REGMATCH *esubs;		/* &subs[nsubs] */
	REGMATCH *subp;			/* working pointer into subs array */
	size_t nsubs;			/* sizeof subs array */
	int i;
	int length = -1;		/* Longest current match */

	/* A few sanity checks... valid string pointer? */
	if (string == NULL)
		return REG_EFATAL;

	/* valid subexpression pointer? */
	if (nsub != 0 && (r->re_flags&REG_NOSUB) == 0 && sub == NULL)
		return REG_EFATAL;

	/*
	 * If the RE is expensive and there is a known substring
	 * (regmust), check for it before calling regmatch.
	 * We only check for a four character match (?).
	 */
	if ((flags&(REG_NOOPT|REG_NOTBOL)) == 0 &&
	    r->re_flags&REG_MUST && r->re_regmust != NULL) {
		register wuchar_t *m = (wuchar_t*)r->re_regmust; /* NOT chartype */
		register const UCHARTYPE *cur = string;

		for (; (cur = STRCHR(cur, m[0])) != NULL; cur++)
			if (m[1] == 0 || m[1] == cur[1] &&
			   (m[2] == 0 || m[2] == cur[2] &&
			   (m[3] == 0 || m[3] == cur[3])))
				break;
		if (cur == NULL)
			return REG_NOMATCH;
	}

	/*
	 * We can't always use the user's `sub' array because he may request
	 * to be notified about fewer subexpressions than used in their
	 * own regular expression, i.e. (a)(b)(c)\3, is valid, and should work,
	 * even on a call to regexec with nsub of zero, or two.
	 * However, we can't just always use our dsubs array, because the
	 * user can match more than 10 subexpressions.  So we have to use
	 * their array for >10, and our array otherwise.
	 *
	 * Unfortunately, because we may backtrack to find the longest match,
	 * things get more complex still -- we need two copies of all this.
	 * We do a very kludgey thing: since we maintain in the regmatch_t
	 * array both an obsolete string pointer, and the new string index:
	 * we effectively have can have two copies, since the string index
	 * is normally only computed at exit time.  Thus, when we want to
	 * store the `longest' match yet found rooted at this point, we save
	 * the substrings in the string index locations, and if it later
	 * turns out that this was the longest, we restore the obsolete
	 * pointers back.  When the string indexes are then recomputed at
	 * exit point, all trace of this is lost.  By the way, the whole reason
	 * for doing this is to avoid allocating dynamic memory here in a fast
	 * critical piece of code, even though that would be much cleaner.
	 */
	if ((r->re_flags & REG_NOSUB) || nsub < 10)
		subs = dsubs, nsubs = 10;
	else
		subs = sub, nsubs = nsub;
	esubs = &subs[nsubs];

	/* clear subexpression positions */
	for (subp = subs ; subp < esubs; subp++) {
		subp->rm_sp = subp->rm_ep = NULL;
		subp->rm_so = subp->rm_eo = -1;
	}

	/*l set up the repeatpop node */
	repeatpop.type = REPEATPOP;
	repeatpop.u.i = 0;	/*l never use it, but initialize to be nice */
	repeatpop.next = NULL;	/*l yes, NULL; not going anywhere */
	repeatpop2.type = REPEATPOP2;
	repeatpop2.u.i = 0;	/*l never use it, but initialize to be nice */
	repeatpop2.next = NULL;	/*l yes, NULL; not going anywhere */

	/*l set up repeat backtracking node */
	mmsize.type = MINMAXSIZE;
	mmsize.u.i = 0;		/*l using mmp instead */
	mmsize.next = NULL;	/*l yes, NULL; not going anywhere */

	/* push a RETURN 0 node as a last resort */
	bp = bstack;
	bp->bt.cp = cp;
	bp++->bt.np = &return0;

	/*
	 * Loop running the NFA until we hit either FAIL or RETURN.
	 * In the case of fail, there is always something on the backtrack
	 * stack; we pop it and continue.  Eventually we'll either hit
	 * a RETURN 1 in the NFA, or the RETURN 0 we just pushed -- in
	 * that case we've tried all branches, and completely bombed out.
	 */
	while (1) {
		register m_collel_t c;

		FETCH(c, cp);
#ifdef DEBUG
		if (np == NULL)
			return REG_EFATAL;
		if (flags&REG_DEBUG) {
			if (sizeof(UCHARTYPE) == 1)
				printf("`%s' %2d ", cp, (int)(cp-string));
			else 
				printf("`%S' %2d ", cp, (int)(cp-string));
			regprint(np);
		}
#endif
		switch (np->type) {
		case RETURN:
#ifdef DEBUG
			if (np->u.i == 0 && bp != bstack) {
				if (flags & REG_DEBUG)
					printf("bad return 0\n");
				return REG_EFATAL;
			}
#endif
			/*
			 * Hitting RETURN 0 implies a failure to match.
			 * Either we had an earlier, shorter match,
			 * or we can just immediately return a failure.
			 */
			if (np->u.i == 0 && length == -1)
				return REG_NOMATCH;

			/*
			 * We had a success, and caller is only interested
			 * in success/fail, so we don't have to try for the
			 * longest match.
			 */
			if ((r->re_flags & REG_NOSUB) || nsub == 0)
				return REG_OK;

			/*
			 * If we succeeded, but have any other alternative
			 * branches on the stack, then we have to remember
			 * this (if longer than any prior success), and
			 * backtrack.  Do not back up to an initial scan,
			 * since we want longest, but also first.
			 * If matched up to end-of-string, then obviously we
			 * can't match longer, so accept the match even with
			 * possible backtracks.
			 */
			/*if (bp != bstack && *cp != '\0') {*/
			if (bp != bstack) {
#ifdef DEBUG
				if (flags & REG_DEBUG)
					printf("RETURN %d, length %d, ",np->u.i,
						(int)(cp-string));
#endif
				/* Match completed, other possibilities */
				if (np->u.i && (int)(cp-string) > length) {
					length = (int)(cp-string);
					/* save subs[] */
				for (subp = subs ; subp < esubs; subp++)
					if (subp->rm_ep == NULL)
						subp->rm_so = subp->rm_eo = -1;
					else
					subp->rm_so = subp->rm_sp - astring,
					subp->rm_eo = subp->rm_ep - astring;
#ifdef DEBUG
					if (flags & REG_DEBUG)
						printf("BEST, ");
#endif
				}
				goto fail;
			}

			/*
			 * Finished all possibilities.
			 * If no match this time, or this match not the longest,
			 * restore earlier match
			 */
			if (np->u.i == 0 || length > (int)(cp-string)) {
		restoresubs:
				/* restore subs[] */
				for (subp = subs; subp < esubs; subp++)
					if (subp->rm_so == -1)
						subp->rm_ep = subp->rm_sp = NULL;
					else
						subp->rm_sp = astring + subp->rm_so,
						subp->rm_ep = astring + subp->rm_eo;
					
#ifdef DEBUG
				if (flags & REG_DEBUG)
					printf("Restore best match\n");
#endif
			}

			/*
			 * Compute the offsets required by .2, copy back into
			 * user's subexpression buffer.  Note that we *might*
			 * not have used that buffer, because \1 must work,
			 * even if the user passed us nsub==0 or REG_NOSUB.
			 * (Note this may be a copy from sub to sub.)
			 */
			for (i = 0; i < nsub; i++)
				if (subs[i].rm_ep == NULL) {
					sub[i].rm_so = sub[i].rm_eo = -1;
					sub[i].rm_sp = sub[i].rm_ep = NULL;
				} else {
					sub[i].rm_so = subs[i].rm_so;
					sub[i].rm_eo = subs[i].rm_eo;
					sub[i].rm_sp = astring + subs[i].rm_so;
					sub[i].rm_ep = astring + subs[i].rm_eo;
				}
			return REG_OK;
		case SCAN_NL:	/* BOL_NL at start of expression */
			if (cp == string) {
				bp->bt.np = np;	/* push SCAN_NL and... */
				bp++->bt.cp = cp+1; /* next position */
				break;
			}
			if ((cp = STRCHR(cp, '\n')) == NULL)
				goto fail;
			cp++;	/* Point following newline */
			bp->bt.np = np;		/* push SCAN_NL and... */
			bp++->bt.cp = cp;	/* next position */
			break;
		case BOL_NL:
			if (cp > (UCHARTYPE *)astring && cp[-1] == '\n')
				break;
			/*FALLTHROUGH*/
		case BOL:
			if (cp > (UCHARTYPE *)astring || flags&REG_NOTBOL)
				goto fail;
			break;
		case EOL_NL:
			if (c == '\n')
				break;
			/*FALLTHROUGH*/
		case EOL:
			if (c != '\0' || flags&REG_NOTEOL)
				goto fail;
			break;
		case ANY_NL:
			if (c == '\n')
				goto fail;
			/*FALLTHROUGH*/
		case ANY:
			if (c == '\0')
				goto fail;
			cp++;
			break;
		case CHAR:
			if (c != np->u.c[0])
				goto fail;
			cp++;
			break;
		case STRING: {
			register wuchar_t *pp;	/* NOT chartype */

			for (pp = np->u.str; *pp != '\0'; )
				if (*cp++ != *pp++)
					goto fail;
		  }	break;
		case ANYOF2:
			if (np->u.c[0] != c && np->u.c[1] != c)
				goto fail;
			cp++;
			break;
		case ANYOF:
			if (c == '\0')
				goto fail;
			/*
			 * Its crazy, but true.  Only ranges work on a
			 * collating-element basis.  Thus, we have to at this
			 * point isolate the next collating-element.
			 * m_mccoll will return the next collating-element,
			 * and update the pointer
			 */
			c = MCCOLL((const CHARTYPE **)&cp);
			if (!classtest(np->u.clp, c))
				goto fail;
			break;
		case ANYBUT_NL:
			if (c == '\n')
				goto fail;
			/*FALLTHROUGH*/
		case ANYBUT:
			if (c == '\0')
				goto fail;
			c = MCCOLL((const CHARTYPE **)&cp);
			if (classtest(np->u.clp, c))
				goto fail;
			break;
		case FAIL:
			goto fail;
		case EMPTY:
			break;
		case SETSP:
			i = np->u.i;
			if (i < nsubs) {
				subs[i].rm_sp = (CHARTYPE*)cp;
				subs[i].rm_ep = NULL;
			}
			break;
		case SETEP:
			i = np->u.i;
			if (i < nsubs) {
				subs[i].rm_ep = (CHARTYPE*)cp;
			}
			break;
		case SEPR:
			/* SubExPression Reset */
			i = np->u.i;
			if (i < nsubs) {
				subs[i].rm_sp = NULL;
				subs[i].rm_ep = NULL;
			}
			break;
		case PUSH:
			/* push subexpr n */
			i = np->u.i;
			if (bp >= bstack+REG_BTSIZE*2-1)
				return REG_STACK;
			if (i < nsubs) {
				bp->rm.sp = subs[i].rm_sp;
				bp++->rm.ep = subs[i].rm_ep;
			}
			np = np->next;		/* skip to POP */
			bp->bt.np = np;		/* stack POP */
			bp++->bt.cp = cp;	/* unused */
			break;
		case POP:
			/* pop subexpr n */
			i = np->u.i;
			if (i < nsubs) {
				subs[i].rm_sp = (--bp)->rm.sp;
				subs[i].rm_ep = bp->rm.ep;
			}
			goto fail;
		case EXPAND:
			/*
			 * Back-reference to ith previous SETSP/ENDEP.
			 * Since we always have the subs array at least 9 in
			 * size, and at compile time checked \i to only 9,
			 * no need to check i < nsubs
			 */
			i = np->u.i;
			if (subs[i].rm_sp == NULL || subs[i].rm_ep == NULL)
				/*
				 * What should we do here?  Possible ballot.
				 *	(abc|(def))\2
				 * will bring us here on an abc.  \2 is valid,
				 * we've matched to this point, but \2 never
				 * actually matched anything!  Currently, just
				 * always assume that we match a null string.
				 * This has its own problems, in that this is
				 * the *only* way to match a null string, and
				 * applying a repeat to the \2 would then cause
				 * an infinite loop terminated by the back-stack
				 * overflow.
				 */
				break;
#ifdef DEBUG
			if (flags & REG_DEBUG)
			    if (sizeof(UCHARTYPE) == 1)
				printf("EXPAND `%.*s'\n",
					(int)(subs[i].rm_ep-subs[i].rm_sp),
					subs[i].rm_sp);
			    else
				printf("EXPAND `%.*S'\n",
					(int)(subs[i].rm_ep-subs[i].rm_sp),
					subs[i].rm_sp);
#endif
			if (memcmp((void *)subs[i].rm_sp, (void *)cp,
			  (size_t)((subs[i].rm_ep - subs[i].rm_sp)*sizeof(UCHARTYPE)))
									 != 0)
				goto fail;
			cp += subs[i].rm_ep - subs[i].rm_sp;
			break;
		case BRANCH:
			/*
			 * Try np->u.np; if it fails try np->next.
			 * Push back-track stack, fail on overflow
			 */
			if (bp >= bstack+REG_BTSIZE*2)
				return REG_STACK;
			bp->bt.np = np->next;
			bp++->bt.cp = cp;
			np = np->u.np;
			continue;

		/*
		 * Repeat construct looks something like:
		 * REPEATSTART  ->  REPEATINFO  ->  REPEAT  ->  ...
		 *		    u.min, max, n   u.np
		 *		    ^   	     -> stuff to repeat ->|
		 *		    <-----------------------------------<-|
		 * or:
		 * REPEATSTART -> REPEATCHECK -> REPEATINFO ->  REPEAT -> ...
		 *                u.str          u.min, max, n  u.np
		 *                ^                  -> stuff to repeat ->|
		 *		  <-------------------------------------<-|
		 * The REPEATSTART node is simply used to initialize the
		 * REPEATINFO loop counter.  The REPEAT node is simply used to
		 * hold the u.np repeated code.
		 * REPEATINFO does the real work, it checks for a count in
		 * range, and pushs that count on the stack.
		 * REPEATCHECK is used if the "stuff to repeat" could match
		 * an empty string and will exit the loop if it does.
		 */
		case REPEATSTART: {
			node *rp;

			rp = np->next;	/* The REPEATINFO or REPEATCHECK node */
			if (rp->type == REPEATCHECK) {
				rp->u.str = (wuchar_t *)cp; /* reset REPEATCHECK
								node */
				rp = rp->next;  /* the REPEATINFO node */
			}
			rp->U.n = 0;		/* Initialize to zero matches */
			np = rp->next->u.np;	/* Start with first node */

			/* If zero repetitions is valid, push it */
			if (rp->U.min == 0) {
				if (bp >= bstack+REG_BTSIZE*2)
					return REG_STACK;
				bp->bt.np = rp->next->next;
				bp++->bt.cp = cp;
			}
			continue;
		}

		case REPEATCHECK: {
			UCHARTYPE *str;
			/* 
			 * If we've matched an empty string stop the
			 * repeat loop, otherwise we'll keep on
			 * matching the emtpy string forever.
			 */
			str = (UCHARTYPE *)np->u.str;
			/*l If cp == str then we haven't gone anywhere (a
			 *  Null match) and if cp < str then we've backtracked
			 *  back behind where this repeat iteration started
			 *  from which means we need to pop the backtracking
			 *  stack some more to match up to where cp is now
			 */
			if (cp == str && np->next->U.n > 0 &&
			    np->next->U.n >= np->next->U.min) {
				/* the new .2b paragraphs for sections
				 * 2.8.3.3 (BRE) and 2.8.4.3 (ERE) */
				goto fail;
			}
			else if (cp == str && mmp > -1 &&
			    mmstack[mmp].id == np && mmstack[mmp].ss != 0) {
				/* empty; we want it to go around the rest
				 * of the if's and be treated as okay.
				 */
			}
			else if (cp <= str || str == NULL) {
				/*
				 * If we've already matched something,
				 * use that alternative instead.
				 */
				if (str != NULL && (np->next->U.min == 0 ||
				    np->next->U.n > 0)) {
					goto fail;
				}
				np = np->next->next->next;
				continue;
			}

			/*l set the stack */
			mmp++;
			if (mmp > REG_BTSIZE-1)
				return REG_STACK;
			mmstack[mmp].id = np;
			mmstack[mmp].ss = (int)(cp-str);

			/*l set the repeatpop2 */
			bp->bt.np = &repeatpop2;
			bp++->bt.cp = (UCHARTYPE *)np->u.str;
			bp->xr.np = &repeatpop2;/* onto stack */
			bp++->xr.ref = np;	/* make reference */
		        np->u.str = (wuchar_t *)cp;
			break;
		}

		case REPEATPOP:
			/*l
			 *  Restores the repeat counter to its correct value
			 *  during backtracking.
			 */
			np = bp->xr.ref;
			np->U.n--;
			goto fail;
			break;

		case REPEATPOP2:
			/*l
			 *  Restores the node string pointer to its
			 *  state at the time
			 */
			np = bp->xr.ref;
			bp--;
			--mmp;
			np->u.str = (wuchar_t *)bp->bt.cp;
			goto fail;
			break;

		case REPEATINFO:
			/*
			 * A full match of the repeated construct.
			 * Note that RE_DUP_MAX is UCHAR_MAX
			 */
			if (np->U.n < UCHAR_MAX) {
				/* Bump match count: maximum RE_DUP_MAX */
				++(np->U.n);
				bp->xr.np = &repeatpop;	/* onto stack */
				bp++->xr.ref = np;	/* make reference */
			}
			if (np->U.n == np->U.max && np->U.unbound == 0) {
				/*
				 * Success with the max count: don't go around
				 * any more, continue immediately with next.
				 */
				np = np->next->next;
				continue;
			}
			if (np->U.n >= np->U.min
			&& (np->U.n <= np->U.max || np->U.unbound)) {
				/* Success in range: stack this as a possible */
				if (bp >= bstack+REG_BTSIZE*2)
					return REG_STACK;
				bp->bt.np = np->next->next;
				bp++->bt.cp = cp;
			}
			/* Continue loop */
			np = np->next->u.np; /* Start again with first node */
			continue;

		case REPEAT:
			/*
			 * This node can't be hit.  The only real reason
			 * for this node, is so as not to require both the
			 * repeat loop information, and the branch pointer
			 * in the node, which would grow all nodes probably				 * from 4 to 8 bytes.
			 */
			return REG_EFATAL;

		case BREAK:
			/*
			 * Skip to the next occurence of a char
			 * Generated as a special case for `.*x', i.e. any
			 * number of characters, followed by one or more
			 * specific letters.
			 */
			if (c == '\0' ||
			    (cp = STRCHR((cp+1), np->u.c[0])) == NULL)
				goto fail;
			break;
		case BREAK_NL: {
			UCHARTYPE *ncp;

			/* Ditto BREAK, but don't cross \n */
			if (c == '\0' || c == '\n' ||
			    (ncp = STRCHR((cp+1), np->u.c[0])) == NULL)
				goto fail;
			/* If \n in string, and it came before match char,
			 * then we crossed a \n boundary -- fail */
			if ((cp = STRCHR((cp+1), '\n')) != NULL && cp < ncp)
				goto fail;
			cp = ncp;
		  }	break;
		case SCANTO:
			/* Unanchored regular expression starting with a char */
			if ((cp = STRCHR(cp, np->u.c[0])) == NULL) {
				goto fail;
			}
			string = cp;
			/* FALLTHROUGH */
		case SCAN:
			/*
			 * Unanchored regular expressions start with SCAN --
			 * we try each position until we get a match
			 */
			if (c != '\0') {
				/* always room on backtrack stack */
				bp->bt.np = np;	/* push SCAN and... */
				bp++->bt.cp = cp + 1;	/* Next position */
			}
			break;
#ifdef REG_WORDS
		case WORDB:
			/*
			 * Word matching.  np->u.i is zero for start of word
			 * match; non-zero for end of word match.
			 *
			 * Start of word: last character was not a token.
			 * End of word: last character was a token.
			 * If no last character, might be start of word.
			 */
			if ((cp == (UCHARTYPE *)astring || !istoken(cp[-1]))
			    == np->u.i)
				goto fail;
			/*
			 * If not a beginning of line, then don't match
			 * at start of string.
			 */
			if (np->u.i == 0
			&& (flags & REG_NOTBOL) && cp == (UCHARTYPE *)astring)
				goto fail;
			/*
			 * If not a token, then fail
			 */
			if ((!istoken(cp[0])) != np->u.i)
				goto fail;
			break;
#endif
		default:
			return REG_EFATAL;
		}

		/*
		 * Success: try next node.
		 */
		np = np->next;			/* never NULL */
		continue;

	  fail:
		/*
		 * Failure: pop the back-track stack.
		 */
#ifdef	DEBUG
		if (flags & REG_DEBUG) {
			stack *x;
			printf("FAILED. Backtrack stack:\n");
			for (x = bp; --x >= bstack; ) {
				printf("\t%d ", x->bt.cp - string);
				regprint(x->bt.np);
				if (x->bt.np->type == POP)
					--x;
			}
		}
#endif
		np = (--bp)->bt.np;
		if (np->type != REPEATPOP && np->type != REPEATPOP2)
			cp = bp->bt.cp;

		/*
		 * Backtracking over a SCAN, when we've had a previous match
		 * means we're done.  Go back and accept that previous match.
		 * This is because we want the longest *first* match.
		 * First have to see if we have ever gotten to the end of
		 * the string to match at least once.
		 */

		if (length != -1
		&& (np->type == SCAN
		    || np->type == SCANTO
		    || np->type == SCAN_NL)) {
				goto restoresubs;
		}
		else if (np->type == SCAN || np->type == SCANTO
		    || np->type == SCAN_NL)
			string++;
	}
	/* NOTREACHED */
#if 0	/* fixes a warning from Turbo C, causes a warning in other Cs */
	return REG_NOMATCH;	/* dummy to keep TurboC happy */
#endif
}
