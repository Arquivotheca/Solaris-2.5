/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * Copyright 1993 by Mortice Kern Systems Inc.  All rights reserved.
 */
#ident	"@(#)regcol.c 1.12	95/10/05 SMI"

/*
 * regcol.c --
 *   Collection of collation routines to support the MKS regex functions
 *
 *   The MKS routines assumed a set table format of collating elements.
 *   An m_collel_t was an index into that table.  We use the relative weight
 *   from the our collation table as a unique identier for an m_collel_t
 */

#include "mks.h"	/* which includes i18n.h, m_collate.h, locale.h ... */
#include <string.h>
#include <unistd.h>
#include <locale.h>

#include "_libcollate.h"

#define	MAX_EQUIVS	4096		/* Hard limit for now */
#define	MAX_WEIGHT	0xFFFFFFFFF
#define	TMP_BUF_SIZ	4096

#define	NO_MULCHR_COL_ELM (_loaded_coll_->u.hp->no_of_coll_elms == 0)
/*
 * Collation routines from libcollate that are used
 */
order 	* _coll_get_order(encoded_val *, order *);
int 	_get_coll_elm(encoded_val *, const char *, collating_element *, int);
int 	_get_encoded_value(unsigned int *, encoded_val *);
_coll_info *_load_coll_(int *);

/*
 * Internal routines
 */
static m_collel_t order_to_collel_t(order *,  order *, encoded_val *);
static void 	encoded_to_mb(unsigned int, int, char []);
static int 	rweight_to_pweight(int);

/*
 * Failover routines
 * If we get load the collation tables fail over to these C locale routines
 */
char	*m_m_colltostr(m_collel_t);
int	m_m_collequiv(m_collel_t, m_collel_t **);
int	m_m_collrange(m_collel_t, m_collel_t, m_collel_t **);
m_collel_t	m_m_maxcoll(void);
m_collel_t	m_m_getmccoll(const char **);
m_collel_t m_m_strtocoll(char *);

/*
 * Debugging support
 */
#ifdef DEBUG_REGCOL
#define	_load_coll_	dummy_setlocale		/* XXX Debugging */
char *l_filename;				/* XXX Debugging */
#define	FILENAME (int)l_filename		/* "		" */
#endif /* DEBUG_REGCOL */

_coll_info *_loaded_coll_ = NULL;		/* Set by _reginit */

/*
 * void _reginit()
 *
 * This routines loads the collation table, if available, and sets
 * _loaded_coll to the table, else to NULL
 */

void
_reginit()
{
	char collname[256];
	char *cur_locale = (char *) _setlocale(LC_COLLATE, NULL);
	int ret;

	/*
	 * If we're in the C locale, or setlocale returned null
	 * we set loaded_coll_ to NULL, causing the regcol routines to
	 * failover to their C locale functionality
	 */
	if (cur_locale == NULL ||
	    strcmp(cur_locale, (const char *)"C") == 0 ||
	    strcmp(cur_locale, (const char *)"POSIX") == 0 ||
	    strcmp(cur_locale, (const char *)"en_US") == 0) {
		_loaded_coll_ = NULL;
		return;
	}
	/*
	 * Change this to call _fullocale()
	 */
	sprintf(collname, "/usr/lib/locale/%s/LC_COLLATE/coll.so", cur_locale);

	/*
	 * If the coll.so file exists, we set loaded_coll to NULL because...
	 * The policy is to use the coll.so file if it exists instead of
	 * the CollTable file - but coll.so doesn't support our collation
	 * functions so we have to failover to the C locale.
	 */
	if (access(collname, R_OK) == 0) {
		_loaded_coll_ = NULL;
		return;
	}

	/*
	 * Only got here if there's no coll.so - so attempt to load the
	 * table.
	 */
	_loaded_coll_ = _load_coll_(&ret);
}


/*
 * m_collel_t m_maxcoll()
 *
 *  Function that returns the maximum value of a m_collel_t in the
 *  current locale.
 */

