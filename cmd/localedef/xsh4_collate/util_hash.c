/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All Rights Reserved
 */
#ident	"@(#)util_hash.c	1.9	95/08/07 SMI"

#include "collate.h"
#include "extern.h"
#include <ctype.h>
#include <limits.h>

#define	C_SIZE	128
#define SZ_TOTAL ((257 + 257) + 7)	/* from lib/libc/port/gen/_locale.h */

static int hash_undefined(order *);
static int hash_coll_element(order *, int, int);
static int hash_add_one(wchar_t *, int, int);
static int hash_ellipsis(order *, int);

static int set_mstring(char *, order *, int, unsigned long long *);
static int set_wstring(wchar_t *, order *, int);
static void ull_to_string(char *, unsigned long long);

static size_t (*mbstowcs_fp)(wchar_t *, const char *, size_t);
static int hash_setup_mbstowcs(char *);

extern char *output_fname;
static char *prev_loc;
extern int l_flag;			/* from main.c */

/*
 * Hash functions
 */
#define	HASH_SIZE	30000
#define	HASH_SUFFIX	".hash"
#define	HASH_UNDEFINED	0xffffffff
#define	LOWER_BYTE	0x00000000000000ff
#define	MAX_COLL_LENGTH	4
#define	MBSTOWCS_ERROR	0xffff0000
static unsigned int hash_undefined_val = HASH_UNDEFINED;
static int counter = 0;
static void *handle;
extern void *CreateIntList(int);
extern int AddToIntList(void *, int, int);
extern int CopyIntList(void *, void *);
extern int SizeIntList(void *);

#define	DEBUG_SUFFIX	".hash_debug"
static FILE *hash_debug_fp = NULL;

/*
 * Initialize hash processing
 */
int
hash_init(char *l)
{
	char *dbg;
	int ret;

	ret = hash_setup_mbstowcs(l);
	if (ret == ERROR)
		return (ERROR);

	/*
	 * Initilize hash information
	 */
	handle = CreateIntList(HASH_SIZE);
	if (handle == NULL) {
		setlocale(LC_CTYPE, prev_loc);
		return (ERROR);
	}

	/*
	 * Open readable hash debugging file.
	 */
	if (debug_flag != 0) {
		dbg = malloc(strlen(output_fname)+
			strlen(DEBUG_SUFFIX)+1);
		if (dbg != NULL) {
			strcpy(dbg, output_fname);
			strcat(dbg, DEBUG_SUFFIX);
			hash_debug_fp = fopen(dbg, "w");
			free(dbg);
		}
	}

	return (0);
}

/*
 * Post process for hashing.
 *	Write out hash table.
 */
int
hash_fin()
{
	void *space;
	int size;
	char *out;
	int fd;
	int ret = 0;
#ifdef DBG
	if (hash_debug_fp != NULL)
		fprintf(hash_debug_fp, "HASH_FIN called:\n");
#endif
	setlocale(LC_CTYPE, prev_loc);

	/*
	 * Create output hash file
	 */
	out = malloc(strlen(output_fname)+strlen(HASH_SUFFIX)+1);
	if (out == NULL)
		return (ERROR);
	strcpy(out, output_fname);
	strcat(out, HASH_SUFFIX);
	fd = creat(out, 0777);
	if (fd == -1) {
		free(out);
		return (ERROR);
	}

	/*
	 * Copy Hash Table
	 */
	space = (void *)malloc(size = SizeIntList(handle));
	if (space == NULL)
		return (ERROR);
	CopyIntList(handle, space);

	/*
	 * Write Hash Table
	 */
	if (write(fd, space, size) != size) {
		ret = ERROR;
	}

	free(out);
	free(space);
	close(fd);
	return (ret);
}

/*
 * For an each order structure,
 * set an hash information.
 */
int
hash_order(order *o)
{
	static int prev_type;
	static unsigned int prev_rval;
	if (o->type == T_UNDEFINED) {
		hash_undefined(o);
		prev_type = T_UNDEFINED;
	} else if (o->type == T_ELLIPSIS) {
		prev_type = T_ELLIPSIS;
	} else {
		hash_coll_element(o, prev_type, (int)prev_rval);
		prev_type = T_CHAR_ENCODED;
	}
	prev_rval = o->r_weight;
	counter++;
	return (0);

}

/*
 * The order structure has
 *	UNDEFINED type.
 * For this version, we just put a dummy key.
 */
