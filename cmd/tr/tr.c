/*
 * Copyright (c) 1994 Sun Microsystems Inc.
 */

/*
 *	Parts of this product may be derived from
 *	International Business Machines Corp. tr.c and
 *	Berkeley 4.3 BSD systems licensed from International
 *	Business Machines Corp. and the University of California.
 */


/*
 * (C) COPYRIGHT International Business Machines Corp. 1985, 1993
 * All Rights Reserved
 */

/*
 * (c) Copyright 1990, 1991, 1992 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 *
 * OSF/1 1.1
 */

#include <alloca.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <stdio.h>
#include <regex.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <locale.h>
#include <limits.h>
#include <string.h>
#include <wchar.h>
#include <widec.h>
#include <wctype.h>
#include <libintl.h>
#include <euc.h>
#include <getwidth.h>
#include <strings.h>
#include "_libcollate.h"

#ifdef XPG4

#define	EOS		0xffffffff
#define	MAX_N		0xfffffff

/* Flag argument to next() */
#define	INSTRING1	1
#define	INSTRING2	2
#define	SIZEONLY	3

static	int	dflag = 0;
static	int	sflag = 0;
static	int	cflag = 0;

/*
 * Basic type for holding wide character lists;
 * mb must be first for qsort to work correctly.
 */
typedef struct {
	wchar_t		wc[2];
	wchar_t		translation;
	char		mb[MB_LEN_MAX + 1];
	unsigned	squeeze : 1;
	unsigned	delete : 1;
} char_info;

typedef struct {
	unsigned long	count;
	unsigned long	avail;
	char_info	*list;
} charset;

/*
 * Contains the complete list of valid wide character codes in collated order.
 */
static charset	character_set = {0, 0, NULL};


/*
 * Used to cache ranges, classes, and equivalence classes so we don't have
 * to recompute them each time we reference them.
 */
typedef struct {
	long	count;
	long	avail;
	long	offset;
	wchar_t	*chars;
} clist;

typedef struct {
	char	key[128];
	clist	clist;
} cache_node;

static struct cache {
	long		count;
	cache_node	*nodes;
} cache = { 0, NULL };

static	struct string {
	wchar_t	*p;	/* command line source String pointer.		*/
	char	*class;
	clist	*nextclass;
			/* nextclass = alternate source String pointer,	*/
			/* into string of members of a class.		*/
	int nchars;	/* Characters in string so far.			*/
	int totchars;	/* Characters in string.			*/
	int belowlower;	/* Characters below first [:lower:]		*/
	int belowupper;	/* Characters below first [:upper:]		*/
} string1, string2;


static clist	vector = { 0, 0, 0, NULL };
static clist	tvector = { 0, 0, 0, NULL };

static	char	*myName;

static unsigned long	arg_count;
static unsigned long	in_C_locale = 0;
static unsigned long	realloc_size = 1;


/*
 * NAME:	tr
 * FUNCTION:	copies standard input to standard output with substitution,
 *		deletion, or suppression of consecutive repetitions of
 *		selected characters.
 */

static void *
allocate(void *prev, size_t size)
{
	void	*ret;

	if (prev == NULL) {
		if ((ret = malloc(size)) == NULL) {
			perror(gettext("tr: Memory allocation failure"));
			exit(1);
		}
	} else {
		if ((ret = realloc(prev, size)) == NULL) {
			perror(gettext("tr: Memory reallocation failure"));
			exit(1);
		}
	}
	return (ret);
}


/*
 * NAME:	Usage
 * FUNCTION:	Issue Usage message to standard error and immediately terminate
 *               the tr command with return value 1.
 * ENTRY:
 * EXIT:
 */
static void
Usage(void)
{
	(void) fprintf(stderr,  gettext(
	"Usage: %s [ -cds ] [ String1 [ String2 ] ]\n"), myName);
	exit(1);
}

static int
charset_compare(const void *a, const void *b)
{
	unsigned long	awch = *(unsigned long *)a;
	unsigned long	bwch = *(unsigned long *)b;
	int		result;

	/*
	 * wcscoll is slow and goofs up on null comparisons in some
	 * locales.  Speed things up and protect ourselfs here.
	 */
	if (awch == bwch)
		return (0);

	if (((unsigned long)awch != 0) && ((unsigned long)bwch != 0)) {
		/*
		 * Neither character is a null: use the system routines
		 */
		if ((result = wcscoll(((wchar_t *)a), ((wchar_t *)b))) == 0) {
			/* further sort 'equal' collation sequences */
			return ((awch > bwch) ? 1 : -1);
		}
		return (result);
	}
	/*
	 * one of the characters was a NULL.
	 * Equal nulls were already handled
	 */
	return ((awch > bwch) ? 1 : -1);
}

static int
wccompare(const void *a, const void *b)
{
	static wchar_t	astr[2] = { 0, 0 };
	static wchar_t	bstr[2] = { 0, 0 };

	astr[0] = *(wchar_t *)a;
	bstr[0] = *(wchar_t *)b;
	return (charset_compare((void *)astr, (void *)bstr));
}

static char_info *
character_set_index(wchar_t wch)
{
	static wchar_t	key[2] = {0, 0};
	char_info	*return_value;

	if (in_C_locale)
		return (&character_set.list[wch]);

	key[0] = wch;
	return_value = (char_info *)bsearch(key, character_set.list,
	    character_set.count, sizeof (char_info), charset_compare);

	if (return_value == NULL) {
		fprintf(stderr, gettext("tr: Could not find wide character "
		    "'%C' (0x%x) in collation tables.\n"), wch, wch);
		exit(1);
	}
	return (return_value);
}

static cache_node *
cache_lookup(char *key)
{
	if (cache.count == 0)
		return (NULL);

	return ((cache_node *)bsearch(key, cache.nodes, cache.count,
	    sizeof (cache_node), (int (*)(const void *, const void *))strcmp));
}

static cache_node *
new_cache_node(char *key)
{
	long		count = cache.count + 1;
	cache_node	*node;

	cache.nodes = allocate(cache.nodes, sizeof (cache_node) * count);
	node = &cache.nodes[cache.count];
	cache.count = count;

	(void) strcpy(node->key, key);
	node->clist.avail = 0;
	node->clist.count = 0;
	node->clist.chars = NULL;

	/* sort the cache for bsearch */
	qsort(cache.nodes, count, sizeof (cache_node),
	    (int (*)(const void *, const void *))strcmp);

	return (cache_lookup(key));
}

static void
add_to_clist(clist *list, wchar_t wch)
{
	/* expand our list if we need more room */
	if (list->avail == 0) {
		list->chars = allocate(list->chars,
		    (list->count + realloc_size) * sizeof (wchar_t));
		list->avail = realloc_size;
	}
	list->chars[list->count++] = wch;
	list->avail--;
}


static void
add_to_character_set(wchar_t wch, char *mbyte, size_t bytes)
{
	char_info	*cinfo;

	/* expand our list if we will need more room */
	if (character_set.avail == 0) {
		character_set.list = allocate(character_set.list,
		    (character_set.count + realloc_size) * sizeof (char_info));
		character_set.avail = realloc_size;
	}

	cinfo = &character_set.list[character_set.count];

	bzero(cinfo, sizeof (char_info));

	/* put in wide character representation */
	cinfo->translation = cinfo->wc[0] = wch;

	/* copy in multibyte representation */
	(void) memcpy(cinfo->mb, mbyte, bytes);

	/* Adjust counters */
	character_set.count++;
	character_set.avail--;
}