m_collel_t
m_maxcoll()
{
	_coll_info *ci;
	order *last_order;

	/*
	 * Set ci to point to the loaded collation table.  Fail over if NULL
	 */
	if ((ci =  _loaded_coll_) == NULL)
		return (m_m_maxcoll());
	/*
	 * Get the last order in the table.  The nu_of_orders less 2 gets us
	 * the last valid entry.  The real "last" entry is a type null record
	 */
	last_order = &ci->_coll_starts.start_order[ci->u.hp->no_of_orders - 2];

	/*
	 * If the last element is an Ellipses we're semi hosed -
	 * return max value
	 */
	if (last_order->type == T_ELLIPSIS)
		return ((m_collel_t) MAX_WEIGHT);

	return ((m_collel_t) last_order->r_weight);
}


/*
 * m_getmccoll -- retrieve the next collating element
 * from a string; returning next position in the string.
 * Returns the collating element found.
 */
m_collel_t
m_getmccoll(s)
const char **s;
{
	int len;
	encoded_val en;
	_coll_info *ci;
	order *order;
	int ret;

	if (**s == '\0')
		return (-1);

	/*
	 * Set ci to point to the loaded collation table.  Fail over if NULL
	 */
	if ((ci =  _loaded_coll_) == NULL)
		return (m_m_getmccoll(s));

	/*
	 * If the char set contains no multi-character collating elements
	 * then take a short cut.
	 */

	if (NO_MULCHR_COL_ELM) {
		wchar_t wc;
		len = mbtowc(&wc, *s, 4);
		if (len < 0)
			return (-1);
		*s = *s + len;
		return (_wctoce(_loaded_coll_, wc));
	}

	/*
	 * Get the collating element
	 */
	if ((len = _get_coll_elm(&en, *s,
		ci->_coll_starts.start_coll,
		ci->u.hp->no_of_coll_elms)) != -1) {
		/*
		 * Got the collating element so get the order...
		 */

		*s = *s + len;	/* Point to next element */

		if ((order = _coll_get_order(&en,
			ci->_coll_starts.start_order)) != NULL) {
			/*
			 * Derive the relative weight from the order entry
			 */
			return (order_to_collel_t(
			    ci->_coll_starts.start_order, order, &en));
		}
	}

	/*
	 * Either _get_coll_elm failed or _coll_get_order failed.
	 * The MKS version just returns the next character but this seems
	 * bogus.  We return -1.
	 */
	return (-1);
}

/*
 * m_getwmccoll -- retrieve the next collating element
 * from a wchar string; returning next position in the string.
 * Returns the collating element found.
 */
m_collel_t
m_getwmccoll(s)
const wchar_t **s;
{
	int i = 0, j = 0, num_bytes;
	wchar_t *wstr;
	char str[TMP_BUF_SIZ], *strtable[TMP_BUF_SIZ], *strp;
	m_collel_t collel;

	if (**s == '\0')
		return (-1);
	if (_loaded_coll_ == NULL)
		return (*((*s)++));
	/*
	 * If the char set contains no multi-character collating elements
	 * then take a short cut.
	 */

	if (NO_MULCHR_COL_ELM) {
		return (_wctoce(_loaded_coll_, *((*s)++)));
	}

	/*
	 * Convert the entire string - up to some reasonable maximum into
	 * a multibyte string.  Assumes that mostly we'll get 1-2 char strings
	 * For each wchar converted save address where its multibyte string
	 * has been stored.
	 */

	i = j = 0;
	wstr = (wchar_t *) *s;
	while (*wstr) {
		strtable[j++] = &str[i];	/* Save address of element */
		num_bytes = wctomb(&str[i], *wstr);	/* Convert wchar */
		if (num_bytes < 0)
			break;
		i += num_bytes;
		if (i >= TMP_BUF_SIZ/4)
			break;
		wstr++;
	}

	strp = str;

	/*
	 * Now get the collating element out of the multibyte string.
	 */
	if ((collel = m_getmccoll((const char **)&strp)) == -1)
		return (-1);
	/*
	 * strp now points to the next collating element. Find that address
	 * in our table to determine which wide character is the start of
	 * the next collating element.
	 */
	for (i = 0; i < j; i++) {
		if (strp == strtable[i])
			break;
	}
	*s = &(*s)[i];
	return (collel);
}

/*
 * order_to_collel_t - get the relative weight and return it as a collel_t
 */

