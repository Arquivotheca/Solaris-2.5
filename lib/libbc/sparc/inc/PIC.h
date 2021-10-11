!
!       @(#)PIC.h 1.12 88/02/08 SMI
!
#ifdef PIC 
#define PIC_SETUP(r) \
	or	%g0,%o7,%g1; \
1: \
	call	2f; \
	nop; \
2: \
	sethi	%hi(_GLOBAL_OFFSET_TABLE_ - (1b-.)), %r; \
	or	%r, %lo(_GLOBAL_OFFSET_TABLE_ - (1b-.)),%r; \
	add	%r, %o7, %r; \
	or	%g0,%g1,%o7
#else 
#define PIC_SETUP()
#endif 