static void
load_character_set(void)
{
	extern void		_reginit(void);
	extern int		_m_maxcoll(void);
	extern _coll_info	*_loaded_coll_;
	extern wchar_t		_cetowc(_coll_info *, int);
	unsigned char		ch[MB_LEN_MAX+1];
	char			*curr_locale, *curr_coll;
	char_info		**ptrs;
	wchar_t			wch;
	long			i, change_back = 0;
	int			csetsize, bytes;

	/*
	 * CTYPE and COLLATE may be different.  We rely on CTYPE's behaviour.
	 * Get the character set from the current ctype tables
	 */
	curr_locale = strdup(setlocale(LC_CTYPE, NULL));
	curr_coll = strdup(setlocale(LC_COLLATE, NULL));
	if (strcmp(curr_locale, curr_coll) != 0) {
		change_back = 1;
		(void) setlocale(LC_COLLATE, curr_locale);
	}

	if ((strcmp(curr_locale, "C") == 0) ||
	    (strcmp(curr_locale, "POSIX") == 0)) {
C_locale:	in_C_locale = 1;

		ch[0] = 0;
		add_to_character_set(0, (char *)ch, 1);
		while (++ch[0] != 0) {
			/* We don't need wide characters for C locale */
			add_to_character_set((wchar_t)ch[0], (char *)ch, 1);
		}
		return;
	}

	/* Load the collation table using regex's routines */
	_reginit();
	if ((_loaded_coll_ == NULL) || ((csetsize = _m_maxcoll()) <= 0)) {
		/*
		 * We don't know the characteristics of this locale
		 * so we default back to the C locale.
		 */
		(void) setlocale(LC_ALL, "C");
		goto C_locale;
	}

	/* Not in C locale */
	in_C_locale = 0;

	ch[0] = NULL;
	add_to_character_set(0, (char *)ch, 1);
	for (i = 0; i <= csetsize; i++) {
		if ((wch = _cetowc(_loaded_coll_, i)) <= 0)
			continue;
		if ((bytes = wctomb((char *)ch, wch)) <= 0)
			continue;
		if (ch[0] == '\0')
			continue;

		ch[bytes] = '\0';
		add_to_character_set(wch, (char *)ch, bytes);
	}

	/* Restore collation locale */
	if (change_back)
		(void) setlocale(LC_COLLATE, curr_coll);

	/* sort according to LC_COLLATE rules for range expressions. */
	qsort(character_set.list, character_set.count,
	    sizeof (char_info), charset_compare);
}


static clist *
character_class(const char *class, int flag)
{
	wctype_t	type;
	cache_node	*node;
	char		*expression;

	/* get class type code */
	if ((type = wctype(class)) == 0)
		return (NULL);

	expression = alloca(strlen(class) + (6 * sizeof (char)));
	(void) sprintf(expression,
	    "[:%s:]%d", class, (flag == INSTRING2) ? 1 : 0);

	if ((node = cache_lookup(expression)) == NULL) {
		unsigned long	index;
		long		count = character_set.count;
		char_info	*nextchar = character_set.list;

		/* we have not encountered this list before ... */
		node = new_cache_node(expression);

		/*
		 * lookup all characters in class.
		 * We use this method rather than regcomp / regexec to
		 * improve performance.
		 */
		if (flag == INSTRING2) {
			/*
			 * upper / lower are only allowed in string 2
			 * for case conversion purposes.  The cases are
			 * found by scanning for the opposite case and
			 * converting because there can be an unequal number
			 * of upper and lower case characters in a character
			 * set.  This method equalizes the number, converting
			 * only the characters that have conversions defined.
			 */
			if (strcmp(class, "lower") == 0) {
				for (index = 0; index < count; index++) {
					if (iswupper(nextchar->wc[0])) {
						add_to_clist(&(node->clist),
						    towlower(nextchar->wc[0]));
					}
					nextchar++;
				}
			} else if (strcmp(class, "upper") == 0) {
				for (index = 0; index < count; index++) {
					if (iswlower(nextchar->wc[0])) {
						add_to_clist(&(node->clist),
						    towupper(nextchar->wc[0]));
					}
					nextchar++;
				}
			}
		} else {
			if (in_C_locale && strcmp(class, "blank") == 0) {
				/*
				 * C locale isblank is broken so
				 * we do it manually
				 */
				add_to_clist(&(node->clist), L' ');
				add_to_clist(&(node->clist), L'\t');
			} else {
				/*
				 * All other classes are handled here
				 */
				for (index = 0; index < count; index++) {
					if (iswctype(nextchar->wc[0], type)) {
						add_to_clist(&(node->clist),
						    nextchar->wc[0]);
					}
					nextchar++;
				}
			}
		}
	}
	node->clist.offset = 0;
	return (&(node->clist));
}


static clist *
character_equiv_class(const wchar_t equiv)
{
	unsigned long	index;
	cache_node	*node;
	regex_t		compiled;
	char		*expression;

	expression = alloca(MB_CUR_MAX + 7);
	(void) sprintf(expression, "[[=%C=]]", equiv);

	if ((node = cache_lookup(expression)) == NULL) {
		/* we have not encountered this list before ... */
		node = new_cache_node(expression);

		/*
		 * lookup all characters in class.
		 * We use regcomp here since there is no other portable
		 * way to find equivalant characters.
		 */
		if (regcomp(&compiled, expression,
		    REG_EXTENDED|REG_NOSUB) != 0) {
			(void) fprintf(stderr, gettext("tr: Could not "
			    "compile regular expression \"%s\"\n"), expression);
			exit(1);
		}
		for (index = 0; index < character_set.count; index++) {
			if (regexec(&compiled, character_set.list[index].mb,
			    0, NULL, REG_NOTBOL | REG_NOTEOL) == 0) {
				add_to_clist(&(node->clist),
				    character_set.list[index].wc[0]);
			}
		}
		regfree(&compiled);
	}
	node->clist.offset = 0;
	return (&(node->clist));
}


static clist *
character_range(wchar_t low, wchar_t high)
{
	static char	*err = "tr: character 0x%x not defined in charmap\n";
	cache_node	*node;
	char		*expression;
	char_info	*lowp, *highp;

	expression = alloca(strlen("xxxxxxxx-xxxxxxxx")+1);
	(void) sprintf(expression, "%lx-%lx", low, high);

	if ((node = cache_lookup(expression)) == NULL) {
		/* we have not encountered this list before ... */
		node = new_cache_node(expression);

		/*
		 * lookup all characters in range.
		 */
		if ((lowp = character_set_index(low)) == NULL) {
			(void) fprintf(stderr, gettext(err), low);
			exit(1);
		}
		if ((highp = character_set_index(high)) == NULL) {
			(void) fprintf(stderr, gettext(err), high);
			exit(1);
		}

		/*
		 * we found both characters in set so now let's get all
		 * characters between them.
		 */
		if (lowp > highp) {
			while (highp <= lowp) {
				add_to_clist(&(node->clist), highp->wc[0]);
				highp++;
			}
		} else {
			while (lowp <= highp) {
				add_to_clist(&(node->clist), lowp->wc[0]);
				lowp++;
			}
		}
	}
	node->clist.offset = 0;
	return (&(node->clist));
}

static clist *
character_repeat(wchar_t wch, long count)
{
	cache_node	*node;
	char		*expression;

	expression = alloca(strlen("xxxxxxxxRxxxxxxxx")+1);
	(void) sprintf(expression, "%lxR%lx", wch, count);

	if ((node = cache_lookup(expression)) == NULL) {
		/* we have not encountered this repeat before ... */
		node = new_cache_node(expression);

		if (count == MAX_N) {
#ifdef XPG4
			/* extend pattend to end of string */
			count = string1.totchars - string2.totchars + 1;
#else
			/* extend repeat sequence past end of string */
			count = 2 * string1.totchars;
#endif
		}

		if (count > 0) {
			while (count--) {
				add_to_clist(&(node->clist), wch);
			}
		}
	}
	node->clist.offset = 0;
	return (&(node->clist));
}

/*
 * NAME: nextc
 *
 * FUNCTION: get the next character from string s with escapes resolved
 * EXIT:	1. IF (next character from s can be delivered as a single byte)
 *		   THEN return value = (int)cast of (next character or EOS)
 *		   ELSE error message is written to standard error
 *			and command terminates.
 */

static wchar_t
nextc(struct string *s)
{
	wchar_t	c;

	c = *s->p++;
	if (c == L'\0') {
		--s->p;
		return (EOS);
	} else if (c == L'\\') {
		/* Resolve escaped '\', '[' or null */
		switch (*s->p) {
		case L'0':
			c = '\0';
			s->p++;
			break;
		default:
			c = *s->p++;
		}
	}
	return (c);
}


static wchar_t
get_next_in_list(struct string *s)
{
	wchar_t nextchar = s->nextclass->chars[s->nextclass->offset++];

	if (s->nextclass->count <= s->nextclass->offset) {
		/* next round will continue on */
		s->nextclass = NULL;
	}
	s->nchars++;
	return (nextchar);
}


/*
 * NAME: next
 *
 * FUNCTION:	Get the next character represented in string s
 * ENTRY:	1. Flag = INSTRING1	- s points to string1
 *			  INSTRING2	- s points to string2
 *			  SIZEONLY	- compute string size only
 * EXIT:	1. IF (next character from s can be delivered as a single byte)
 *		   THEN return value = (int)cast of (next character or EOS)
 *		   ELSE error message is written to standard error
 *			and command terminates.
 */

