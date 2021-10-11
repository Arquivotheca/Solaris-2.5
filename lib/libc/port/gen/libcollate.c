/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */
#ident	"@(#)libcollate.c	1.6	95/04/10 SMI"

#include "_libcollate.h"
#include <limits.h>

#define	ALLOC_UNIT	50
#define	MAXLOCALENAME 14
#define	MAXIGNORE	5

#define	LEVEL_DELIM	1	/* Weight level delimiter */
#define	POS_DELIM	2	/* position delimiter */
#define	IGN_VAL		3	/* If POSITION is specified, this is */
				/*  used as its weight value. */

#ifndef DEBUG
#define	GET_LC_COLLATE	(char *)setlocale(LC_COLLATE, NULL)
#else
#define	DEBUG_ENV	"debug"
#define	GET_LC_COLLATE	(char *)d_setlocale(NULL)
#endif

extern int	strcmp(const char *, const char *);
extern char *	strcpy(char *, const char *);
extern char *	strncpy(char *, const char *, size_t);

_coll_info *_load_coll_(int *);

int _xpg4_strcoll_(char *, char *, _coll_info *, int *);
int _xpg4_strxfrm_(int, char *, char *, size_t, _coll_info *, int *);

int _get_r_weight_mbs(char *, unsigned int *);
int _get_r_weight_wcs(wchar_t *, unsigned int *);

static int _coll_strcoll(char *, char *, int, _coll_info *, int *);
static unsigned int *_coll_strxfrm(char *, int *, int, _coll_info *, int *);
static unsigned char *_coll_convert(unsigned int *, int, int *);
static int _coll_reverse(char *, char *, int);
static int _coll_getmbchar(char *, char *, int);
order * _coll_get_order(encoded_val *, order *);
int _get_coll_elm(encoded_val *, char *, collating_element *, int);
static one_to_many * _coll_get_target(encoded_val *, one_to_many *, int);
static char * _coll_expand(char *, one_to_many *, int);
static unsigned int _get_weight(order *, int, encoded_val *, int *);

static _coll_info * _setup_coll_info(char *);
static char *_coll_mapin(int, unsigned int *);
static void _coll_set_start(start_points *, header *);

static void bytescopy(unsigned char *, unsigned char *, int);
static int bytescmp(unsigned char *, unsigned char *, int len);
static int cmp_encoded(encoded_val *, encoded_val *);
static void copy_encoded(encoded_val *, encoded_val *);
int _get_encoded_value(unsigned int *, encoded_val *);

static void collation_hash_table_init(_coll_info *);
static void collation_hash_table_fini(_coll_info *);
static unsigned int IntTabFindCharbyRank(void *, unsigned int);
static unsigned int IntTabFindRankbyChar(void *, unsigned int);

/*
 * XPG4 collation support routines.
 */
_coll_info *
_load_coll_(int *ret)
{
	char strcoll_name[BUFSIZ];
	char *cur_locale;
	static char last_locale[MAXLOCALENAME+1] = {0};
	static _coll_info *ci = NULL;
	static int last_ret;
	extern int _collate_category_changed;

#ifdef DBGG
printf("_load_coll: _collate_category_changed = %d\n",
	_collate_category_changed);
#endif
	if (_collate_category_changed == 0) {
		/*
		 * This means that LC_COLLATE has not
		 * changed since last time this routine
		 * was called.
		 */
		*ret = last_ret;
		return (ci);
	}

	_collate_category_changed = 0;
	cur_locale = GET_LC_COLLATE;
	if (strcmp(last_locale, cur_locale) == 0) {
		if (strcmp(cur_locale, (const char *)"C") == 0) {
			last_ret = *ret = COLL_USE_C;
			return (NULL);
		} else {
			last_ret = *ret = COLL_USE_LOCALE;
			return (ci);
		}
	}

	/* Locale has been changed -or- first time initialization. */

	if (ci != NULL) {
		/*
		 * Unmap the previous one.
		 */
		collation_hash_table_fini(ci);
		munmap(ci->u.mapin, ci->size);
		close(ci->fd);
		free(ci);
		ci = NULL;
	}

	if (strcmp(cur_locale, (const char *)"C") == 0) {
		last_ret = *ret = COLL_USE_C;
		goto success;
	}

	/*
	 * Otherwise mapin collation table.
	 */
#ifndef DEBUG
	sprintf(strcoll_name,
		"/usr/lib/locale/%s/LC_COLLATE/CollTable", cur_locale);
#else
	{
	char *c;
	char x[100];
	c = getenv(DEBUG_ENV);
	if (c == NULL)
		strcpy(x, "CollTable");
	else
		strcpy(x, c);
	strcpy(strcoll_name, x);
	}
#endif
	ci = _setup_coll_info(strcoll_name);
	if (ci == NULL) {
		last_ret = *ret = COLL_USE_C;
		fprintf(stderr,
			"loading %s failed. Using C locale,,,\n", strcoll_name);
		goto success;
	}
	last_ret = *ret = COLL_USE_LOCALE;
	collation_hash_table_init(ci);

success:
	strcpy(last_locale, cur_locale);
	return (ci);
} /* _load_coll_ */