static m_collel_t
order_to_collel_t (first_o, o, en)
	order *first_o;			/* First order in the table */
	order *o;			/* The order to get rel weight */
	encoded_val *en;
{
	unsigned int ret = 0;

	/*
	 * XXX We assume one-to-many definitly has already been done
	 */
	switch (o->type) {
	/*
	 * List all the types for paranoia/thoroughness
	 */
	case T_CHAR_CHARMAP:
	case T_CHAR_ID:
	case T_CHAR_ENCODED:
	case T_CHAR_COLL_ELM:
	case T_CHAR_COLL_SYM:
	case T_UNDEFINED:
		ret = o->r_weight;
		break;
	case T_ELLIPSIS:
		if (o->encoded_val.length == 0) {
			/*
			 * The first element
			 */
			(void) _get_encoded_value(&ret, en);
		} else {
			unsigned int v1, v2;
			if (o == first_o)	/* Is Ellipsis first order? */
				v1 = 0;		/* yES, set to min */
			else
				(void) _get_encoded_value(&v1,
				    &(o-1)->encoded_val);

			(void) _get_encoded_value(&v2, en);
			ret = v2-v1 + (o-1)->r_weight;

		}
		break;
	case T_NULL:	/* XXX Also an error? */
		ret = MAX_WEIGHT;		/* Maximum value */
	}
	return ((m_collel_t) ret);
}

/*
 * m_strtocoll -  We've cloned m_getmccoll for now.  Double check.
 *
 */
m_collel_t
m_strtocoll(s)
char *s;
{
	int len;
	encoded_val en;
	_coll_info *ci;
	order *order;
	int ret;

	if (*s == '\0')
		return (-1);

	/*
	 * Set ci to point to the loaded collation table.  Fail over if NULL
	 */
	if ((ci =  _loaded_coll_) == NULL)
		return (m_m_strtocoll(s));
	/*
	 * If the char set contains no multi-character collating elements
	 * then take a short cut.
	 */

	if (NO_MULCHR_COL_ELM) {
		wchar_t wc;
		len = mbtowc(&wc, s, 4);
		if (len < 0)
			return (-1);
		return (_wctoce(_loaded_coll_, wc));
	}

	/*
	 * Get the collating element
	 */
	if ((len = _get_coll_elm(&en, s,
		ci->_coll_starts.start_coll,
		ci->u.hp->no_of_coll_elms)) != -1) {
		/*
		 * Got the collating element so get the order...
		 */

		if ((order = _coll_get_order(&en,
			ci->_coll_starts.start_order)) != NULL) {
			/*
			 * Derive the relative weight from the order entry
			 */
			return (order_to_collel_t(
			    ci->_coll_starts.start_order, order, &en));
		}
	}

	/*
	 * Either _get_coll_elm failed or _coll_get_order failed.
	 * The MKS version just returns the next character but this seems
	 * bogus.  We return -1.
	 */
	return (-1);
}

/*
 * m_collrange
 * Given the character index values of two end points,
 * return all collating elements between them.  Either end point could be
 * a character, e.g. 'a', or a many-to-one mapping, e.g. `ch'.
 * Return is a count of # entries in the range; -1 indicates invalid endpoint
 * (i.e. > M_CSETSIZE + # of many-to-one mappings); 0 indicates first range
 * is greater than second.  Other return is the third arg, which is a m_collel_t
 */

int
m_collrange(m_collel_t start, m_collel_t end, m_collel_t **index)
{
	m_collel_t *colltable, the_end;
	int ret, offset;

	/*
	 * Check parameters to see if they're in range
	 */
	the_end = m_maxcoll();

	if (start > the_end || start == -1 || end > the_end || end == -1)
		return (-1);
	if (start > end)
		return (0);

	/*
	 * Get a pointer to the collation table
	 */
	if (m_collorder(&colltable) < 0)
		return (-1);

	/*
	 * Assumes that collation table is monotonically increasing
	 * So the first element is the amount other elements are offset by.
	 */
	offset = (int) colltable[0];

	/*
	 * One more range check. Start & end should be greater than offset
	 */
	if (start >= offset && end >= offset) {
		*index = &colltable[start - offset];
		return (end-start+1);
	} else
		return (-1);
}