static wchar_t
next(struct string *s, int flag)
{
	char		*class;
	int		c, n, bytes;
	int		base;
	wchar_t		basechar; /* Next member of char class to return */
	wchar_t		*dp;	  /* Points to ending : in :] of a class name */
	int		state;
	wchar_t		save1, save2;
	wchar_t		char1, char2, opchar;
	wchar_t		ret;

	/*
	 * If we are generating class members, ranges or repititions
	 * get the next one.
	 */
	if (s->nextclass != NULL) {
		return (get_next_in_list(s));
	}

	char1 = *s->p;
	save1 = nextc(s);

	if ((char1 == '[') || (*s->p == '-')) {
		/*
		 * Check for character class, equivalence class, range,
		 * or repetition in ASCIIPATH. Implementation uses a state
		 * machine to parse the POSIX syntax. Convention used is
		 * that syntax characters specified by POSIX must appear
		 * as explicit characters while user-specified characters
		 * (range endpoints, repetition character, and equivalence
		 *  class character) may use escape sequences.
		 */
/*
 * STATE  STRING SEEN       *p          ACTION
 *
 *   1    [=	            '*'         STATE=4		'[<char1>*' <char1>='='
 *			    '<char1>="  STATE=8		'[=<char1>='
 *			    other       STATE=9
 *
 *   2	  [:	            '*'		STARE=5		'[<char1>*' <char1>=':'
 *		            <class>:]	ACCEPT		'[:<class>:]'
 *			    other	STATE=9
 *
 *   3	  [<char1>          '*'		STATE=5		'[<char1>*'
 *			    other	STATE=9
 *
 *   4    [=*               '='		STATE=8		'[=<char1>=' <char1>='*'
 *                          '<digit>'	STATE=7		'[<char1>*<digit>'
 *							<char1>='='
 *                          ']'		ACCEPT		'[<char1>*]' <char1>='='
 *			    other	STATE=9
 *
 *   5	  [<char1>*         ']'		ACCEPT		'[<char1>*]'
 *			    '<digit>'	STATE=7         '[<char1>*<digit>]'
 *			    other	STATE=9
 *
 *   6    <char1>-          '<char2>'	ACCEPT		'<char1>-<char2>'
 *			    other	STATE=9
 *
 *   7    [<char1>*<digit>  '<digit>'	STATE=7a	'[<char1>*<digit>'
 *                          ']'		ACCEPT		'[<char1>*<digit>]'
 *			    other	STATE=9
 *
 *   7a   [<char1>*<digits> '<digit>'	STATE=7a	'[<char1>*<digits>'
 *                          ']'		ACCEPT		'[<char1>*<digits>]'
 *
 *   8    [=<char1>=        ']'		ACCEPT		'[=<char1>=]'
 *			    other	STATE=9
 *
 *   9    '[<other>' 'c-'		ACCEPT		'[' or 'c'
 *					(set to process second char next)
 *
 */
		n = MAX_N;	/* For short path to STATE_7b */

		if (char1 == L'[') {
			opchar = *((s->p)++);
			if (opchar == L'=')
				state = 1;
			else if (opchar == L':')
				state = 2;
			else {	/* Allow escape conversion of char1. */
				s->p--;
				char1 = (u_int)nextc(s);
				state = 3;
			}
		} else {
			s->p++;
			char1 = save1;
			state = 6;
		}
		while (state != 0)
		switch (state) {
		case 1:
			opchar = *((s->p)++);
			if (opchar == '*')
				state = 4;
			else { /* Allow escape conversion of char1. */
				s->p--;
				char1 = (u_int)nextc(s);
				if (*((s->p)++) == '=')
					state = 8;
				else
					state = 9;
			}
			break;
		case 2:
			opchar = *((s->p)++);
			if (opchar == '*')
				state = 5;
			else {
				/*
				 * Check for valid well-known character
				 * class name
				 */
				s->p--;
				if ((dp = wcschr(s->p, L':')) == NULL) {
					state = 9;
					break;
				}
				if (*(dp+1) != ']') {
					state = 9;
					break;
				}
				*dp = '\0';

				/* get class char list */
				bytes = (wcslen(s->p) + 1) * MB_LEN_MAX;
				class = alloca(bytes);
				(void) wcstombs(class, s->p, bytes);

				*dp = L':';
				s->p = dp + 2;

				/*
				 * Check invalid use of char class in String2:
				 */
				if ((flag == INSTRING2) &&
				    ((strcmp(class, "lower") == 0 &&
				    (s->nchars != string1.belowupper)) ||
				    (strcmp(class, "upper") == 0 &&
				    (s->nchars != string1.belowlower)))) {
					(void) fprintf(stderr, gettext("%s: "
					    "String2 contains invalid "
					    "character class '%s'.\n"),
					    myName, class);
					exit(1);
				}

				if (strcmp(class, "upper") == 0) {
					s->class = "upper";
					s->belowupper = s->nchars;
				} else if (strcmp(class, "lower") == 0) {
					s->class = "lower";
					s->belowlower = s->nchars;
				}
				s->nextclass = character_class(class, flag);

				/* handle bogus classes */
				if (s->nextclass == NULL) {
					state = 11;
					break;
				}

				/* handle empty classes */
				if (s->nextclass->count == 0) {
					s->nextclass = NULL;
					return (next(s, flag));
				}
				return (get_next_in_list(s));
			}
			break;
		case 3:
			opchar = *((s->p)++);
			if (opchar == '*')
				state = 5;
			else
				state = 9;
			break;
		case 4:
			opchar = *((s->p)++);
			if (opchar == '=') {
				char1 = '*';
				state = 8;
			} else if (iswdigit(opchar))
				state = 7;
			else if (opchar == ']') {
				n = MAX_N; /* Unspecified length */
				state = 10; /* 7b */
			} else
				state = 9;
			break;
		case 5:
			opchar = *((s->p)++);
			if (opchar == ']') {
				n = MAX_N; /* Unspecified length */
				state = 10; /* 7b */
			} else if (iswdigit(opchar))
				state = 7;
			else
				state = 9;
			break;
		case 6:
			if ((save2 = (u_int)nextc(s)) != EOS) {
				s->nextclass = character_range(char1, save2);
				return (get_next_in_list(s));
			} else {
				state = 9;
			}
			break;
		case 7:
			base = (opchar == '0') ? 8 : 10;  /* which base */
			basechar = (opchar == '0') ? '7' : '9';
			n = opchar - (u_int)'0';
			while (((c = (u_int)*s->p) >= '0') &&
			    (c <= (int) basechar)) {
				n = base*n + c - (u_int)'0';
				s->p++;
			}
			if (*s->p++ != ']') {
				state = 9;
				break;
			}
			if (n == 0)
				n = MAX_N; /* Unspecified length */
			/*FALLTHROUGH*/
		case 10:
			/*
			 * 7b, must follow case 7 without break;
			 * ACCEPT action for repetitions from states 4, 5, and
			 * 7.  POSIX 1003.2/D11 Rule: No repetitions in String1
			 */
			if (flag == INSTRING1) {
				(void) fprintf(stderr, gettext(
				    "%s: Character repetition in String1\n"),
				    myName);
				Usage();
			}

			if (flag == SIZEONLY) {
				s->nchars++;
				return (char1);
			}
			s->nextclass = character_repeat(char1, n);

			/* handle empty classes */
			if (s->nextclass->count == 0) {
				s->nextclass = NULL;
				return (next(s, flag));
			}
			return (get_next_in_list(s));
		case 8:
			if (*s->p++ == ']') {
			/* POSIX 1003.2/D11 Rule: No equiv classes in String2 */
				if (flag == INSTRING2 && (!dflag || !sflag)) {
					(void) fprintf(stderr, gettext(
					"%s: Equivalence class in String2\n"),
					myName);
					Usage();
				}
				/* get class char list */
				s->nextclass = character_equiv_class(char1);

				/* handle empty classes */
				if (s->nextclass->count == 0) {
					s->nextclass = NULL;
					return (next(s, flag));
				}
				return (get_next_in_list(s));
			} else state = 9;
			break;
		case 9:
			if ((save2 = (u_int)nextc(s)) != EOS) {
				/* remove the closing ] */
				if ((save1 = (u_int)nextc(s)) != (u_int)']') {
					(void) fprintf(stderr, gettext(
					    "%s: Bad string.\n"), myName);
					exit(1);
				}

				char2 = save2;
				s->nextclass = character_range(char1, char2);
				return (get_next_in_list(s));
			} else {
				ret = char1;
				s->p--;
			}
			state = 0;
			break;
		default: /* ERROR state */
			(void) fprintf(stderr, gettext(
			    "%s: Bad string between [ and ].\n"),
			    myName);
			exit(1);
			break;

		}
	} else {
		ret = save1;
	}
	s->nchars++;
	return (ret);
}




