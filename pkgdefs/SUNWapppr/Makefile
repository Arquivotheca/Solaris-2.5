#
#ident	"@(#)Makefile	1.2	94/03/16 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#

include ../Makefile.com


DATAFILES += depend

ACTION_sparc=
ACTION_i386=grep os86r depend > /dev/null || \
	( chmod 666 depend; \
	echo "P SUNWos86r	x86 Platform Root Objects" >> depend; \
	chmod 444 depend )

.KEEP_STATE:

all: $(FILES) action

# action is a pseudotarget denoting completed work on the depend file
action: depend
	$(ACTION_$(MACH))
	touch $@

install: all pkg

include ../Makefile.targ