/*
 * m_collequiv
 * Given the character index values of a collating
 * element, return all collating elements whose primary weight is the same.
 * Return is a count of # entries in the class; -1 indicates invalid input
 * Other return is the third arg, which is a pointer
 * to the equivalence table.
 */

int
m_collequiv(m_collel_t ch, m_collel_t **index)
{
	_coll_info *ci;
	order *order;
	int num_orders;
	static m_collel_t *c_table = NULL;
	int i, range;
	int equivs = 0;			/* Number of elements added to table */
	int ch_weight, prev_uw, next_uw;
	int prev_rw, next_rw;

	/*
	 * Set ci to point to the loaded collation table.  Fail over if NULL
	 */
	if ((ci =  _loaded_coll_) == NULL)
		return (m_m_collequiv(ch, index));

	/*
	 * Since localedef doesn't provide us with an equivalance table we
	 * malloc one here.
	 * Note also to check that "equivs" doesn't exceed MAX_EQUIVS.
	 * XXX Note this isn't MT safe!  We need an equivalence table built
	 * localedef.
	 */
	if (c_table == NULL &&
	    (c_table = malloc(MAX_EQUIVS * sizeof (m_collel_t))) == NULL)
		return (-1);

	/*
	 * Get the primary weight that corresponds to this m_collel_t
	 */
	ch_weight = rweight_to_pweight(ch);

	/*
	 * Now go through the order table looking for all entries that have
	 * the same primary weight as ch_weight
	 */
	order = ci->_coll_starts.start_order;
	num_orders = ci->u.hp->no_of_orders - 1;

	for (i = 0; i < num_orders; i++) {
		switch (order->type) {
		case T_UNDEFINED:
		case T_NULL:
			break;
		/*
		 * XXX Review
		 */
		case T_CHAR_CHARMAP:
		case T_CHAR_ID:
		case T_CHAR_ENCODED:
		case T_CHAR_COLL_ELM:
		case T_CHAR_COLL_SYM:
			switch (order->weights[0].type) {
			case WT_IGNORE:
			case WT_ONE_TO_MANY:	/* XXX What does MKS do? */
				break;
			case WT_ELLIPSIS:
				break;		/* Must be error in table */
			case WT_RELATIVE:
				if (order->weights[0].u.weight == ch_weight)
					c_table[equivs++] =
					    order->r_weight;
				break;
			default:
				break;		/* Error? */
			}
			break;

		case T_ELLIPSIS:
			/*
			 * If the type is an ellipsis then get the prev entry
			 * weight[0] entry and next extry weight[0].
			 * Handle case's where ... is first, last or only
			 *     entry.
			 */
			if (i == 0)		/* Ellipsis is first entry */
				prev_rw = prev_uw = 0;
			else {
				prev_uw = order[-1].weights[0].u.weight;
				prev_rw = order[-1].r_weight;
			}

			if (i == num_orders)	/* Ellipsis is last entry */
				next_rw = next_uw = MAX_WEIGHT;
			else {
				next_uw = order[1].weights[0].u.weight;
				next_rw = order[1].r_weight;
			}
			/*
			 * Check to see if ch_weight is within the range
			 */
			if (! (ch_weight >= prev_uw && ch_weight <= next_uw))
				break;		/* Not in range - break */
			/*
			 * We're in range - check if weight type is r_weight
			 * and the weight matches our weight...
			 */
			switch (order->weights[0].type) {
			case WT_RELATIVE:
				if (ch_weight == order->weights[0].u.weight) {
					/*
					 * type is relative so enumerate
					 * all the elements with this weight.
					 */
					for (i = prev_rw; i < next_rw; i++) {
						c_table[equivs++] = i;
						if (equivs >= MAX_EQUIVS)
							break;
					}
				}
				break;
			case WT_ELLIPSIS:
				range = ch_weight - prev_uw;
				c_table[equivs++] =
				    (prev_rw + range);
				break;
			case WT_ONE_TO_MANY:
			case WT_IGNORE:
				break;
			}
		default:
			break;		/* definitly an error */
		}
		if (equivs >= MAX_EQUIVS)
			break;

		order++;	/* Next order */
	}			/* end For loop */
	*index = c_table;
	return (equivs);
}