/*
 * remove_escapes - takes \seq and replace with the actual character value.
 * 		\seq can be a 1 to 3 digit octal quantity or {abfnrtv\}
 *
 *		This prevents problems when trying to extract multibyte
 * 		characters (entered in octal) from the translation strings
 *
 * Note:	the translation can be done in place, as the result is
 * 		guaranteed to be no larger than the source.
 */

static void
remove_escapes(wchar_t *s)
{
	wchar_t	*d = s;		/* Position in destination of next character */
	int	i, n;

	while (*s) {		/* For each character of the string */
		switch (*s) {
		default:
			*d++ = *s++;
			break;

		case L'\\':
			switch (*++s) {
			case L'0':
			case L'1':
			case L'2':
			case L'3':
			case L'4':
			case L'5':
			case L'6':
			case L'7':
				i = n = 0;
				while (i < 3 && *s >= L'0' && *s <= L'7') {
					n = n*8 + (*s++ - L'0');
					i++;
				}
				if (n == 0) { /* \000 */
					*d++ = L'\\';
					*d++ = L'0';
				} else if (n == L'\\') { /* \134 */
					*d++ = L'\\';
					*d++ = L'\\';
				} else if (n == L'[') { /* \133 */
					*d++ = L'\\';
					*d++ = L'[';
				} else if (n == L':') { /* \072 */
					*d++ = L'\\';
					*d++ = L':';
				} else if (n == L'=') { /* \075 */
					*d++ = L'\\';
					*d++ = L'=';
				} else
					*d++ = (wchar_t)n;
				break;

			case L'a':	*d++ = L'\a'; 	s++; 	break;
			case L'b':	*d++ = L'\b';	s++;	break;
			case L'f':	*d++ = L'\f';	s++;	break;
			case L'n':	*d++ = L'\n';	s++;	break;
			case L'r':	*d++ = L'\r';	s++;	break;
			case L't':	*d++ = L'\t';	s++;	break;
			case L'v':	*d++ = L'\v';	s++;	break;
			case L'\\':	*d++ = L'\\'; /* leave '\' escaped */
					*d++ = L'\\';    s++;	 break;

			default:
				*d++ = *s++;
				break;
			}
		} /* switch */
	} /* while (*s) */
	*d = L'\0';
}

void
main(int argc, char **argv)
{
	static char	*err1 =
	    "tr: Invalid character sequence in first argument\n";
	static char	*err2 =
	    "tr: Invalid character sequence in second argument\n";
	FILE		*fi = stdin, *fo = stdout;
	int		save = EOS;
	char_info	*idx;
	wint_t		wc;
	wchar_t		*temp, c, d, oc;
	int		need;

	realloc_size = PAGESIZE /* sizeof (wchar_t) */;

	/* Get locale variables from environment */
	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	/* save program name */
	myName = argv[0];

	/* Parse command line */
	while ((oc = getopt(argc, argv, "cds")) != -1) {
		switch (oc) {
		case 'c':
			cflag++;
			break;
		case 'd':
			dflag++;
			break;
		case 's':
			sflag++;
			break;
		default:
		/* Option syntax or bad option. */
			Usage();
		}
	}

	load_character_set();

	/* Get translation strings */
	arg_count = argc - optind;
	switch (arg_count) {
	case 2:
		need = strlen(argv[optind + 1]) + 1;
		string2.p = allocate(NULL, need * sizeof (wchar_t));
		(void) mbstowcs(string2.p, argv[optind + 1], need);
		remove_escapes(string2.p);

		need = strlen(argv[optind]) + 1;
		string1.p = allocate(NULL, need * sizeof (wchar_t));
		(void) mbstowcs(string1.p, argv[optind], need);
		remove_escapes(string1.p);
		break;

	case 1:
		need = strlen(argv[optind]) + 1;
		string1.p = allocate(NULL, need * sizeof (wchar_t));
		(void) mbstowcs(string1.p, argv[optind], need);
		remove_escapes(string1.p);
		string2.p = L"";
		break;

	case 0:
		string1.p = L"";
		string2.p = L"";
		break;

	default:
		/* More than two translation strings specified. */
		Usage();
	}


	string1.nextclass = NULL;
	string1.nchars = 0;
	string1.belowlower = -1;
	string1.belowupper = -1;

	/* expand out string 1 */
	temp = string1.p;
	while ((c = next(&string1, INSTRING1)) != EOS)
		add_to_clist(&vector, c);
	string1.p = temp;

	if (cflag) {
		/* complement the character set found in string 1 */
		int		i, j;

		/* sort list for quick scanning of character set */
		qsort(vector.chars, vector.count, sizeof (wchar_t), wccompare);

		/*
		 * Now loop through character set adding any characters
		 * not found in string one to our temp list.
		 */

		idx = character_set.list;
		for (j = 0, i = 0; i < character_set.count; i++) {
			if (vector.chars[j] == idx->wc[0])
				j++;
			else
				add_to_clist(&tvector, idx->wc[0]);
			idx++;
		}

		/* free up old vector's space */
		free(vector.chars);

		/* and point to new vector */
		vector = tvector;
	}
	string1.totchars = vector.count;

	string2.nextclass = NULL;
	string2.nchars = 0;
	string2.belowlower = -1;
	string2.belowupper = -1;

	temp = string2.p;
	while ((c = next(&string2, SIZEONLY)) != EOS)
		;
	string2.p = temp;
	string2.totchars = string2.nchars - 1;

	if (dflag) {
		vector.offset = 0;
		while (vector.offset < vector.count) {
			c = vector.chars[vector.offset++];
			idx = character_set_index(c);
			idx->delete = 1;
		}
		if (sflag && arg_count == 2) {
			temp = string2.p;
			while ((c = next(&string2, SIZEONLY)) != EOS) {
				idx = character_set_index(c);
				idx->squeeze = 1;
			}
			string2.p = temp;
		}
	} else if (sflag && arg_count == 1) {
		vector.offset = 0;
		while (vector.offset < vector.count) {
			c = vector.chars[vector.offset++];
			idx = character_set_index(c);
			idx->squeeze = 1;
		}
	} else {
		vector.offset = 0;
		string2.nextclass = NULL;
		string2.nchars = 0;
		while (vector.offset < vector.count) {
			c = vector.chars[vector.offset++];
			if ((d = next(&string2, INSTRING2)) == EOS)
				break;
			idx = character_set_index(c);
			idx->translation = d;
			if (sflag) {
				idx = character_set_index(d);
				idx->squeeze = 1;
			}
		}
	}

	if (in_C_locale) {
		int	ch;

		/* Read and process standard input using single bytes */
		for (;;) {
			if ((ch = getc(fi)) == EOF) {
				if (ferror(fi)) {
					perror(gettext("tr: Input file error"));
					exit(1);
				}
				break;
			}
#ifndef  XPG4
			if (ch == L'\0')
				continue;
#endif
			idx = &character_set.list[ch];
			if (!idx->delete) {
				ch = (int)idx->translation;
				if (!sflag) {
					(void) putc(ch, fo);
				} else if (save != ch) {
					idx = &character_set.list[ch];
					if (idx->squeeze)
						save = ch;
					else
						save = -1;
					(void) putc(ch, fo);
				}
			}
		}
		exit(0);
	}

	/*
	 * Not in C locale.  Collation sequence is unknown and is not
	 * based on binary character value...
	 */
	for (;;) {
		/* Get next input multi-byte character: */
		if ((wc = getwc(fi)) == EOF) {
			if (ferror(fi)) {
				perror(gettext("tr: Input file error"));
				exit(1);
			}
			break;
		}

		if ((idx = character_set_index(wc)) == NULL) {
			/*
			 * Anything that gets here is an invalid character,
			 * if -d and -c are set then the character is deleted.
			 * Otherwise we just pass it untranslated.
			 * Reason: -cd implies all characters not represented
			 *	   in first argument.  The characters can't
			 *	   be represented since they are illegal...
			 */
			if (!(dflag && cflag)) {
				(void) putwc((int)wc, fo);
			}
		} else if (!idx->delete) {
			wc = idx->translation;
			if (!sflag) {
				(void) putwc((int)wc, fo);
			} else if (save != wc) {
				idx = character_set_index(wc);
				if (idx != NULL && idx->squeeze)
					save = wc;
				else
					save = -1;
				(void) putwc((int)wc, fo);
			}
		}
	}
	exit(0);
}
#else  /* !XPG4 */
/*
 * Temporary hack to get around severe performance loss in large multibyte
 * locales.  We just use the old code which does not handle classes or
 * equivalence which require full knowledge of the character set.
 */
#define	DHL_ISBLANK
#ifdef DHL_ISBLANK /* WARNING! See the comments for isblank() */
static int	isblank(int c);
#endif /* DHL_ISBLANK */

#ifndef	TRUE
#define	TRUE	1
#define	FALSE	0
#endif

