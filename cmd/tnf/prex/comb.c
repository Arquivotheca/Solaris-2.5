/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)comb.c 1.16 94/10/11 SMI"

/*
 * Includes
 */

#ifndef DEBUG
#define	NDEBUG	1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>
#include <assert.h>
#include <sys/types.h>

#include "prbutl.h"
#include "dbg.h"


/*
 * Typedefs
 */

typedef struct comb_callinfo {
	unsigned		offset;
	unsigned		shift;	/* shift right <n> bits */
	unsigned		mask;

}			   comb_callinfo_t;

typedef struct comb_calltmpl {
	caddr_t		 entry;
	caddr_t		 down;
	caddr_t		 next;
	caddr_t		 end;

}			   comb_calltmpl_t;

typedef struct comb_key {
	prb_comb_op_t   op;
	caddr_t		 down;
	caddr_t		 next;
	caddr_t		 comb;

}			   comb_key_t;

typedef struct decode_key {
	caddr_t		 addr;
	char		   *name;

}			   decode_key_t;


/*
 * Globals
 */

extern comb_callinfo_t prb_callinfo;

extern void	 prb_chain_entry(void);
extern void	 prb_chain_down(void);
extern void	 prb_chain_next(void);
extern void	 prb_chain_end(void);

static comb_calltmpl_t calltmpl[PRB_COMB_COUNT] = {
{
		(caddr_t) prb_chain_entry,
		(caddr_t) prb_chain_down,
		(caddr_t) prb_chain_next,
		(caddr_t) prb_chain_end}
};

static void	*g_buildroot = NULL;
static void	*g_decoderoot = NULL;


/*
 * Declarations
 */

static char	*
decode(int procfd, caddr_t addr);
static boolean_t
find(prb_comb_op_t op, caddr_t down, caddr_t next, caddr_t * comb_p);
static prb_status_t
build(int procfd, prb_comb_op_t op, caddr_t down,
	caddr_t next, caddr_t * comb_p);
static prb_status_t
add(prb_comb_op_t op,
	caddr_t down,
	caddr_t next,
	caddr_t comb);
static int
comb_compare(const void *a, const void *b);
static int
decode_compare(const void *v0p, const void *v1p);
static boolean_t
iscomb(int procfd, caddr_t addr, caddr_t * down_p, caddr_t * next_p);
static char	*
findname(int procfd, caddr_t addr);


/* ---------------------------------------------------------------- */
/* ----------------------- Public Functions ----------------------- */
/* ---------------------------------------------------------------- */

/*
 * prb_comb_build() - finds (or builds) a combination satisfing the op, down
 * and next constraints of the caller.
 */

prb_status_t
prb_comb_build(int procfd, prb_comb_op_t op,
	caddr_t down, caddr_t next, caddr_t * comb_p)
{
	prb_status_t	prbstat;

	*comb_p = NULL;

	if (find(op, down, next, comb_p)) {
#ifdef DEBUG
		if (__prb_verbose)
			(void) fprintf(stderr, ": found comb at 0x%x",
				(unsigned) *comb_p);
#endif
		return (PRB_STATUS_OK);
	}
	prbstat = build(procfd, op, down, next, comb_p);

#ifdef DEBUG
	if (__prb_verbose)
		(void) fprintf(stderr, ": built comb at 0x%x",
			(unsigned) *comb_p);
#endif

	return (prbstat);

}				/* end prb_comb_build */


/*
 * prb_comb_decode() - returns a string describing the probe functions NOTE -
 * the string is for reference purposes ONLY, it should not be freed by the
 * client.
 */

prb_status_t
prb_comb_decode(int procfd,
		caddr_t addr,
		char **name_pp)
{

	*name_pp = decode(procfd, addr);

	return (PRB_STATUS_OK);

}				/* end prb_comb_decode */


/* ---------------------------------------------------------------- */
/* ----------------------- Private Functions ---------------------- */
/* ---------------------------------------------------------------- */