/*
 * strcoll and strxfrm for XPG4
 */
int
_xpg4_strcoll_(char *s1, char *s2, _coll_info *ci, int *err)
{
	int level = 0;
	int ret;
	*err = 0;

	while (level < ci->u.hp->weight_level) {
		ret = _coll_strcoll(s1, s2, level, ci, err);
		if (*err != 0)
			/*
			 * This is an error return.
			 */
			return (0);
		if (ret != 0)
			return (ret);
		++level;
	}
	return (0);
}

int
_xpg4_strxfrm_(int flag, char *t, char *src, size_t n, _coll_info *ci, int *e)
{
	unsigned int *w = NULL;
	unsigned int *ww;
	unsigned int *tw;
	unsigned char *to;
	unsigned char *fr;
	int t_len = 0;
	int len;
	int l = 0;

	*e = 0;
	while (l < ci->u.hp->weight_level) {
		tw = _coll_strxfrm(src, &len, l, ci, e);
		if (tw == NULL) {
			if (w != NULL)
				free(w);
			return (-1);
		}
		if (len != 0) {
			int size;
			size = (t_len + len)*sizeof (unsigned int);
			ww = (unsigned int *)realloc(w, size);
			if (ww == NULL) {
				free(tw);
				if (w != NULL)
					free(w);
				return (-1);
			}
			w = ww;
			to = (unsigned char *)&w[t_len];
			fr = (unsigned char *)tw;
			bytescopy(to, fr, (len)*sizeof (unsigned int));
			t_len += len;
		}
		free(tw);
		++l;
	}

	/*
	 * IF (flag == T_STRXFRM)
	 *  	This routine is called from strxfrm(). The weight list has
	 * 	to be massaged so it can be used as an argument to strcmp().
	 *	(The string can not contain NULL bytes.)
	 * IF (flag == T_WSXFRM)
	 *	This routine is called from wcsxfrm(). The output list has to
	 *	be usable as an argument to wcscmp().
	 *	The internal knowledge used here; I know that wchar_t and
	 *	unisgned int have same size.
	 */
	if (flag == T_STRXFRM) {
		to = _coll_convert(w, t_len, &len);
		if (to == NULL)
			goto out;
		if (t == NULL) {
			free(to);
			goto out;
		}
		if (len < n - 1)
			l = len;
		else
			l = n - 1;
		bytescopy((unsigned char *)t, to, l);
		t[l] = 0;
		free(to);
	out:
		if (w != NULL)
			free(w);
		return (len);
	} else {
		wchar_t *wt = (wchar_t *)t;

		if (wt == NULL)
			goto w_out;
		for (len = 0; len < t_len; len++)
			wt[len] = (wchar_t) w[len];
		wt[len] = 0;
	w_out:
		if (w != NULL)
			free(w);
		return (t_len);
	}
}

/*
 * _strcoll() body
 */