static int
hash_undefined(order *o)
{
	static int undefined_counter = 0;

	if (debug_flag != 0) {
		if (hash_debug_fp != NULL)
			fprintf(hash_debug_fp,
				"HASH_UNDEFINED() called R_WEIGHT = %d.\n",
				o->r_weight);
	}
	/*
	 * Should be only one UNDEFINED if there is any.
	 */
	hash_add_one((wchar_t *)&hash_undefined_val, 1, (int)o->r_weight);
	/*
	AddToIntList(handle, (int)hash_undefined_val, (int)o->r_weight);
	 */
	--hash_undefined_val;
	return (undefined_counter++);
}

/*
 * Regular order structure.
 * It has a collating element.
 * If it is an ELLIPSIS type,
 * it will be expanded.
 */
static int
hash_coll_element(order *o, int p_type, int p_val)
{
	wchar_t ws1[C_SIZE+1];
	int cnt;
	int len;

	if (p_type == T_ELLIPSIS) {
		/*
		 * The previou entry was ELLIPSIS.
		 * Add hash entries for them, including
		 * the currnet o itself.
		 *
		 * Also note that if ELLIPSIS is specified at
		 * last, then it will be ignored.
		 */
		cnt = hash_ellipsis(o, p_val);
	} else {
		/*
		 * Add the current o.
		 */
		len = set_wstring(ws1, o, C_SIZE);
		hash_add_one(ws1, len, (int)o->r_weight);
		cnt = 1;
	}
	return (cnt);
}

/*
 * Expand the ... and call
 * 	hash_add_one()
 * for each entry.
 */
static int
hash_ellipsis(order *o, int e_val)
{
	char mbchar[MAX_BYTES+1];
	wchar_t ws1[C_SIZE+1];
	unsigned long long ull = 0;
	int len;
	int r_val = (int)o->r_weight;

	set_mstring(mbchar, o, MAX_BYTES, &ull);
	for (r_val = (int)o->r_weight; r_val >= e_val; r_val--) {
		ull_to_string(mbchar, ull);
		ull--;
		len = (*mbstowcs_fp)(ws1, mbchar, C_SIZE);
		if (len == -1) {
			unsigned int tmp = 0;
			char *p = mbchar;
			unsigned int i = 0;
			while (*p != 0 && i != 4) {
				tmp = tmp << 8;
				tmp = tmp | (unsigned int)(*p);
				i++;
				p++;
			}
			ws1[0] = tmp;
			ws1[1] = 0;
			len = 1;
			if (debug_flag != 0) {
				if (hash_debug_fp != NULL)
					fprintf(hash_debug_fp,
					"MBSTOWCS(set_ellipsis):  failed\n");
			}
		}
		hash_add_one(ws1, len, r_val);
#ifdef DDBG
		if (debug_flag != 0) {
			if (hash_debug_fp != NULL)
				fprintf(hash_debug_fp,
					"\tHASH_ELLIPSIS (%d, %d).\n",
					e_val, r_val);
		}
#endif
	}
}

/*
 * Add a collating element into the hash table.
 */
static int
hash_add_one(wchar_t *ws, int len, int val)
{
	int key = 0;
	int real_length;
	int ret_hash;

	if (len <= MAX_COLL_LENGTH)
		real_length = len;
	else
		real_length = MAX_COLL_LENGTH;
	if (len == 1)
		key = (int)ws[0];
	else {
		int i;
		for (i = 0; i < real_length; i++) {
			key = key << 8;
			key = key | ((int)ws[i] & 0x00ff);
		}
	}

	if (debug_flag != 0) {
		char mb[10];
		int ret;
		int i;

		for (ret = 0; ret < 10; ret++)
			mb[ret] = 0;
		if (len == 1) {
			ret = wctomb(mb, ws[0]);
		}
		if (hash_debug_fp != NULL) {
			fprintf(hash_debug_fp,
			"AddToIntList(HNDL,0x%x,0x%x): Col.Elm.len=%d: ",
			key, val, real_length);
			fprintf(hash_debug_fp, "Multi.Bytes(");
		}
		for (i = 0; i < ret; i++)
			if (hash_debug_fp != NULL)
				fprintf(hash_debug_fp,
				"0x%x,", (unsigned char)mb[i]);
		if (hash_debug_fp != NULL)
			fprintf(hash_debug_fp, ")\n");
	}
	ret_hash = AddToIntList(handle, (int)key, val);
	if (ret_hash != 0) {
		fprintf(stderr, "hash_add_one() returned %d\n", ret_hash);
	}
}