static char	*
decode(int procfd, caddr_t addr)
{
	decode_key_t	key;
	decode_key_t   *new_p;
	decode_key_t  **find_pp;
	caddr_t		 down;
	caddr_t		 next;

	/* see if we can find the previously decoded answer */
	key.addr = addr;
	find_pp = (decode_key_t **) tfind(&key, &g_decoderoot, decode_compare);
	if (find_pp) {
#ifdef DEBUG
		if (__prb_verbose >= 2)
			(void) fprintf(stderr, "decode: found %s\n",
				(*find_pp)->name);
#endif
		return ((*find_pp)->name);
	}
	new_p = (decode_key_t *) malloc(sizeof (decode_key_t));
	if (!new_p)
		return (NULL);
	new_p->addr = addr;

	if (iscomb(procfd, addr, &down, &next)) {
		char		   *nextname;
		char		   *thisname;

#ifdef DEBUG
		if (__prb_verbose >= 2)
			(void) fprintf(stderr,
				"decode: iscomb down=0x%x next=0x%x\n",
				down, next);
#endif

		thisname = findname(procfd, down);
		nextname = decode(procfd, next);

		new_p->name = (char *) malloc(strlen(thisname) +
			strlen(nextname) + 2);
		(void) strcpy(new_p->name, thisname);
		(void) strcat(new_p->name, " ");
		(void) strcat(new_p->name, nextname);

		if (thisname)
			free(thisname);
	} else {
		new_p->name = findname(procfd, addr);
	}

#ifdef DEBUG
	if (__prb_verbose >= 2)
		(void) fprintf(stderr, "decode: built %s\n", new_p->name);
#endif

	find_pp = (decode_key_t **) tsearch(new_p,
		&g_decoderoot, decode_compare);
	assert(*find_pp == new_p);

	return (new_p->name);

}				/* end decode */


/*
 * iscomb() - determine whether the pointed to function is a combination.  If
 * it is, return the down and next pointers
 */

static		  boolean_t
iscomb(int procfd, caddr_t addr, caddr_t * down_p, caddr_t * next_p)
{
	int			 type;
	boolean_t	   matched = B_FALSE;

	for (type = 0; type < PRB_COMB_COUNT; type++) {
		size_t		  size;
		char		   *targ_p;
		prb_status_t	prbstat;
		char		   *ptr;
		char		   *tptr;
		caddr_t		 downaddr;
		caddr_t		 nextaddr;

		/* allocate room to copy the target code */
		size = (size_t) (calltmpl[type].end - calltmpl[type].entry);
		targ_p = (char *) malloc(size);
		if (!targ_p)
			return (B_FALSE);

		/* copy code from target */
		prbstat = prb_proc_read(procfd, addr, targ_p, size);
		if (prbstat)
			return (B_FALSE);

		/* loop over all the words */
		tptr = (char *) calltmpl[type].entry;
		for (ptr = targ_p; ptr < (targ_p + size); ptr++, tptr++) {
			int			 downbits;
			int			 nextbits;
		/* LINTED pointer cast may result in improper alignment */
			int			*uptr = (int *) ptr;

			/*
			 * If we are pointing at one of the words that we
			 * patch, * (down or next displ) then read that value
			 * in. * Otherwise make sure the words match.
			 */
			if ((caddr_t) tptr == calltmpl[type].down +
				prb_callinfo.offset) {
				downbits = *uptr;
				downbits &= prb_callinfo.mask;
				downbits <<= prb_callinfo.shift;
				downaddr = (caddr_t) (addr +
					(ptr - targ_p) + downbits);
#if defined(i386)
				downaddr += 4;
				/* intel is relative to *next* instruction */
#endif

				ptr += 3;
				tptr += 3;
			} else if ((caddr_t) tptr == calltmpl[type].next +
				prb_callinfo.offset) {
				nextbits = *uptr;
				nextbits &= prb_callinfo.mask;
				nextbits <<= prb_callinfo.shift;
				nextaddr = (caddr_t) (addr +
					(ptr - targ_p) + nextbits);
#if defined(i386)
				nextaddr += 4;
				/* intel is relative to *next* instruction */
#endif

				ptr += 3;
				tptr += 3;
			} else {
				/* the words better match or we bail */
				if (*ptr != *tptr)
					goto NextComb;
			}
		}

		/* YOWSA! - its a match */
		matched = B_TRUE;

NextComb:
		/* free allocated memory */
		if (targ_p)
			free(targ_p);

		if (matched) {
			*down_p = downaddr;
			*next_p = nextaddr;
			return (B_TRUE);
		}
	}

	return (B_FALSE);

}				/* end iscomb */