static int
_coll_strcoll(char *s1, char *s2, int l, _coll_info *ci, int *e)
{
	int expanded = 0;
	char *ss1 = s1;
	char *ss2 = s2;
	char *ss1_save;
	char *ss2_save;
	int num;
	int ret = 0;
	char is_position = 0;
	struct ig_pos {
		int ig[MAXIGNORE];
	} ig[2];
	int p1 = 0;
	int p2 = 0;
	int np1 = 0;
	int np2 = 0;

	*e = 0;
	/*
	 * POSITION specified ?
	 */
#ifdef DDEBUG
printf("_coll_strcoll(lev = %d), postion = 0x%x\n", 
	l, ci->u.hp->weight_types[l] & T_POSITION);
#endif
	if (ci->u.hp->weight_types[l] & T_POSITION) {
		is_position = 1;
		for (num = 0; num < MAXIGNORE; num++)
			ig[0].ig[num] = ig[1].ig[num] = 0;
	}

	/*
	 * If there are any one-to-many, expand them here.
	 */
	if ((num = ci->u.hp->num_otm[l]) != 0) {
		ss1 = _coll_expand(s1, ci->_coll_starts.otm_starts[l], num);
		if (ss1 == NULL) {
			*e = 1;
			return (0);
		}
		ss2 = _coll_expand(s2, ci->_coll_starts.otm_starts[l], num);
		if (ss2 == NULL) {
			*e = 2;
			free(ss1);
			return (0);
		}
		expanded = 1;
	}
	/*
	 * Is it backward ?
	 */
	if (ci->u.hp->weight_types[l] & T_BACKWARD) {
		char *s;
		/*
		 * do it for the first string.
		 */
		s = malloc(strlen(ss1)+1);
		if (s == NULL) {
			if (expanded) {
				free(ss1);
				free(ss2);
			}
			*e = 3;
			return (0);
		}
		if (_coll_reverse(s, ss1, strlen(ss1)) == -1) {
			if (expanded) {
				free(ss1);
				free(ss2);
			}
			free(s);
			*e = 4;
			return (0);
		}
		if (expanded)
			free(ss1);
		ss1 = s;

		/*
		 * do it for the second string.
		 */
		s = malloc(strlen(ss2)+1);
		if (s == NULL) {
			if (expanded) {
				free(ss1);
				free(ss2);
			}
			*e = 5;
			return (0);
		}
		if (_coll_reverse(s, ss2, strlen(ss1)) == -1) {
			if (expanded) {
				free(ss1);
				free(ss2);
			}
			free(s);
			*e = 6;
			return (0);
		}
		if (expanded)
			free(ss2);
		ss2 = s;
		expanded = 1;
	}

	/*
	 * Do comparison
	 */
	ss1_save = ss1;
	ss2_save = ss2;
	while (1) {
		int len1;
		encoded_val en1;
		order *o1;
		unsigned int w1, w2;

		/*
		 * get weight for the first string
		 */
		while (*ss1) {
			++p1;
			len1 = _get_coll_elm(&en1, ss1,
				ci->_coll_starts.start_coll,
				ci->u.hp->no_of_coll_elms);
			if (len1 == -1) {
				*e = 7;
				goto out;
			}
			ss1 += len1;
			o1 = _coll_get_order(&en1,
				ci->_coll_starts.start_order);
			if (o1 == NULL) {
				*e = 8;
				goto out;
			}
			w1 = _get_weight(o1, l, &en1,  e);
			if (*e != 0)
				goto out;
#ifdef DDEBUG
printf("ss1, weight = %d\n", w1);
#endif
			if (w1 != 0)
				break;
			if (is_position) { /* It was IGNORE */
				if (np1 < MAXIGNORE)
					ig[0].ig[np1++] = p1;
			}
		}

		/*
		 * get weight for the second string
		 */
		while (*ss2) {
			p2++;
			len1 = _get_coll_elm(&en1, ss2,
						ci->_coll_starts.start_coll,
						ci->u.hp->no_of_coll_elms);
			if (len1 == -1) {
				*e = 9;
				goto out;
			}
			ss2 += len1;
			o1 = _coll_get_order(&en1,
				ci->_coll_starts.start_order);
			if (o1 == NULL) {
				*e = 10;
				goto out;
			}
			w2 = _get_weight(o1, l, &en1,  e);
			if (*e != 0)
				goto out;
			if (w2 != 0)
				break;
			if (is_position) { /* It was IGNORE */
				if (np2 < MAXIGNORE)
					ig[1].ig[np2++] = p2;
			}
		}
#ifdef DDDEBUG
fprintf(stderr, "w1=%d, w2=%d\n", w1, w2);
#endif
		if (w1 != w2) {
			ret = w1 - w2;
			break;
		}
		if (*ss1 == 0) {
			if (*ss2 != 0) {
				/*
				 * Check the rest of ss2 string and set
				 * ig structre as needed.
				 */
				while (*ss2) {
					p2++;
					len1 = _get_coll_elm(&en1, ss2,
						ci->_coll_starts.start_coll,
						ci->u.hp->no_of_coll_elms);
					if (len1 == -1) {
						*e = 9+100;
						goto out;
					}
					ss2 += len1;
					o1 = _coll_get_order(&en1,
						ci->_coll_starts.start_order);
					if (o1 == NULL) {
						*e = 10+100;
						goto out;
					}
					w2 = _get_weight(o1, l, &en1,  e);
					if (*e != 0)
						goto out;
					if (w2 != 0) {
						/*
						 * Ok, the string has other
						 * collating element with an
						 * weight.
						 */
						ret = -1;
						break;
					}
					if (is_position) { /* It was IGNORE */
						if (np2 < MAXIGNORE)
							ig[1].ig[np2++] = p2;
					}
				}
			}
			break;
		}
		if (*ss2 == 0) {
			/*
			 * Check the rest of ss1 string and set
			 * ig structre as needed.
			 */
			while (*ss1) {
				p1++;
				len1 = _get_coll_elm(&en1, ss1,
					ci->_coll_starts.start_coll,
					ci->u.hp->no_of_coll_elms);
				if (len1 == -1) {
					*e = 9+200;
					goto out;
				}
				ss1 += len1;
				o1 = _coll_get_order(&en1,
					ci->_coll_starts.start_order);
				if (o1 == NULL) {
					*e = 10+200;
					goto out;
				}
				w1 = _get_weight(o1, l, &en1,  e);
				if (*e != 0)
					goto out;
				if (w1 != 0) {
					/*
					 * Ok, the string has other
					 * collating element with an
					 * weight.
					 */
					ret = 1;
					break;
				}
				if (is_position) { /* It was IGNORE */
					if (np1 < MAXIGNORE)
						ig[0].ig[np1++] = p1;
				}
			}
			break;
		}
	}
	/*
	 * Check Position stuff
	 */
#ifdef DDDEBUG
	printf("IS_POSITION = %d, ret = %d\n", is_position, ret);
	if (is_position && ret == 0) {
		int i;
		printf("\t");
		for (i = 0; i < MAXIGNORE; i++) {
			printf("(%d, %d), ", ig[0].ig[i],
					ig[1].ig[i]);
		}
		printf("\n");
	}
#endif
	if (is_position && ret == 0) {
		int i;
		for (i = 0; i < MAXIGNORE; i++) {
			if (ig[0].ig[i] != ig[1].ig[i]) {
				if (ig[0].ig[i] == 0) {
					ret = 1;
				} else if (ig[1].ig[i] == 0) {
					ret = -1;
				} else {
					ret = ig[0].ig[i] -
						ig[1].ig[i];
				}
				break;
			}
		}

	}
	/*
	 * return
	 */
	out:
	if (expanded) {
		free(ss1_save);
		free(ss2_save);
	}
	return (ret);
}