#define	NCHARS		256
#define	EOS		NCHARS+1
#define	MAX_N		0xffff

/* Flag argument to next() */
#define	INSTRING1	1
#define	INSTRING2	2
#define	SIZEONLY	3

/* wide char vars */
#define	MAXC	256
#define	CH(ss, i)	ss->ch[i]
#define	RNG(ss, i)	ss->rng[i]
#define	REP(ss, i)	ss->rep[i]
#define	NCH(ss)		ss->nch
#define	CODESET(c)	(ISASCII(c) ? 0 : (ISSET2(c) ? 2 : (ISSET3(c) ? 3 : 1)))
static	eucwidth_t	WW;
static	char		width[4];
static	FILE		*input;
static	char	cset;

static	struct	mstring {
	long	int	ch[MAXC];
	long	int	rng[MAXC];
	int	rep[MAXC];
	int	nch;
	} s1, s2;

static	void	mtr(u_char *, u_char *);
static	void	sbuild(struct mstring *, u_char *);
static	void	csbuild(struct mstring *, struct mstring *);
static	int	nextbyte(register u_char **);
static	int	getlc(register long int, u_char **);

/* end wide char stuff */


static	int	dflag = 0;
static	int	sflag = 0;
static	int	cflag = 0;
static	u_short	code[NCHARS];
static	u_char	squeez[NCHARS];
static	u_short	vect[NCHARS];
static	struct string {
	int	last;	/* most recently generated char.		*/
	int	max;	/* high end of range currently being generated	*/
			/* (if any), else 0.				*/
	int	rep;	/* number of repetitions of last left in	*/
			/* repetition currently being generated.	*/
	u_char	*p;	/* command line source String pointer.		*/
	u_short *nextclass;
			/* nextclass = alternate source String pointer,	*/
			/* into string of members of a class.		*/
	int nchars;	/* Characters in string so far.			*/
	int totchars;	/* Characters in string.			*/
	int belowlower;	/* Characters below first [:lower:]		*/
	int belowupper;	/* Characters below first [:upper:]		*/
} string1, string2;


static	char	*myName;

/* The POSIX Well-Known character class names. */
static	u_char asclasname [12] [8] = {
	"alnum",
	"alpha",
	"blank",
	"cntrl",
	"digit",
	"graph",
	"lower",
	"print",
	"punct",
	"space",
	"upper",
	"xdigit"
};

/* Hardcoded knowledge of POSIX locale character classes */
static	u_short alnum[] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
	'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
	'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
	'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', EOS
};

static	u_short alpha[] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
	'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
	'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
	'w', 'x', 'y', 'z', EOS
};

static	u_short blank[] = {
	' ', '\t', EOS
};

static	u_short cntrl[] = {
	'\00', '\01', '\02', '\03', '\04', '\05', '\06', '\07', '\10',
	'\11', '\12', '\13', '\14', '\15', '\16', '\17', '\20', '\21',
	'\22', '\23', '\24', '\25', '\26', '\27', '\30', '\31', '\32',
	'\33', '\34', '\35', '\36', '\37', '\177', EOS
};

static	u_short digit[] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', EOS
};

static	u_short graph[] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
	'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
	'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
	'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', '!', '\\', '\"', '#', '$', '%', '&', '\'', '(', ')',
	'*', '+', ',', '-', '.', '/', ':', ';', '<', '=', '>', '\?',
	'@', '[', '\\', ']', '^', '_', '`', '{', '|', '}', '~', EOS
};

static	u_short lower[] = {
	'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
	'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x',
	'y', 'z', EOS
};

static	u_short print[] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
	'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
	'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
	'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', '!', '\\', '\"', '#', '$', '%', '&', '\'', '(', ')',
	'*', '+', ',', '-', '.', '/', ':', ';', '<', '=', '>', '\?',
	'@', '[', '\\', ']', '^', '_', '`', '{', '|', '}', '~', ' ',
	EOS
};

static	u_short punct[] = {
	'!', '\\', '\"', '#', '$', '%', '&', '\'', '(', ')', '*', '+',
	',', '-', '.', '/', ':', ';', '<', '=', '>', '\?', '@', '[',
	'\\', ']', '^', '_', '`', '{', '|', '}', '~', EOS
};

static	u_short space[] = {
	'\t', '\n', '\13', '\14', '\15', ' ', EOS
};

static	u_short upper[] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
	'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', EOS
};

static	u_short xdigit[] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B',
	'C', 'D', 'E', 'F', 'a', 'b', 'c', 'd', 'e', 'f', EOS
};

/* 8-bit ascii character classes */
static	u_short *asclasmem [12] = {
	alnum,
	alpha,
	blank,
	cntrl,
	digit,
	graph,
	lower,
	print,
	punct,
	space,
	upper,
	xdigit
};


/*
 * Function prototypes
 */
static	int	next(struct string *, int flag);
static	int	nextc(struct string *);
static	void	remove_escapes(char *);
static	void	Usage();



/*
 * NAME:	tr
 * FUNCTION:	copies standard input to standard output with substitution,
 *		deletion, or suppression of consecutive repetitions of
 *		selected characters.
 */

