#
#ident        "@(#)Makefile.4.x 1.8     95/08/17 SMI"
#
# Copyright (c) 1995, by Sun Microsystems, Inc.
# All rights reserved.
#
# A build environment can be created for this using the sgs/tools
# scripts.  Use `make native' to create the appropriate NSE variant,
# but use `addproto -v native -t 5.0' to populate all the 5.0 headers.
#
# tpm, Wed Oct 26 01:39:36 PDT 1994
#
# Now that the days of NSE are gone, all you should need to do is to
# point ROOT at the header files you're using, and do
#
#	% sccs edit ld.so
#	% make -f Makefile.4.x all
#	<test it a lot>
#	% sccs delget ld.so
#
# Unfortunately, <sys/isa_defs.h> contains a '#error' line that makes the 4.x
# cpp choke (even though it shouldn't parse the error clause).  You may need to
# delete the '#' sign to make the linker compile.  Also, <sys/elf.h> contains
# two prototype definitions (for kernel use) that should be deleted. Ick.

OBJS=	rtldlib.o rtld.4.x.o rtsubrs.o div.o umultiply.o rem.o zero.o

all:	${OBJS}
	ld -o ld.so -Bsymbolic -assert nosymbolic -assert pure-text ${OBJS}

%.o:%.s
	as -k -P -I$(ROOT)/usr/include -D_SYS_SYS_S -D_ASM $<
	mv -f a.out $*.o

%.o:%.c
	cc -c -O -I$(ROOT)/usr/include -pic -D_NO_LONGLONG $<