/*
 * _strxfrm() body
 */
static unsigned int *
_coll_strxfrm(char *src, int *len, int l, _coll_info *ci, int *e)
{
	int expanded = 0;
	char *ss1 = src;
	char *ss1_save;
	int num;
	unsigned int *ww = NULL;
	int cnt = 0;
	int num_alloced = 0;
	int is_position = 0;

	unsigned int *ww2 = NULL;
	int cnt2 = 0;
	int num_alloced2 = 0;

	/*
	 * Check if this level has POSITION specified or not.
	 */
	if (ci->u.hp->weight_types[l] & T_POSITION)
		is_position = 1;

	*e = 0;
	ww = (unsigned int *)malloc(ALLOC_UNIT*sizeof (unsigned int));
	if (ww == NULL) {
		*e = 1;
		return (NULL);
	}
	num_alloced += ALLOC_UNIT;

	/*
	 * If POSITIONed, allocate memory for parallel array.
	 */
	if (is_position) {
		ww2 = (unsigned int *)malloc(ALLOC_UNIT*sizeof (unsigned int));
		if (ww2 == NULL) {
			*e = 1;
			free(ww);
			return (NULL);
		}
	}

	/*
	 * If there are any one-to-many, expand them here.
	 */
	if ((num = ci->u.hp->num_otm[l]) != 0) {
		ss1 = _coll_expand(src, ci->_coll_starts.otm_starts[l], num);
		if (ss1 == NULL) {
			*e = 10;
			if (ww != NULL)
				free(ww);
			if (ww2 != NULL)
				free(ww2);
			return (NULL);
		}
		expanded = 1;
	}
	/*
	 * Is it backward ?
	 */
	if (ci->u.hp->weight_types[l] & T_BACKWARD) {
		char *s;
		/*
		 * do it for the first string.
		 */
		s = malloc(strlen(ss1)+1);
		if (s == NULL) {
			if (expanded) {
				free(ss1);
			}
			*e = 11;
			return (NULL);
		}
		if (_coll_reverse(s, ss1, strlen(ss1)) == -1) {
			if (expanded) {
				free(ss1);
			}
			free(s);
			*e = 12;
			if (ww != NULL)
				free(ww);
			if (ww2 != NULL)
				free(ww2);
			return (NULL);
		}
		if (expanded)
			free(ss1);
		expanded++;
		ss1 = s;
	}

	/*
	 * Set up weight list
	 */
	ss1_save = ss1;
	while (*ss1) {
		int len1;
		encoded_val en1;
		order *o1;
		unsigned int w1;

		len1 = _get_coll_elm(&en1, ss1,
			ci->_coll_starts.start_coll,
			ci->u.hp->no_of_coll_elms);
		if (len1 == -1) {
			*e = 20;
			if (expanded)
				free(ss1_save);
			if (ww != NULL)
				free(ww);
			if (ww2 != NULL)
				free(ww2);
			return (NULL);
		}
		ss1 += len1;
		o1 = _coll_get_order(&en1,
			ci->_coll_starts.start_order);
		if (o1 == NULL) {
			*e = 21;
			if (expanded)
				free(ss1_save);
			if (ww != NULL)
				free(ww);
			if (ww2 != NULL)
				free(ww2);
			return (NULL);
		}
		w1 = _get_weight(o1, l, &en1,  e);
		if (*e != 0) {
			if (expanded)
				free(ss1_save);
			if (ww != NULL)
				free(ww);
			if (ww2 != NULL)
				free(ww2);
			return (NULL);
		}

		/*
		 * Fill in weights lists.
		 *	If POSITION is specified, fill in the 
		 *	paralle array first.
		 */
		if (is_position) {
			/*
			 * Fill in primary array
			 */
			if (cnt2+1  > num_alloced2) {
				int size;
				unsigned int *w;
				size = (num_alloced2+ALLOC_UNIT)*
					sizeof (unsigned int);
				w = (unsigned int *) realloc(ww2, size);
				if (w == NULL) {
					*e = 22;
					free(ww);
					free(ww2);
					if (expanded)
						free(ss1_save);
					return (NULL);
				}
				num_alloced2 += ALLOC_UNIT;
				ww2 = w;
			}
			if (w1 != 0)
				ww2[cnt2++] = w1;
			else
				ww2[cnt2++] = IGN_VAL;
		}
		if (w1 != 0) {
			/*
			 * Fill in primary array
			 */
			if (cnt+1  > num_alloced) {
				int size;
				unsigned int *w;
				size = (num_alloced+ALLOC_UNIT)*
					sizeof (unsigned int);
				w = (unsigned int *) realloc(ww, size);
				if (w == NULL) {
					*e = 22;
					if (ww2 != NULL)
						free(ww2);
					free(ww);
					if (expanded)
						free(ss1_save);
					return (NULL);
				}
				num_alloced += ALLOC_UNIT;
				ww = w;
			}
			ww[cnt++] = w1;
		}
	}

	if (is_position == 0)
		ww[cnt++] = LEVEL_DELIM;
	else {
		/*
		 * Now, concatenamte ww and ww2.
		 */
		unsigned int *w;
		int i;
		ww[cnt++] = POS_DELIM;
		w = (unsigned int *) malloc((cnt + cnt2 + 1) *
					sizeof (unsigned int));
		if (w == NULL) {
			*e = 23;
			free(ww);
			free(ww2);
			if (expanded)
				free(ss1_save);
			return (NULL);
		}
		for (i = 0; i < cnt; i++)
			w[i] = ww[i];
		free(ww);
		ww = w;
		for (i = 0; i < cnt2; i++)
			ww[cnt+i] = ww2[i];
		cnt += cnt2;
		free(ww2);
		ww[cnt++] = LEVEL_DELIM;
	}
	/*
	 * return
	 */
	if (expanded)
		free(ss1_save);
	*len = cnt;
	return (ww);
}