/*
 * findname() - find a name for a function.
 */

static char	*
findname(int procfd, caddr_t addr)
{
	char		   *symnames[1];
	caddr_t		 symaddrs[1];

	symaddrs[0] = addr;
	symnames[0] = NULL;
	(void) prb_sym_findname(procfd, 1, symaddrs, symnames);
	if (symnames[0]) {
		/* found an name */

		/*
		 * SPECIAL CASE
		 * If we find "tnf_trace_end" then we should not report it
		 * as this is the "end-cap" function and should be hidden
		 * from the user.  Return a null string instead ...
		 */
		if (strcmp(symnames[0], TRACE_END) == 0)
			return ("");
		else
			return (symnames[0]);
	} else {
		char			buffer[32];

		/* no name found, use the address */
		(void) sprintf(buffer, "func@0x%x", (unsigned) addr);
		return (strdup(buffer));
	}

}				/* end findname */


/*
 * find() - try to find an existing combination that satisfies ...
 */

boolean_t
find(prb_comb_op_t op, caddr_t down, caddr_t next, caddr_t * comb_p)
{
	comb_key_t	  key;
	comb_key_t	**find_pp;

	key.op = op;
	key.down = down;
	key.next = next;
	key.comb = NULL;

	find_pp = (comb_key_t **) tfind(&key, &g_buildroot, comb_compare);
	if (find_pp) {
		*comb_p = (*find_pp)->comb;
		return (B_TRUE);
	} else
		return (B_FALSE);

}				/* end find */


/*
 * add() - adds a combination to combination cache
 */

prb_status_t
add(prb_comb_op_t op,
	caddr_t down,
	caddr_t next,
	caddr_t comb)
{
	comb_key_t	 *new_p;
	/* LINTED set but not used in function */
	comb_key_t	**ret_pp;

	new_p = (comb_key_t *) malloc(sizeof (comb_key_t));
	if (!new_p)
		return (PRB_STATUS_ALLOCFAIL);

	new_p->op = op;
	new_p->down = down;
	new_p->next = next;
	new_p->comb = comb;

	ret_pp = (comb_key_t **) tsearch(new_p, &g_buildroot, comb_compare);
	assert(*ret_pp == new_p);

	return (PRB_STATUS_OK);

}				/* end add */


/*
 * decode_compare() - comparison function used for tree search for
 * combinations
 */

static int
decode_compare(const void *v0p, const void *v1p)
{
	decode_key_t   *k0p = (decode_key_t *) v0p;
	decode_key_t   *k1p = (decode_key_t *) v1p;

	if ((unsigned) k0p->addr > (unsigned) k1p->addr)
		return (-1);
	else if ((unsigned) k0p->addr < (unsigned) k1p->addr)
		return (1);
	else
		return (0);

}				/* end decode_compare */


/*
 * comb_compare() - comparison function used for tree search for combinations
 */