main(int argc, char **argv)
{
	int		i;
	int		j;
	int		c;
	int		d;
	u_short		*compl;
	int		oc;
	char		*loc_val;
	u_char		*save_str;
	int		badopt;
	int		POSIXlc_ctype;
	int		POSIXlc_collate;



	/* Get locale variables from environment */
	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	/* save program name */
	myName = argv[0];

	/* Parse command line */
	badopt = 0;
	while ((oc = getopt(argc, argv, "cds")) != -1) {
		switch ((u_char)oc) {
		case 'c':
			cflag++;
			break;
		case 'd':
			dflag++;
			break;
		case 's':
			sflag++;
			break;
		default:
		/* Option syntax or bad option. */
			badopt++;
		}
	}

	/* Get translation strings */
	switch (argc-optind) {
	case 2:
		remove_escapes(argv[optind + 1]);
		string2.p = (u_char *) argv[optind + 1];
		remove_escapes(argv[optind]);
		string1.p = (u_char *)argv[optind];
		break;

	case 1:
		remove_escapes(argv[optind]);
		string1.p = (u_char *)argv[optind];
		string2.p = (u_char *)"";
		break;

	case 0:
		string1.p = (u_char *)"";
		string2.p = (u_char *)"";
		break;

	default:
		/* More than two translation strings specified. */
		badopt++;
	}

	/* If any command errors detected, issue Usage message and terminate. */
	if (badopt) {
		Usage();
	}

	/* multibyte */
	getwidth(&WW);
	if (WW._multibyte) {
		mtr(string1.p, string2.p);
		exit(0);
	}

	/*
	 *  Determine whether environment variables allow character classes
	 * and equivalence classes to be handled by POSIX locale tables.
	 */
	loc_val =  setlocale(LC_CTYPE, NULL);
	POSIXlc_ctype = ((strcmp(loc_val, "C") == 0) ||
	    (strcmp(loc_val, "POSIX") == 0));
	loc_val = setlocale(LC_COLLATE, NULL);
	POSIXlc_collate = ((strcmp(loc_val, "C") == 0) ||
	    (strcmp(loc_val, "POSIX") == 0));

	if (!POSIXlc_ctype || !POSIXlc_collate) {

		for (i = 0; i < (sizeof (asclasmem) / sizeof (char *)); i++) {
			if ((asclasmem[i] =
			    (u_short *) malloc(NCHARS * sizeof (u_short))) ==
			    (u_short *) NULL) {
				perror("tr");
				exit(1);
			}
		}

		for (i = 0, j = 0; i < NCHARS; i++) {
			if (isalnum(i))
				asclasmem[0][j++] = (u_short)i;
		}
		asclasmem[0][j] = EOS;

		for (i = 0, j = 0; i < NCHARS; i++) {
			if (isalpha(i))
				asclasmem[1][j++] = (u_short)i;
		}
		asclasmem[1][j] = EOS;

		for (i = 0, j = 0; i < NCHARS; i++) {
			if (isblank(i))
				asclasmem[2][j++] = (u_short)i;
		}
		asclasmem[2][j] = EOS;

		for (i = 0, j = 0; i < NCHARS; i++) {
			if (iscntrl(i))
				asclasmem[3][j++] = (u_short)i;
		}
		asclasmem[3][j] = EOS;

		for (i = 0, j = 0; i < NCHARS; i++) {
			if (isdigit(i))
				asclasmem[4][j++] = (u_short)i;
		}
		asclasmem[4][j] = EOS;

		for (i = 0, j = 0; i < NCHARS; i++) {
			if (isgraph(i))
				asclasmem[5][j++] = (u_short)i;
		}
		asclasmem[5][j] = EOS;

		for (i = 0, j = 0; i < NCHARS; i++) {
			if (islower(i))
				asclasmem[6][j++] = (u_short)i;
		}
		asclasmem[6][j] = EOS;

		for (i = 0, j = 0; i < NCHARS; i++) {
			if (isprint(i))
				asclasmem[7][j++] = (u_short)i;
		}
		asclasmem[7][j] = EOS;

		for (i = 0, j = 0; i < NCHARS; i++) {
			if (ispunct(i))
				asclasmem[8][j++] = (u_short)i;
		}
		asclasmem[8][j] = EOS;

		for (i = 0, j = 0; i < NCHARS; i++) {
			if (isspace(i))
				asclasmem[9][j++] = (u_short)i;
		}
		asclasmem[9][j] = EOS;

		for (i = 0, j = 0; i < NCHARS; i++) {
			if (isupper(i))
				asclasmem[10][j++] = (u_short)i;
		}
		asclasmem[10][j] = EOS;

		for (i = 0, j = 0; i < NCHARS; i++) {
			if (isxdigit(i))
				asclasmem[11][j++] = (u_short)i;
		}
		asclasmem[11][j] = EOS;
	}

	string1.last = 0;
	string1.max = 0;
	string1.rep = 0;
	string1.nextclass = (u_short *)0x0;
	string1.nchars = 0;
	string1.belowlower = -1;
	string1.belowupper = -1;

	string2.last = 0;
	string2.max = 0;
	string2.rep = 0;
	string2.nextclass = (u_short *)0x0;
	string2.nchars = 0;
	string2.belowlower = -1;
	string2.belowupper = -1;

	if (cflag) {
		for (i = 0; i < NCHARS; i++)
			vect[i] = 0;
		while ((c = next(&string1, INSTRING1)) != EOS)
			vect[c] = (u_short) 1;
		j = 0;
		for (i = 0; i < NCHARS; i++)
			if (vect[i] == 0)
				vect[j++] = (u_short) i;
		vect[j] = (u_short) EOS;
		string1.totchars = j;
		compl = vect;
	} else {
		/* Compute number of characters in string1 */
		save_str = string1.p;
		while ((c = next(&string1, INSTRING1)) != EOS)
			;
		string1.totchars = string1.nchars -1;
		string1.last = 0;
		string1.max = 0;
		string1.rep = 0;
		string1.nextclass = (u_short *)0x0;
		string1.nchars = 0;
		string1.belowlower = -1;
		string1.belowupper = -1;
		string1.p = save_str;
	}

	/* Compute number of characters in string2 */
	save_str = string2.p;
	while ((c = next(&string2, SIZEONLY)) != EOS)
		;
	string2.totchars = string2.nchars -1;
	string2.last = 0;
	string2.max = 0;
	string2.rep = 0;
	string2.nextclass = (u_short *)0x0;
	string2.nchars = 0;
	string2.belowlower = -1;
	string2.belowupper = -1;
	string2.p = save_str;

	for (i = 0; i < NCHARS; i++) {
		code[i] = EOS;
		squeez[i] = 0;
	}

	for (; ; ) {
		if (cflag)
			c = *compl++;
		else
			c = next(&string1, INSTRING1);
		if (c == EOS)
			break;
		d = next(&string2, INSTRING2);
		if (d == EOS)
			d = c;

		code[c] = (u_short) d;
		squeez[d] = 1;
	}
	if (sflag) {
		while ((d = next(&string2, INSTRING2)) != EOS) {
			squeez[d] = 1;
			/*
			 * we want to exhaust string2 as fast as
			 * possible.  we therefore reduce rep to 1, if
			 * not already there.
			 */
			if (string2.rep > 0)
			string2.rep = 1;
		}
	}
	for (i = 0; i < NCHARS; i++) {
		if (code[i] == EOS)
			code[i] = (u_short) i;
		else if (dflag)
			code[i] = EOS;
	}

	/* Read and process standard input */
	{
		FILE	*fi = stdin, *fo = stdout;
		int	sf = sflag;
		int	save = EOS;

		for (;;) {
			/* Get next input one-byte character: */
			if ((c = getc(fi)) == EOF)
				break;
#ifndef  XPG4
			if (c == '\0')
				continue;
#endif
			/* A little paranoia */
			if (c >= NCHARS)
				continue;

			if ((c = code[c]) != EOS) {
				if (!sf || c != save || !squeez[c]) {
					save = (u_short)c;
					(void) putc((int)c, fo);
				}
			}
		}
	}
	return (0);

}


/*
 * NAME: next
 *
 * FUNCTION:	Get the next character represented in string s
 * ENTRY:	1. LC_CTYPE and LC_COLLATE are each one of C, POSIX, or En_US
 *		2. Flag = INSTRING1	- s points to string1
 *			  INSTRING2	- s points to string2
 *			  SIZEONLY	- compute string size only
 * EXIT:	1. IF (next character from s can be delivered as a single byte)
 *		   THEN return value = (int)cast of (next character or EOS)
 *		   ELSE error message is written to standard error
 *			and command terminates.
 */