static unsigned char *
_coll_convert(unsigned int *s, int len, int *l)
{
	unsigned char *p;
	unsigned char *q;
	unsigned char *xx;
	unsigned char lower;
	int i;
#ifdef DDDEBUG
	{
	int i = 0;
	unsigned int *ss = s;
	printf("DUMPING weights\n");
	for (i = 0; i < len; i++)
		printf("%d,", *ss++);
	printf("\n");
	}
#endif
	xx = (unsigned char *)s;
	*l = 2*len*sizeof (unsigned int);
	q = p = (unsigned char *)malloc(*l);
	if (p == NULL)
		return (NULL);
	for (i = 0; i < *l; i++) {
		if (i%2 == 0)
			lower = (*xx >> 4) & 0x0f;
		else
			lower = *xx++ & 0x0f;

		*p++ = 0x80 | lower;
	}
	return (q);
}

/*
 * Collation common functions
 *

/*
 * reverse string
 */
static int
_coll_reverse(char *to, char *from, int len)
{
	int mlen;

	to[len] = 0;
	while (*from != 0) {
		mlen = mblen(from, MAX_CHAR_LEN);
		if (mlen == -1)
			return (-1);
		if ((len -= mlen) < 0)
			return (-1);
		bytescopy((unsigned char *)&to[len],
				(unsigned char *)from, mlen);
		from += mlen;
	}
	return (0);
}

/*
 * get a multibyte character
 */
