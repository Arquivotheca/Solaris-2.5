/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SEARCH_H
#define	_SEARCH_H

#pragma ident	"@(#)search.h	1.11	93/07/09 SMI"	/* SVr4.0 1.3.1.11 */

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _SIZE_T
#define	_SIZE_T
typedef unsigned	size_t;
#endif

/* HSEARCH(3C) */
typedef enum { FIND, ENTER } ACTION;

#if defined(__EXTENSIONS__) || !defined(_XOPEN_SOURCE)
struct qelem {
	struct qelem	*q_forw;
	struct qelem	*q_back;
};
#endif /* defined(__EXTENSIONS__) || !defined(_XOPEN_SOURCE) */

typedef struct entry { char *key, *data; } ENTRY;

#if defined(__STDC__)
int hcreate(size_t);
void hdestroy(void);
ENTRY *hsearch(ENTRY, ACTION);

#if defined(__EXTENSIONS__) || !defined(_XOPEN_SOURCE)
void insque(struct qelem *, struct qelem *);
void remque(struct qelem *);
#endif /* defined(__EXTENSIONS__) || !defined(_XOPEN_SOURCE) */

#else
int hcreate();
void hdestroy();
ENTRY *hsearch();
void insque();
void remque();
#endif

/* TSEARCH(3C) */
typedef enum { preorder, postorder, endorder, leaf } VISIT;

#if defined(__STDC__)
void *tdelete(const void *, void **, int (*)(const void *, const void *));
void *tfind(const void *, void *const *, int (*)(const void *, const void *));
void *tsearch(const void *, void **, int (*)(const void *, const void *));
void twalk(const void *, void (*)(const void *, VISIT, int));
#else
void *tdelete();
void *tfind();
void *tsearch();
void twalk();
#endif

#if defined(__STDC__)

#if defined(__EXTENSIONS__) || !defined(_XOPEN_SOURCE)
/* BSEARCH(3C) */
void *bsearch(const void *, const void *, size_t, size_t,
	    int (*)(const void *, const void *));
#endif /* defined(__EXTENSIONS__) || !defined(_XOPEN_SOURCE) */

/* LSEARCH(3C) */
void *lfind(const void *, const void *, size_t *, size_t,
	    int (*)(const void *, const void *));
void *lsearch(const void *, void *, size_t *, size_t,
	    int (*)(const void *, const void *));
#else
void *bsearch();
void *lfind();
void *lsearch();
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SEARCH_H */