int
next(struct string *s, int flag)
{
	int		c, n;
	int		base;
	u_char		basechar;
				/* Next member of char class to return */
	u_char		*dp;	/* Points to ending : in :] of a class name */
	int		badstring, si, badclass;
	int		state, save1, save2;
	u_char		char1, char2, opchar;

	/* If we are generating class members, get the next one */
	if (s->nextclass != (u_short *)0x0) {
		if (*s->nextclass != EOS) {
			s->last = *s->nextclass++;
			s->nchars++;
			return (s->last);
		} else	s->nextclass = (u_short *)0x0;
	}

	if (--s->rep > 0) {
		s->nchars++;
		return (s->last);
	}
	if (s->last < s->max) {
		s->nchars++;
		return (++s->last);
	}

	s->max = 0;
	char1 = *s->p;
	save1 = (u_int)nextc(s);

	if ((char1 == '[') || (*s->p == '-')) {
		/*
		 * Check for character class, equivalence class, range,
		 * or repetition in ASCIIPATH. Implementation uses a state
		 * machine to parse the POSIX syntax. Convention used is
		 * that syntax characters specified by POSIX must appear
		 * as explicit characters while user-specified characters
		 * (range endpoints, repetition character, and equivalence
		 *  class character) may use escape sequences.
		 */
/*
 * STATE  STRING SEEN       *p          ACTION
 *
 *   1    [=	            '*'         STATE=4		'[<char1>*' <char1>='='
 *			    '<char1>="  STATE=8		'[=<char1>='
 *			    other       STATE=9
 *
 *   2	  [:	            '*'		STARE=5		'[<char1>*' <char1>=':'
 *		            <class>:]	ACCEPT		'[:<class>:]'
 *			    other	STATE=9
 *
 *   3	  [<char1>          '*'		STATE=5		'[<char1>*'
 *			    other	STATE=9
 *
 *   4    [=*               '='		STATE=8		'[=<char1>=' <char1>='*'
 *                          '<digit>'	STATE=7		'[<char1>*<digit>'
 *							<char1>='='
 *                          ']'		ACCEPT		'[<char1>*]' <char1>='='
 *			    other	STATE=9
 *
 *   5	  [<char1>*         ']'		ACCEPT		'[<char1>*]'
 *			    '<digit>'	STATE=7         '[<char1>*<digit>]'
 *			    other	STATE=9
 *
 *   6    <char1>-          '<char2>'	ACCEPT		'<char1>-<char2>'
 *			    other	STATE=9
 *
 *   7    [<char1>*<digit>  '<digit>'	STATE=7a	'[<char1>*<digit>'
 *                          ']'		ACCEPT		'[<char1>*<digit>]'
 *			    other	STATE=9
 *
 *   7a   [<char1>*<digits> '<digit>'	STATE=7a	'[<char1>*<digits>'
 *                          ']'		ACCEPT		'[<char1>*<digits>]'
 *
 *   8    [=<char1>=        ']'		ACCEPT		'[=<char1>=]'
 *			    other	STATE=9
 *
 *   9    '[<other>' 'c-'		ACCEPT		'[' or 'c'
 *					(set to process second char next)
 *
 */
		n = MAX_N;	/* For short path to STATE_7b */

		if (char1 == '[') {
			opchar = *((s->p)++);
			if (opchar == '=')
				state = 1;
			else if (opchar == ':')
				state = 2;
			else {	/* Allow escape conversion of char1. */
				s->p--;
				char1 = (u_int)nextc(s);
				state = 3;
			}
		} else {
			s->p++;
			char1 = (u_char) save1;
			state = 6;
		}
		while (state != 0) {
		switch (state) {
		case 1:
			opchar = *((s->p)++);
			if (opchar == '*')
				state = 4;
			else { /* Allow escape conversion of char1. */
				s->p--;
				char1 = (u_int)nextc(s);
				if (*((s->p)++) == '=')
					state = 8;
				else
					state = 9;
			}
			break;
		case 2:
			opchar = *((s->p)++);
			if (opchar == '*')
				state = 5;
			else {
				/*
				 * Check for valid well-known character
				 * class name
				 */
				s->p--;
				if ((dp = (u_char *)
				    strchr((char *)s->p, ':')) == NULL) {
					state = 9;
					break;
				}
				if (*(dp+1) != ']') {
					state = 9;
					break;
				}
				*dp = '\0';
				/* Until proven 0 by finding class name */
				badstring = 1;
				for (si = 0; si < 12; si++) {
					if (strcmp((char *)s->p,
					    (char *)asclasname[si]) == 0) {
						badstring = 0;
						break;
					}
				}
				if (badstring) {
					state = -1;
					break;
				} else {
					*dp = ':';
					s->p = dp + 2;
				}
				/*
				 * Check invalid use of char class in String2:
				 */
				if (flag == INSTRING2 && !(sflag && dflag)) {
					badclass = 0;
					switch (si) {
					case 6:
						if (s->nchars !=
						    string1.belowupper)
							badclass++;
						break;
					case 10:
						if (s->nchars !=
						    string1.belowlower)
							badclass++;
						break;
					default: badclass++;
					}
					if (badclass) {
						(void) fprintf(stderr, gettext(
			"%s: String2 contains an invalid character class.\n"),
						    myName);
						exit(1);
					}
				}

				/*
				 * For a char class: set string's alternate-
				 * source pointer to the hardcoded string of
				 * members of the class.
				 */
				if (si == 6 && s->belowlower == -1)
					s->belowlower = s->nchars;
				if (si == 10 && s->belowupper == -1)
					s->belowupper = s->nchars;
				s->nextclass = asclasmem[si];
				s->last = *s->nextclass++;
				state = 0;
			}
			break;
		case 3:
			opchar = *((s->p)++);
			if (opchar == '*')
				state = 5;
			else
				state = 9;
			break;
		case 4:
			opchar = *((s->p)++);
			if (opchar == '=') {
				char1 = '*';
				state = 8;
			} else if (opchar >= '0' && opchar <= '9')
				state = 7;
			else if (opchar == ']') {
				n = MAX_N; /* Unspecified length */
				state = 10; /* 7b */
			} else
				state = 9;
			break;
		case 5:
			opchar = *((s->p)++);
			if (opchar == ']') {
				n = MAX_N; /* Unspecified length */
				state = 10; /* 7b */
			} else if (opchar >= '0' && opchar <= '9')
				state = 7;
			else
				state = 9;
			break;
		case 6:
			if ((save2 = (u_int)nextc(s)) != EOS) {
				char2 = (u_char) save2;
				if (char2 < char1) {
					(void) fprintf(stderr, gettext(
					"%s: Range endpoints out of order.\n"),
					    myName);
					exit(1);
				}
				s->max  = char2;
				s->last = char1;
				state = 0;
			} else {
				state = 9;
			}
			break;
		case 7:
			base = (opchar == '0') ? 8 : 10;  /* which base */
			basechar = (opchar == '0') ? '7' : '9';
			n = opchar - (u_int)'0';
			while (((c = (u_int)*s->p) >= '0') &&
			    (c <= (int) basechar)) {
				n = base*n + c - (u_int)'0';
				s->p++;
			}
			if (*s->p++ != ']') {
				state = 9;
				break;
			}
			if (n == 0)
				n = MAX_N; /* Unspecified length */
			/*FALLTHROUGH*/
		case 10:
			/*
			 * 7b, must follow case 7 without break;
			 * ACCEPT action for repetitions from states 4, 5, and
			 * 7.  POSIX 1003.2/D11 Rule: No repetitions in String1
			 */
			if (flag == INSTRING1) {
				(void) fprintf(stderr, gettext(
				    "%s: Character repetition in String1\n"),
				    myName);
				Usage();
			}
			if (flag == SIZEONLY)
				n = 1;

#ifdef XPG4
			if (n == MAX_N)
				n = string1.totchars - string2.totchars +1;

#endif
			/* Could be ABC -> DE[X*0]F */
			if (n <= 0)
				char1 = next(s, INSTRING2);
			s->rep  = n;
			s->last = char1;
			state = 0;
			break;
		case 8:
			if (*s->p++ == ']') {
			/* POSIX 1003.2/D11 Rule: No equiv classes in String2 */
				if (flag == INSTRING2 && (!dflag || !sflag)) {
					(void) fprintf(stderr, gettext(
					"%s: Equivalence class in String2\n"),
					myName);
					Usage();
				}
				/* Ascii equivalence classes are one char */
				s->last = char1;
				state = 0;
			} else state = 9;
			break;
		case 9:
			if ((save2 = (u_int)nextc(s)) != EOS) {
				char2 = (u_char) save2;
				if (char2 < char1) {
					(void) fprintf(stderr, gettext(
					"%s: Range endpoints out of order.\n"),
					    myName);
					exit(1);
				}
				s->max  = char2;
				s->last = char1;
				/* remove the closing ] */
				if ((save1 = (u_int)nextc(s)) != (u_int)']') {
					(void) fprintf(stderr, gettext(
					    "%s: Bad string.\n"), myName);
					exit(1);
				}
			} else {
				s->last = char1;
				s->p--;
			}
			state = 0;
			break;
		default: /* ERROR state */
			(void) fprintf(stderr, gettext(
			    "%s: Bad string between [ and ].\n"),
			    myName);
			exit(1);
			break;

		}
		}
	} else {
		s->last = save1;
	}
	s->nchars++;
	return (s->last);
}


/*
 * NAME: nextc
 *
 * FUNCTION: get the next character from string s with escapes resolved
 * ENTRY:	1. EITHER (LC_CTYPE and LC_COLLATE are each one of
 *		            C or POSIX)
 *		   OR  -A option was specified.
 * EXIT:	1. IF (next character from s can be delivered as a single byte)
 *		   THEN return value = (int)cast of (next character or EOS)
 *		   ELSE error message is written to standard error
 *			and command terminates.
 */
int
nextc(struct string *s)
{
	u_char	c;

	c = *s->p++;
	if (c == '\0') {
		--s->p;
		return (EOS);
	} else if (c == '\\') {
		/* Resolve escaped '\', '[' or null */
		switch (*s->p) {
		case '0':
			c = '\0';
			s->p++;
			break;
		case '\\':
		case '[':
		default:
			c = (u_char)*s->p++;
		}
	}
	return ((int)c);
}



/*
 * remove_escapes - takes \seq and replace with the actual character value.
 * 		\seq can be a 1 to 3 digit octal quantity or {abfnrtv\}
 *
 *		This prevents problems when trying to extract multibyte
 * 		characters (entered in octal) from the translation strings
 *
 * Note:	the translation can be done in place, as the result is
 * 		guaranteed to be no larger than the source.
 */

void
remove_escapes(char *s)
{
	char	*d = s;		/* Position in destination of next byte */
	int	i, n;

	while (*s) {		/* For each byte of the string */
		switch (*s) {
		default:
			*d++ = *s++;
			break;

		case '\\':
			switch (*++s) {
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
				i = n = 0;
				while (i < 3 && *s >= '0' && *s <= '7') {
					n = n*8 + (*s++ - '0');
					i++;
				}
				if (n == 0) { /* \000 */
					*d++ = '\\';
					*d++ = '0';
				} else if (n == '\\') { /* \134 */
					*d++ = '\\';
					*d++ = '\\';
				} else if (n == '[') { /* \133 */
					*d++ = '\\';
					*d++ = '[';
				} else if (n == ':') { /* \072 */
					*d++ = '\\';
					*d++ = ':';
				} else if (n == '=') { /* \075 */
					*d++ = '\\';
					*d++ = '=';
				} else
					*d++ = n;
				break;

			case 'a':	*d++ = '\a'; 	s++; 	break;
			case 'b':	*d++ = '\b';	s++;	break;
			case 'f':	*d++ = '\f';	s++;	break;
			case 'n':	*d++ = '\n';	s++;	break;
			case 'r':	*d++ = '\r';	s++;	break;
			case 't':	*d++ = '\t';	s++;	break;
			case 'v':	*d++ = '\v';	s++;	break;
			case '\\':	*d++ = '\\'; /* leave '\' escaped */
					*d++ = '\\';    s++;	 break;

			default:
				*d++ = *s++;
				break;
			}
		} /* switch */
	} /* while (*s) */
	*d = '\0';
}