/*
 * Conver the order collating element value
 * into wide character string.
 */
static int
set_wstring(wchar_t *w, order *o, int max)
{
	char mbchar[MAX_BYTES+1];
	int len;
	unsigned int tmp = 0;

	/*
	 * Generate a multibyte string
	 */
	for (len = 0; len < (o->encoded_val).length; len++) {
		mbchar[len] = (o->encoded_val).bytes[len];
		if (len < 4) {
			tmp = tmp << 8;
			tmp = tmp | (unsigned int)mbchar[len];
		}
	}
	mbchar[len] = 0;

	len = (*mbstowcs_fp)(w, mbchar, max);
	if (len == -1) {
		w[0] = MBSTOWCS_ERROR|tmp;
		w[1] = 0;
		len = 1;
#ifdef DBG
		if (hash_debug_fp != NULL)
			fprintf(hash_debug_fp,
				"MBSTOWCS(set_wstring):  failed\n");
#endif
	}
	return (len);
}

/*
 * Conver the order collating element value
 * into multibyte character string.
 */
static int
set_mstring(char *m, order *o, int max, unsigned long long *ull)
{
	int len;
	int limit;
	unsigned long long l = 0;

	if ((o->encoded_val).length > max)
		limit = max;
	else
		limit = (o->encoded_val).length;
	/*
	 * Generate a multibyte string
	 */
	for (len = 0; len < limit; len++) {
		m[len] = (o->encoded_val).bytes[len];
		l = l << 8;
		l |= (o->encoded_val).bytes[len];
	}

	(*ull) = l;
	m[len] = 0;

	return (len);
}

/*
 * Convert unsigned long long to string
 */
static void
ull_to_string(char *s, unsigned long long ull)
{
	int i = 0;
	char tmp_char[C_SIZE];
	unsigned char uc;
	char tmp;
	int j;
	while (ull > 0) {
		uc = ull & LOWER_BYTE;
		tmp_char[i++] = (char) uc;
		ull = ull >> 8;
	}
	tmp_char[i] = 0;
	for (j = 0; j < i; j++) {
		s[j] = tmp_char[(i-1)-j];
	}
	s[j] = 0;
}

/*
 * Set Up
 *	mbstowcs() function
 */
static int
hash_setup_mbstowcs(char *l)
{
	char *ll;
	int fd;
	char filename[PATH_MAX];
	extern  size_t mbstowcs(wchar_t *, const char *, size_t);
	extern unsigned char _ctype[];		/* from ctype.h */
	extern char *output_fname;		/* from main.c */
	extern int l_flag;			/* from main.c */


#ifdef DBG
	if (hash_debug_fp != NULL)
		fprintf(hash_debug_fp,
			"HASH_SETUP_MBSTOWCS called: '%s'\n", l);
#endif

	prev_loc = setlocale(LC_CTYPE, NULL);

	if (l_flag) {		/* using runtime locale */
		/*
	 	 * BUG - for now to have Code Set Independence continue to
	 	 *	 work we leave this code in so it loads mb.so from
	 	 *	 the locale.
	 	 */

		if (setlocale(LC_CTYPE, l) == NULL)
			return (ERROR);
	} else {
		/*
		 * EUC only.
		 * Load the newly created ctype table if there is one.
		 * If not then just use the ctype of the current locale.
		 */

		(void) strncpy(filename, output_fname, PATH_MAX);
		if ((ll = strrchr(filename, '.')) != NULL) {
			*ll = (char) NULL;
			strncat(filename, CHRTBL_TRAIL, PATH_MAX);
		} else {
			fprintf(stderr,
				gettext("%s: can't determine location of .chrtbl table\n"),
				COMMAND_NAME);
			return(ERROR);
		}

		/*
		 * There may not be a LC_CTYPE section which is ok.
		 * Therefore, we'll use the ctype table of the current locale.
		 */

		if ((fd = open(filename, O_RDONLY)) != -1) {
			if (read(fd, _ctype, SZ_TOTAL) != SZ_TOTAL) {
				perror(COMMAND_NAME);
				(void) close(fd);
				return(ERROR);
			}
			(void) close(fd);
		}
	}

#ifdef DBG
	if (hash_debug_fp != NULL) {
		fprintf(hash_debug_fp,
			"PREVIOUS locale = %s\n", prev_loc);
		fprintf(hash_debug_fp,
			"CURRENT         = %s\n", setlocale(LC_CTYPE, 0));
	}
#endif
	mbstowcs_fp = mbstowcs;
	return (0);
}