static int
comb_compare(const void *v0p, const void *v1p)
{
	comb_key_t	 *k0p = (comb_key_t *) v0p;
	comb_key_t	 *k1p = (comb_key_t *) v1p;

	if (k0p->op != k1p->op)
		return ((k0p->op < k1p->op) ? -1 : 1);

	if (k0p->down != k1p->down)
		return ((k0p->down < k1p->down) ? -1 : 1);

	if (k0p->next != k1p->next)
		return ((k0p->next < k1p->next) ? -1 : 1);

	return (0);

}				/* end comb_compare */


/*
 * build() - build a composition
 */

prb_status_t
build(int procfd, prb_comb_op_t op, caddr_t down,
	caddr_t next, caddr_t * comb_p)
{
	size_t		  size;
	caddr_t		 addr;
	prb_status_t	prbstat;
	char		   *buffer_p;
	offset_t		offset;
	offset_t		contents;
	unsigned	   *word_p;

#if 0
	(void) fprintf(stderr, "off=0x%x shift=0x%x mask=0x%x size=%d\n",
		prb_callinfo.offset,
		prb_callinfo.shift,
		prb_callinfo.mask,
		calltmpl[op].end - calltmpl[op].entry);
#endif

	*comb_p = NULL;
	size = calltmpl[op].end - calltmpl[op].entry;

	/* allocate memory in the target process */
	prbstat = prb_targmem_alloc(procfd, size, &addr);
	if (prbstat) {
		DBG((void) fprintf(stderr,
			"build: trouble allocating target memory: %s\n",
			prb_status_str(prbstat)));
		goto Error;
	}
	/* allocate a scratch buffer, copy the template into it */
	buffer_p = NULL;
	buffer_p = (void *) malloc(size);
	if (!buffer_p) {
		DBG((void) fprintf(stderr, "build: alloc failed\n"));
		goto Error;
	}
	(void) memcpy(buffer_p, (void *) calltmpl[op].entry, size);

	/* poke the down address */
	offset = calltmpl[op].down - calltmpl[op].entry;
	/*LINTED pointer cast may result in improper alignment*/
	word_p = (unsigned *) (buffer_p + offset + prb_callinfo.offset);
	contents = (unsigned) down - ((unsigned) addr + offset);
#if defined(i386)
	contents -= 5;		/* intel offset is relative to *next* instr */
#endif
#ifdef DEBUG
	if (__prb_verbose >= 3)
		(void) fprintf(stderr,
			"build: down=0x%x contents=0x%x, "
			"word_p=0x%x, offset=%d\n",
			(unsigned) down, (unsigned) contents,
			(unsigned) word_p, offset);
#endif
	*word_p &= ~prb_callinfo.mask;	/* clear the relevant field */
	*word_p |= ((contents >> prb_callinfo.shift) & prb_callinfo.mask);

	/* poke the next address */
	offset = calltmpl[op].next - calltmpl[op].entry;
	/*LINTED pointer cast may result in improper alignment*/
	word_p = (unsigned *) (buffer_p + offset + prb_callinfo.offset);
	contents = (unsigned) next - ((unsigned) addr + offset);
#if defined(i386)
	contents -= 5;		/* intel offset is relative to *next* instr */
#endif
#ifdef DEBUG
	if (__prb_verbose >= 3)
		(void) fprintf(stderr,
			"build: next=0x%x contents=0x%x, "
			"word_p=0x%x, offset=%d\n",
			(unsigned) next, (unsigned) contents,
			(unsigned) word_p, offset);
#endif
	*word_p &= ~prb_callinfo.mask;	/* clear the relevant field */
	*word_p |= ((contents >> prb_callinfo.shift) & prb_callinfo.mask);

	/* copy the combination template into target memory */
	prbstat = prb_proc_write(procfd, addr, buffer_p, size);
	if (prbstat) {
		DBG((void) fprintf(stderr,
			"build: trouble writing combination: %s\n",
			prb_status_str(prbstat)));
		goto Error;
	}
	*comb_p = addr;
	prbstat = add(op, down, next, addr);

Error:
	if (buffer_p)
		free(buffer_p);
	return (prbstat);

}				/* end build */
