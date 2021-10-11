/*
 * Copyright (c) 1991-1994, Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)sparc_util.s	1.6	94/11/29 SMI"

#include <sys/asm_linkage.h>

#if defined(lint)

#include "cbootblk.h"

/*ARGSUSED*/
void
bcopy(char *from, char *to, size_t length)
{}

#else	/* lint */

	ENTRY(bcopy)
	cmp	%o0, %o1		! if (from < to)
	ble	2f			!   copy last bytes first
	sub	%o2, 1, %o3
	add	%o0, %o3, %o0
	add	%o1, %o3, %o1
	neg	%o2
1:
	inccc	%o2
	bg	3f
	ldub	[%o0 + %o2], %o3
	ba	1b
	stb	%o3, [%o1 + %o2]
2:
	deccc	%o2
	bl	3f
	ldub	[%o0 + %o2], %o3
	ba	2b
	stb	%o3, [%o1 + %o2]
3:
	retl
	nop
	SET_SIZE(bcopy)

#endif	/* lint */

#if defined(lint)

/*ARGSUSED*/
void
bzero(caddr_t addr, size_t len)
{}

#else	/* lint */

	ENTRY(bzero)
	deccc	%o1
	bg,a	bzero
	clrb	[%o0 + %o1]
	retl
	clrb	[%o0]
	SET_SIZE(bzero)

#endif	/* lint */

#if defined(lint)

/*ARGSUSED*/
int
strcmp(char *s1, char *s2)
{ return (0); }

#else	/* lint */

	ENTRY(strcmp)
	clr	%o4
	clr	%o5
1:
	ldub	[%o0 + %o4], %o2	! *s1
	ldub	[%o1 + %o4], %o3	! *s2
	subcc	%o2, %o3, %o5
	bnz	2f			! *s1 != *s2; done
	tst	%o2			! *s1 == NULL; done
	bz	2f
	tst	%o3			! *s2 == NULL; done
	bnz	1b
	inc	%o4
2:
	retl
	mov	%o5, %o0
	SET_SIZE(strcmp)

#endif	/* lint */

#if defined(lint)

/*ARGSUSED*/
int
strlen(char *s1)
{ return (0); }

#else	/* lint */

	ENTRY(strlen)
	clr	%o1
1:
	ldub	[%o0 + %o1], %o2
	tst	%o2
	bnz,a	1b
	inc	%o1
	retl
	mov	%o1, %o0
	SET_SIZE(strlen)

#endif	/* lint */

#if defined(lint)

/*ARGSUSED*/
char *
strcpy(char *to, char *from)
{ return (0); }

#else	/* lint */
	
	ENTRY(strcpy)
	save	%sp, -SA(MINFRAME), %sp
	call	strlen
	mov	%i1, %o0
	add	%o0, 1, %o2
	mov	%i0, %o1
	call	bcopy
	mov	%i1, %o0
	mov	%i0, %o0
	ret
	restore
	SET_SIZE(strcpy)

#endif	/* lint */
	