static int
_coll_getmbchar(char *to, char *from, int maxlen)
{
	int len;
	len = mblen(from, maxlen);
	if (len == -1)
		return (-1);
	bytescopy((unsigned char *)to, (unsigned char *)from, len);
	return (len);
}

/*
 * get collating order structure
 */
order *
_coll_get_order(encoded_val *en, order *sp)
{
	order *p = sp;
	order *undef = (order *)NULL;
	order *prev;
	order *next;
	int diff;
	while (p->type != T_NULL) {
		if ((diff = cmp_encoded(en, &p->encoded_val)) == 0)
			return (p);
		else if (p->type == T_ELLIPSIS) {
			if ((p+1)->type == T_NULL) {
				/* ellipsis is the last order */
				if (p == sp)
					return ((order *)NULL);
				prev = p-1;
				if (cmp_encoded(en, &prev->encoded_val) > 0)
					return (p);
			} else if (p == sp) { 	/* the ellipsis is the first */
				next = p+1;
				if (cmp_encoded(en, &next->encoded_val) < 0)
					return (p);
			} else {
				prev = p-1;
				next = p+1;
				if ((cmp_encoded(en, &prev->encoded_val) > 0)&&
				    (cmp_encoded(en, &next->encoded_val) < 0))
					return (p);
			}
		} else if (p->type == T_UNDEFINED)
			undef = p;
		++p;
	}
	return (undef);
}

/*
 * get an collating unit
 */
int
_get_coll_elm(encoded_val *en, char *s, collating_element *e, int num)
{
	int len;

	while (num--) {
		if (bytescmp((unsigned char *)s, e->encoded_val.bytes,
				e->encoded_val.length) == 0) {
			copy_encoded(en, &e->encoded_val);
			return (en->length);
		}
		++e;
	}
	len = mblen(s, MAX_CHAR_LEN);
	if (len == -1)
		return (-1);
	en->length = len;
	bytescopy(en->bytes, (unsigned char *)s, len);
	return (len);
}

/*
 *
 */
static one_to_many *
_coll_get_target(encoded_val *source, one_to_many *otm, int num)
{
	while (num-- > 0) {
		if (cmp_encoded(source, &otm->source) == 0)
			return (otm);
		++otm;
	}
	return ((one_to_many *)NULL);
}

/*
 * expand one_to_many mapping
 */
static char *
_coll_expand(char *s, one_to_many *otm, int l)
{
	char *new;
	char *limit;
	int alloc_len;
	int bcopied = 0;
	encoded_val mbchar;
	one_to_many *expanded;
	unsigned char *source;
	int length;

	alloc_len = strlen(s) + 1;
	new = malloc(alloc_len);
	if (new == NULL)
		return (NULL);
	limit = s + alloc_len;

	while (*s) {
		mbchar.length = _coll_getmbchar((char *)&mbchar.bytes, s,
						MAX_CHAR_LEN);
		if (mbchar.length == -1) {
			free(new);
			return (NULL);
		}
		expanded = _coll_get_target(&mbchar, otm, l);
		if (expanded == (one_to_many *)NULL) {
			source = mbchar.bytes;
			length = mbchar.length;
		} else {
			source = expanded->target.bytes;
			length = expanded->target.length;
		}

		if (bcopied + mbchar.length + 1 > alloc_len) {
			char *tmp;
			tmp = realloc(new, ALLOC_UNIT);
			if (tmp == NULL) {
				free(new);
				return (NULL);
			}
			new = tmp;
		}
		bytescopy((unsigned char *)&new[bcopied], source, length);
		s += mbchar.length;
		if (s > limit) {
#ifdef DDEBUG
printf("OVERFLOW IN _coll_expand\n");
#endif
			free(new);
			return (NULL);
		}
		bcopied += length;
	}
	new[bcopied] = 0;
	return (new);
}

/*
 * get weight
 */
static unsigned int
_get_weight(order *o, int l, encoded_val *en, int *e)
{
	unsigned int ret = 0;

	switch (o->weights[l].type) {
	case WT_RELATIVE:
		ret = o->weights[l].u.weight;
		break;
	case WT_ONE_TO_MANY:
		/*
		 * should not happen ? user error
		 */
		ret = o->r_weight;
		break;
	case WT_ELLIPSIS:
		if (o->encoded_val.length == 0) {
			/*
			 * The first element
			 */
			(void) _get_encoded_value(&ret, en);
		} else {
			unsigned int v1, v2;
			(void) _get_encoded_value(&v1, &(o-1)->encoded_val);
			(void) _get_encoded_value(&v2, en);
			ret = v2-v1 + (o-1)->r_weight;

		}
#ifdef DDEBUG
printf("_get_weight: ELLIPSIS, val = %d\n", ret);
#endif
		break;
	case WT_IGNORE:
		ret = 0;
		break;
	default:
		*e = 100;
		break;
	}
	return (ret);
}

