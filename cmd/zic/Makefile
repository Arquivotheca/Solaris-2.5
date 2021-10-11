#
#ident	"@(#)Makefile	1.16	95/01/09 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

NATIVE=native
PROG= zic
NPROG= $(NATIVE)/$(PROG)

# "etcetera" must be listed before "backward" because it sets up links that
# "backward" needs.
TZFILES= africa antarctica asia australasia etcetera europe \
	 factory northamerica pacificnew solar87 solar88 solar89 \
	 southamerica systemv backward

OBJS= zic.o scheck.o ialloc.o emkdir.o
SRCS= $(OBJS:%.o=%.c)
NOBJS= $(OBJS:%=$(NATIVE)/%)
OWNER= bin
GROUP= bin

CLOBBERFILES= $(NOBJS) $(NPROG) $(NATIVE)/zic.c $(NATIVE)/tzfile.h

include ../Makefile.cmd

NCPPFLAGS=	-I$(NATIVE)
ROOTTZD=	$(ROOTSHLIB)/zoneinfo
DIRS=  $(ROOTTZD)

ROOTLIBTZFILES=	$(TZFILES:%=$(ROOTTZD)/%)

$(ROOTLIBTZFILES):=	FILEMODE=	0644

.KEEP_STATE:

all: $(PROG) $(NPROG)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(DIRS) $(ROOTUSRSBINPROG) $(ROOTLIBTZFILES)
	$(NPROG) -d $(ROOTTZD) $(TZFILES)
	$(CH) $(CHOWN) -R $(OWNER) $(ROOTTZD)
	$(CH) $(CHGRP) -R $(GROUP) $(ROOTTZD)

$(NPROG): $(NATIVE) $(NOBJS)
	$(NATIVECC) -o $@ $(NOBJS)

$(ROOTTZD)/%: %
	$(INS.file)

$(DIRS):
	$(INS.dir)

$(NATIVE):
	-@mkdir -p $@

clean:
	$(RM) $(OBJS)

lint: lint_SRCS

$(NOBJS): $$(@F:.o=.c)
	$(NATIVECC) $(NCPPFLAGS) -c -o $@ $(@F:.o=.c)

$(NATIVE)/zic.o: $(NATIVE)/tzfile.h

#$(NATIVE)/scheck.o: scheck.c
#	$(NATIVECC) -c -o $@ scheck.c

#$(NATIVE)/ialloc.o: ialloc.c
#	$(NATIVECC) -c -o $@ ialloc.c

$(NATIVE)/tzfile.h: $(ROOT)/usr/include/tzfile.h
	$(RM) $@; cp $(ROOT)/usr/include/tzfile.h $@

include ../Makefile.targ