/*
 * rweight_to_pweight	- Look up in the order table the relative weight
 *			  that matches this weight and return the primary
 *			  weight.
 */

static int
rweight_to_pweight(ch)
	int ch;
{
	_coll_info *ci;
	order *order;
	int num_orders;
	int i, range;
	int prev_rw, next_rw, prev_uw;

	/*
	 * Set ci to point to the loaded collation table.  Fail over if NULL
	 */
	if ((ci =  _loaded_coll_) == NULL)
		return (-1);

	order = ci->_coll_starts.start_order;
	num_orders = ci->u.hp->no_of_orders - 1;
	for (i = 0; i < num_orders; i++) {
		switch (order->type) {
		case T_UNDEFINED:
		case T_NULL:
			break;
		case T_CHAR_CHARMAP:
		case T_CHAR_ID:
		case T_CHAR_ENCODED:
		case T_CHAR_COLL_ELM:
		case T_CHAR_COLL_SYM:
		    if (order->r_weight == ch) {
			switch (order->weights[0].type) {
			case WT_IGNORE:
			case WT_ONE_TO_MANY:
			case WT_ELLIPSIS:
				break; 	/* This case shouldn't happen */
			case WT_RELATIVE:
				return (order->weights[0].u.weight);
			default:
				break;		/* Error? */
			}
		    }
		    break;

		case T_ELLIPSIS:
			/*
			 * If the type is an ellipsis then get the prev entry
			 * weight[0] entry and next extry weight[0].
			 * Handling case's where "..." is first, last or only
			 *     entry.
			 */
			if (i == 0)		/* Ellipsis is first entry */
				prev_uw = prev_rw = 0;
			else {
				prev_rw = order[-1].r_weight;
				prev_uw = order[-1].weights[0].u.weight;
			}

			if (i == num_orders)	/* Ellipsis is last entry */
				next_rw = MAX_WEIGHT;
			else
				next_rw = order[1].r_weight;
			/*
			 * Check to see if ch_weight is within the range
			 */
			if (! (ch >= prev_rw && ch <= next_rw))
				break;		/* Not in range - break */
			/*
			 * We're in range - check if weight type is r_weight
			 * and the weight matches our weight...
			 */
			switch (order->weights[0].type) {
			case WT_RELATIVE:
				return (order->weights[0].u.weight);
			case WT_ELLIPSIS:
				range = ch - prev_rw;
				return (prev_uw + range);
			case WT_ONE_TO_MANY:
			case WT_IGNORE:
				break;
			}
		default:
			break;		/* definite error */
		}
		order++;
	}
	return (-1);
}



/*
 * m_colltostr	- Return the character(s) that coresponds to the
 *		  m_collel_t
 */