/*
 * Setlocale() supplements
 */

static _coll_info *
_setup_coll_info(char *name)
{
	int fd;
	_coll_info *ci;
	unsigned int size;

	if ((fd = open(name, O_RDONLY)) == -1) {
#ifdef DDEBUG
		printf("_setup_coll_info: (%s)open failed.\n", name);
#endif
		return ((_coll_info *)NULL);
	}
	ci = (_coll_info *)malloc(sizeof (_coll_info));
	if (ci == (_coll_info *)NULL) {
#ifdef DDEBUG
		printf("dummy_setlocale: malloc failed.\n");
#endif
		close(fd);
		return ((_coll_info *)NULL);
	}
	if ((ci->u.mapin = _coll_mapin(fd, &size)) == (char *)-1) {
#ifdef DDEBUG
		printf("dummy_setlocale: _coll_mapin failed.\n");
#endif
		close(fd);
		free(ci);
		return ((_coll_info *)NULL);
	}
	ci->fd = fd;
	ci->size = size;
	_coll_set_start(&ci->_coll_starts, ci->u.hp);
	return (ci);
}

/*
 * Map in collation table.
 */
static char *
_coll_mapin(int fd, unsigned int *s)
{
	struct stat stat;
	char *mapin;

	if (fstat(fd, &stat) == -1)
		return ((char *)-1);

	*s = stat.st_size;
	mapin = mmap(0, stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (mapin == (char *)-1)
		return ((char *)-1);

	return (mapin);
}

/*
 * Set Up header information.
 */
static void
_coll_set_start(start_points *sp, header *hp)
{
	char *p;
	char *q;
	int sum = 0;
	int i;

	p = (char *)hp + sizeof (header);
	sp->start_otm = (one_to_many *)p;
	for (i = 0; i < hp->weight_level; i++) {
		if (hp->num_otm[i] == 0) {
			sp->otm_starts[i] = 0;
			sp->otm_starts[i] = (one_to_many *)0;
		} else {
			q = (char *)sp->start_otm +
			    sizeof (one_to_many)*sum;
			sp->otm_starts[i] = (one_to_many *)q;
			sum += hp->num_otm[i];
		}
	}
	p = (char *)sp->start_otm + sizeof (one_to_many)*sum;
	sp->start_coll = (collating_element *)p;
	p = (char *)sp->start_coll +
		sizeof (collating_element)*hp->no_of_coll_elms;
	sp->start_order = (order *)p;
}

/*
 * Supplementary functions
 */

/*
 * bytes copy
 */
static void
bytescopy(unsigned char *to, unsigned char *from, int len)
{
	while (len-- > 0)
		*to++ = *from++;
}

/*
 * bytes compare
 */
static int
bytescmp(unsigned char *s1, unsigned char *s2, int len)
{
	while (len--) {
		if (*s1 != *s2)
			return (*s1-*s2);
		s1++;
		s2++;
	}
	return (0);
}

/*
 * encoded_val operations
 */
static int
cmp_encoded(encoded_val *e1, encoded_val *e2)
{
	int i = 0;

	if (e1->length != e2->length)
		return (e1->length - e2->length);
	while (i < e1->length) {
		if (e1->bytes[i] != e2->bytes[i])
			return (e1->bytes[i] - e2->bytes[i]);
		i++;
	}
	return (0);
}

static void
copy_encoded(encoded_val *to, encoded_val *from)
{
	to->length = from->length;
	bytescopy(to->bytes, from->bytes, to->length);
}

int
_get_encoded_value(unsigned int *i_val, encoded_val *en)
{
	int length = 0;

	*i_val = 0;
	while (length < en->length) {
		*i_val = (*i_val << 8) | en->bytes[length];
		length++;
	}
	return (en->length);
}

/*
 *
 * Routines to support look-up operation on relocatable hashed heap
 * of wide-char<=> rank data
 *
 */
static void
collation_hash_table_init(_coll_info * ptr)
{
	int fd;
	char buffer[255];
	struct stat buf;
	void * table;

	ptr->hash_ptr = NULL;
	ptr->hash_table_size = 0;

	sprintf(buffer, "/usr/lib/locale/%s/LC_COLLATE/CollTable.hash",
		(char *) setlocale(LC_COLLATE, NULL));

	if ((fd = open(buffer, O_RDONLY)) == -1) {
		return;
	}

	if (fstat(fd, & buf) < 0) {
		close(fd);
		return;
	}

	if ((table = mmap(NULL, buf.st_size,
		PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
		close(fd);
		table = 0;
		return;
	}

	ptr->hash_ptr = table;
	ptr->hash_table_size = buf.st_size;

	close(fd);
}

static void collation_hash_table_fini(_coll_info * ptr)
{
	if (ptr->hash_ptr) {
		munmap(ptr->hash_ptr, ptr->hash_table_size);
	}
}

static unsigned int
IntTabFindCharbyRank(void * inttab, unsigned int rank)
{
	register const inttab_t * ptr = (const inttab_t *) inttab;
	register int i;
	
	i = ptr[rank % ptr->key + 1].first_rank;
	while(i && rank != ptr[i].rank)
		i = ptr[i].next_rank;
	return(i?(ptr[i].key):-1);
}

static unsigned int
IntTabFindRankbyChar(void * inttab, unsigned int key)
{
	register const inttab_t * ptr = (const inttab_t *) inttab;
	register int i;
	i = ptr[key % ptr->key + 1].first_key;
	while(i && key != ptr[i].key)
		i = ptr[i].next_key;
	return(i?(ptr[i].rank):-1);
}

wchar_t
_cetowc(_coll_info * ptr, int mc)
{
	void * table;

	if(ptr==NULL)
	    return(mc);

	if((table=ptr->hash_ptr) && mc) 
		return( IntTabFindCharbyRank(table, (unsigned int)mc));

	return(-1);
}
			     
/*
* _wctoce()   - convert a wide character to a
* collating element
*/

int
_wctoce(_coll_info * ptr, wchar_t wc)
{	
	void * table;

	if(ptr==NULL)
	    return(wc);

	if((table=ptr->hash_ptr) && wc) 
	   return( IntTabFindRankbyChar(table, (unsigned int)wc));
	   
	return(-1);
}	

/*
 * DEBBUGING AIDS
 */
_coll_info *
dummy_setlocale(char *name)
{
	return (_setup_coll_info(name));
}

/*
 * Given a pointer to wchar_t or character string,
 * these functions retorun the relative weights
 * assigned to the first collating element in
 * the specified wchar_t or character string.
 */

/*
 * This function is slow.
 */
int
_get_r_weight_wcs(wchar_t *wcs, unsigned int *r_val)
{
	char mbuf[BUFSIZ+1];
	int len;
	char *mp = mbuf;
	int wlen = 0;
	int mlen = 0;
	int one_len = 0;

	if (wcs[0] == 0)
		return (0);

	len = wcstombs(mbuf, wcs, BUFSIZ);
	if (len == -1)
		return (-1);
	len = _get_r_weight_mbs(mbuf, r_val);

	if (len < 0)
		return (len);

	/*
	 * Calculate the number of wide characters
	 * comprising the collating element.
	 */
	while (mlen < len) {
		one_len = mblen(mp, MB_LEN_MAX);
		if (one_len < 0)
			return (-1);
		mp += one_len;
		mlen += one_len;
		wlen++;
	}
	return (wlen);
}

/*
 * Returns the number of bytes comprise the
 * first collating element.
 *	Negative numbers for errors.
 *
 * Set the relative weight to the second
 * argument.
 *
 *
 */
int
_get_r_weight_mbs(char *mbs, unsigned int *r_val)
{
	order *o;
	int ret;
	int e = 0;
	_coll_info *ci;
	encoded_val en;
	int len;
	unsigned int v1, v2;

	/*
	 * If NULL character is passed,
	 * then return 0.
	 */
	if (*mbs == 0)
		return (0);
	/*
	 * Load Collation Table
	 */
	ci = _load_coll_(&ret);
	if (ret == COLL_USE_C) {
		(*r_val) = MIN_WEIGHT + (unsigned int)(*mbs);
		return (1);
	}

	/*
	 * Get the collating element.
	 */
	len = _get_coll_elm(&en, mbs,
		ci->_coll_starts.start_coll,
		ci->u.hp->no_of_coll_elms);
	if (len == -1)
		return (-7);
	ret = len;

	/*
	 * Get order structure
	 */
	o = _coll_get_order(&en,
		ci->_coll_starts.start_order);
	if (o == NULL) {
		return (-1);
	}

	switch (o->type) {
	case T_UNDEFINED:
		(*r_val) = o->r_weight;
		break;
	case T_ELLIPSIS:
		if (o->encoded_val.length == 0) {
			_get_encoded_value(&v1, &en);
			(*r_val) = v1;
		} else {
			_get_encoded_value(&v1, &(o-1)->encoded_val);
			_get_encoded_value(&v2, &en);
			(*r_val) = v2-v1+ (o-1)->r_weight;
		}
		break;
	default:
		(*r_val) = o->r_weight;
		break;
	}

	return (ret);
}