/*
 * NAME:	Usage
 * FUNCTION:	Issue Usage message to standard error and immediately terminate
 *               the tr command with return value 1.
 * ENTRY:
 * EXIT:
 */
void
Usage()
{
	(void) fprintf(stderr,  gettext(
	"Usage: %s [ -cds ] [ String1 [ String2 ] ]\n"), myName);
	exit(1);
}

#ifdef DHL_ISBLANK
/*
 * isblank():	see if a character is a blank.
 *	input:	character to be examined.
 *	output:	non-zero is character is a blank; zero otherwise.
 *	description:
 *		the purpose of this routine is to check a character to
 *	see if it's a space or a tab, in an international fashion.
 *
 * N.B.! at the time that this routine was written for spec1170 compliance,
 *	we didn't have an isblank() routine. isspace() comes close, but
 *	checks for more characters than space or tab. so, in order to get
 *	this operational in an international fashion, i've had to resort
 *	to hand crafting this routine. it's a total and temporary hack,
 *	and a bugid will be raised to correct it for the future (see
 *	the SCCS comments for the bugid).
 */

static int
isblank(c)
int c;
{
	return ((c == (char) 0x09 || c == (char) 0x20) ? 1 : 0);
}
#endif /* DHL_ISBLANK */


static	void
mtr(u_char *mstring1, u_char *mstring2)
{
	register	long	int	c;
	register	long	int	j;
	register	long	int	k;
	register	int	i;
	long	int	save = 0;

	width[0] = 1;
	width[1] = WW._eucw1;
	width[2] = WW._eucw2;
	width[3] = WW._eucw3;
	if (cflag) {
		sbuild(&s2, mstring1);
		csbuild(&s1, &s2);
	} else
		sbuild(&s1, mstring1);
	sbuild(&s2, mstring2);
	input = stdin;
	while ((c = getc(input)) != EOF) {
		if (c == 0)
			continue;
		switch (cset = CODESET(c)) {
		case 2:
		case 3:
			c = width[cset] ? getc(input) : 0;
			/*FALLTHROUGH*/

		case 1: c &= 0177;
			for (i = width[cset]; i-- > 1; )
				c = (c<<7)+(getc(input)&0177);
			c |= cset<<28;
			break;

		default:
			;
		}

		i = s1.nch;
		while (i-- > 0 && (c < s1.ch[i] || s1.ch[i] + s1.rng[i] < c))
			;
		if (i >= 0) { /* c is specified in mstring1 */
			if (dflag)
				continue;
			j = c-s1.ch[i]+s1.rep[i];
			while (i-- > 0)
				j += s1.rep[i]+s1.rng[i];
			/* j is the character position of c in mstring1 */
			for (i = k = 0; i < s2.nch; i++) {
				if ((k += s2.rep[i] + s2.rng[i]) >= j) {
					c = s2.ch[i] + s2.rng[i];
					if (s2.rng[i])
						c -= k - j;
					if (!sflag || c != save)
						goto put;
					else
						goto next;
				}
			}
		}

		i = s2.nch;
		while (i-- > 0 && (c < s2.ch[i] || s2.ch[i] + s2.rng[i] < c))
			;
		if (i < 0 || !sflag || c != save) {
		put:
			save = c;
			switch (cset = c >> 28) {
			case 0:
				(void) putchar(c);
				break;

			case 2:
				(void) putchar(SS2);
				goto multi_put;

			case 3:
				(void) putchar(SS3);

				/*FALLTHROUGH*/

			default:
			multi_put:
				for (i = width[cset]; i-- > 0; )
					(void) putchar(c >> 7 * i & 0177|0200);
			}
		}
	next:
		;
	}
}


static	void
sbuild(struct mstring *s, u_char *t)
{

	register	int	i;
	long	int	c;
	int	n;
	int	base;

#define	PLACE(i, c) { CH(s, i) = getlc(c, &t); REP(s, i) = 1; RNG(s, i) = 0; }

	for (i = 0; *t; i++) {
		if (i > MAXC)
			goto error;
		if (*t == '[') {
			t++;
			c = nextbyte(&t);
			cset = CODESET(c);
			PLACE(i, c)
			switch (*t++) {
			case '-':
				c = nextbyte(&t);
				if (cset == CODESET(c)) {
					if ((RNG(s, i) =
						getlc(c, &t)-CH(s, i)) < 0)
						goto error;
				} else {
					cset = CODESET(c);
					i++;
					PLACE(i, c)
				}
				if (*t++ != ']')
					goto error;
				break;

			case '*':
				base = (*t == '0') ? 8 : 10;
				n = 0;
				while ((c = *t) >= '0' && c < '0' + base) {
					n = base * n + c - '0';
					t++;
				}
				if (*t++ != ']')
					goto error;
				if (n == 0)
					n = 300000000;
				REP(s, i) = n;
				break;

			default:
			error:
				(void) fprintf(stderr, gettext(
				    "Bad string\n"));
				exit(1);
			}
		} else {
			c = nextbyte(&t);
			cset = CODESET(c);
			PLACE(i, c)
		}
	}
	NCH(s) = i;
}


static	void
csbuild(struct mstring *s, struct mstring *t)
{
	register	int	i;
	register	int	j;
	int	 k;
	int	 nj;
	char	link[MAXC];
	char	set[4];
	long	int	i_y;
	long	int	j_y;
	long	int	st;
#define	J	link[j]

	set[0] = 0; set[1] = 2; set[2] = 3; set[3] = 1;
	width[2]++;
	width[3]++;
	if (width[set[1]] > width[set[2]]) {
		i = set[1];
		set[1] = set[2];
		set[2] = (char) i;
	}
	if (width[set[2]] > width[set[3]]) {
		i = set[2];
		set[2] = set[3];
		set[3] = (char) i;
	}
	if (width[set[1]] > width[set[2]]) {
		i = set[1];
		set[1] = set[2];
		set[2] = (char) i;
	}
	width[2]--;
	width[3]--;
	NCH(s) = 0;
	for (cset = 0; cset < 4; cset++) {
		for (nj = 0, i = NCH(t); i-- > 0; ) {
			if (CH(t, i) >> 28 != set[cset])
				continue;
			for (j = 0; j < nj && (j_y = CH(t, J)+RNG(t, J)) <
			    CH(t, i); j++)
				;
			if (j >= nj)
				link[nj++] = (char) i;
			else if ((i_y = CH(t, i)+RNG(t, i)) < j_y) {
				if (CH(t, i) < CH(t, J)) {
					if (i_y < CH(t, J)-1) {
						for (k = nj++; k > j; k--)
							link[k] = link[k-1];
						link[j] = (char) i;
					} else {
						RNG(t, J) = j_y-(CH(t, J)
							= CH(t, i));
					}
				}
			} else if (CH(t, i) <= CH(t, J))
				link[j] = (char) i;
			else if (i_y > j_y)
				RNG(t, J) = i_y-CH(t, J);
		}
		/* "link" has the sorted order of CH for current code set. */
		for (st = (set[cset] << 28) + 1, i = NCH(s), j = 0;
		    j < nj; j++) {
			if (st < CH(t, J)) {
				RNG(s, i) = CH(t, J) -1 -(CH(s, i) = st);
				REP(s, i++) = 1;
			}
			st = CH(t, J)+RNG(t, J)+1;
		}
		for (k = width[set[cset]], j_y = 0; k > 0; k--)
			j_y = (j_y << 7) + 0177;
		if ((j_y |= set[cset] << 28) > st) {
			RNG(s, i) = j_y - (CH(s, i) = st);
			REP(s, i++) = 1;
		}
		NCH(s) = i;
	}
}


static	int
getlc(register long int c, u_char **t)
{
	register	int	i;

	switch (cset) {
	case 0:
		return (c);

	case 2:
	case 3:
		c = nextbyte(t);
		/*FALLTHROUGH*/

	default:
	case 1:
		c &= 0177;
		break;
	}
	for (i = width[cset]; i > 1; i--)
		c = (c << 7) + (nextbyte(t) & 0177);
	return (c|cset << 28);
}


static	int
nextbyte(register u_char **s)
{
	register	c;
	register	i;
	register	n;

	c = *(*s)++;
	if (c == '\\') {
		i = n = 0;
		while (i < 3 && (c = **s) >= '0' && c <= '7') {
			n = n*8 + c - '0';
			i++;
			(*s)++;
		}
		if (i > 0)
			c = n;
		else
			c = *(*s)++;
	}
	if (c == 0) *--(*s) = 0;
	return (c & 0377);
}
#endif /* !XPG4 */