char *
m_colltostr(m_collel_t n)
{
	_coll_info *ci;
	order *order;
	int num_orders;
	int i, ret, range, len;
	int prev_rw, next_rw;
	wchar_t wchar;
	static char null_string[] = "\0";
	static char mbuf[256];		/* Not MT safe */
	encoded_val en;
	unsigned int v1;

	/*
	 * Set ci to point to the loaded collation table.  Fail over if NULL
	 */
	if ((ci =  _loaded_coll_) == NULL)
		return (m_m_colltostr(n));
	/*
	 * If the char set contains no multi-character collating elements
	 * then take a short cut.
	 */

	if (NO_MULCHR_COL_ELM) {
		wchar = _cetowc(_loaded_coll_, n);
		if (wchar < 0 || (len = wctomb(mbuf, wchar)) == -1)
			mbuf[0] = '\0';
		return (mbuf);
	}

	order = ci->_coll_starts.start_order;
	num_orders = ci->u.hp->no_of_orders - 1;
	for (i = 0; i < num_orders; i++) {
		switch (order->type) {
		case T_UNDEFINED:
		case T_NULL:
			if (n == order->r_weight)
				return (null_string);
		/*
		 * all of these contain the literal bytes - just return
		 * pointer to those.
		 */
		case T_CHAR_CHARMAP:
		case T_CHAR_ID:
		case T_CHAR_ENCODED:
		case T_CHAR_COLL_ELM:
		case T_CHAR_COLL_SYM:
			if (n == order->r_weight) {	/* Return the bytes */
				return ((char *) order->encoded_val.bytes);
			}
			break;
		case T_ELLIPSIS:
			/*
			 * If the type is an ellipsis then get the prev entry
			 * weight[0] entry and next extry weight[0].
			 * Handling case's where ... is first, last or only
			 *     entry.
			 */
			if (i == 0)		/* Ellipsis is first entry */
				prev_rw = 0;
			else
				prev_rw = order[-1].r_weight;

			if (i == num_orders)	/* Ellipsis is last entry */
				next_rw = MAX_WEIGHT;
			else
				next_rw = order[1].r_weight;
			/*
			 * Check to see if n is within the range
			 */
			if (! (n >= prev_rw && n <= next_rw))
				break;		/* Not in range - break */
			/*
			 * We're in range - Now generate the string.
			 */
			range = n - prev_rw;
			if (i == 0) {	/* Ellipsis was first char */
				len = 1;
				/*
				 * Derive length - a hack
				 */
				if (range & 0xFF000000)
					len = 4;
				else if (range & 0x00FF0000)
					len = 3;
				else if (range & 0x0000FF00)
					len = 2;

				encoded_to_mb(range, len, mbuf);
			} else {
				len = _get_encoded_value(&v1,
					&(&order[-1])->encoded_val);
				v1 += range;
				/*
				 * Convert the encoded val into a multibyte
				 */
				encoded_to_mb(v1, len, mbuf);
			}
			return (mbuf);
		default:
			break;		/* definite error */
		}
		order++;
	}
	return (NULL);
}


/*
 * _m_ismccollel(ce)
 * Check if the ce is a multi-character collating element.
 * Returns 1 if it is, 0 if it isn't.
 * If an error occurrs we also return 1.  This is because all the callers
 * of this routine bail out if this code returns a 1 anyway.
 *
 * Currently this is really slow.  We need a fast way to do this.
 */

_m_ismccollel(ce)
	m_collel_t ce;
{
	char *str;
	wchar_t wstr[256];
	int len;

	/*
	 * If no collation table is loaded, default to C locale semantics
	 * - no multi char collating elements
	 */
	if (_loaded_coll_ == NULL)
		return (0);
	/*
	 * Check first if there are any multichar col elements at all.
	 */
	if (NO_MULCHR_COL_ELM)
		return (0);

	/*
	 * Convert the collating element to a string
	 */
	if ((str = m_colltostr(ce)) == NULL)
		return (1);			/* This is actually fatal */

	/*
	 * Determine the number of wchars in the string
	 */
	if ((len = mbstowcs(wstr, str, 256)) < 0)
		return (1);			/* Also fatal */

	/*
	 * If we've got more than one character, then return 1, else 0
	 */
	if (len > 1)
		return (1);
	else
		return (0);
}

/*
 * encoded_to_mb 	- convert an encoded val into a multibyte string
 */
static void
encoded_to_mb(val, len, mbuf)
	int len;
	unsigned int val;
	char mbuf[];
{
	int i;

	for (i = len -1; i >= 0; i--) {
		mbuf[i] = val & 0xFF;
		val = val >> 8;
	}
	mbuf[len] = 0;
}

/*
 * m_collorder -  Return all collating elements in collating order.
 *		  Assumes m_collel_t's increase by one with no holes
 * Return:
 *   - a count of # entries in the range
 *   - set 'index' to point to collation table
 */
int
m_collorder(m_collel_t **index)
{
	int maxcoll, i;
	static m_collel_t *colltable = NULL;
	static last_maxcoll = 0;

	maxcoll = m_maxcoll();

	if (colltable == NULL || maxcoll != last_maxcoll) {
		if ((colltable = malloc((maxcoll + 1) * sizeof (m_collel_t))) ==
		    NULL)
			return (-1);
		for (i = 0; i <= maxcoll; i++)
			colltable[i] = i;
	}
	last_maxcoll = maxcoll;

	*index = colltable;
	return (maxcoll);
}

/*
 * Failover stuff in case we can't load the collation table.  We use the
 * stock MKS routines from generic.c
 */
/*
 * generic.c --
 *   Collection of collation routines that can be used on sytems
 *   that have locales with the same collation order as the machine ordering.
 *
 *
 * Copyright 1993 by Mortice Kern Systems Inc.  All rights reserved.
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
 */

/*
 * m_collel_t m_maxcoll()
 *
 *  Function that returns the maximum value of a m_collel_t in the
 *  current locale.
 */
m_collel_t
m_m_maxcoll()
{
	return (M_CSETSIZE);
}

/*
 * m_getmccoll -- retrieve the next, possibly multi-character collating element
 * from a string; returning next position in the string.
 */
LDEFN m_collel_t
m_m_getmccoll(s)
const char **s;
{
	/*
	 * Return -1 if we're passed a null string
	 */
	if (*s == '\0')
		return (-1);
	return (*(unsigned char *)(*s)++);
}

/*
 * Single characters will be returned unchanged, while multi-character
 * strings which don't match will cause a return of -1.
 * Multi-character strings which match will return M_CSETSIZE+n, where
 * n is the internal index into the one-to-many tables.
 */
LDEFN m_collel_t
m_m_strtocoll(s)
char *s;
{
	if (*s != '\0' && s[1] != '\0')
		return (-1);
	return (*(unsigned char *)s);
}

/*
 * mks private function.  Given the character index values of two end points,
 * return all collating elements between them.  Either end point could be
 * a character, e.g. 'a', or a many-to-one mapping, e.g. `ch', in which case
 * it has a value greater than M_CSETSIZE.
 * We use two arrays which localedef carefully constructed for this problem,
 * the first when indexed by the character index, returns a collating index
 * into the second.  The second then has the correct character values in
 * order of collation.
 * Return is a count of # entries in the range; -1 indicates invalid endpoint
 * (i.e. > M_CSETSIZE + # of many-to-one mappings); 0 indicates first range
 * is greater than second.  Other return is the third arg, which is a pointer
 * into the collation table.
 */
LDEFN int
m_m_collrange(m_collel_t start, m_collel_t end, m_collel_t **index)
{
	int i = 0;
	static m_collel_t buf[M_CSETSIZE+1]; /* used for return equiv string */

	if (start >= M_CSETSIZE || end >= M_CSETSIZE)
		return (-1);
	while (start <= end)
		buf[i++] = start++;
	*index = buf;
	return (i);
}

/*
 * mks private function.  Given the character index values of a collating
 * element, return all collating elements whose primary weight is the same.
 * Return is a count of # entries in the class; -1 indicates invalid input
 * (i.e. > M_CSETSIZE + # of many-to-one mappings).
 * Other return is the third arg, which is a pointer
 * into the equivalence table.
 */
LDEFN int
m_m_collequiv(m_collel_t ch, m_collel_t **index)
{
	static m_collel_t buf[1];	/* used for return equiv string */

	if (ch >= M_CSETSIZE)
		return (-1);
	buf[0] = ch;
	*index = buf;
	return (1);
}
/*
 * If n is greater than M_CSETSIZE plus number of collating strings, or
 * less than M_CSETSIZE an
 * error of NULL is returned.  Otherwise a pointer into the collating
 * string table is returned, this pointer is good until a setlocale
 * destroys it.
 * We don't bother to build a valid string for ordinary chars, real routines
 * will be checking themselves and only call us for a real collating string.
 * For speed, its quite possible we may provide a non-error checking version
 * of this routine as a #define some day.
 */
LDEFN char *
m_m_colltostr(m_collel_t n)
{
	return (NULL);
}


/*
 * mks private function.  Return all collating elements in collating order.
 * Return:
 *   - a count of # entries in the range
 *     (i.e. M_CSETSIZE + # of many-to-one mappings)
 *   - set 'index' to point to collation table
 */
LDEFN int
m_m_collorder(m_collel_t **index)
{
	int i;
	static m_collel_t buf[M_CSETSIZE]; /* used for return equiv string */

	for (i = 0; i < M_CSETSIZE; i++)
		buf[i] = i;
	*index = buf;
	return (M_CSETSIZE);
}
