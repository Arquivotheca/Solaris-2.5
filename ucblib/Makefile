#
#ident	"@(#)Makefile	1.4	93/09/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# ucblib/Makefile
#
# include global definitions
include ../Makefile.master

# libbc is special.  It depends on libucb and must be built after libucb.
SUBDIRS= libdbm libcurses libtermcap libucb librpcsoc
sparc_SUBDIR= ../lib/libbc 
BWOSDIRS=

all:=		TARGET= all
install:=	TARGET= install
clean:=		TARGET= clean
clobber:=	TARGET= clobber
lint:=		TARGET= lint

.KEEP_STATE:

.PARALLEL:	$(SUBDIRS)

all install clean clobber: $(SUBDIRS) .WAIT $($(MACH)_SUBDIR)

bwos: $(BWOSDIRS) .WAIT $(SUBDIRS) .WAIT $($(MACH)_SUBDIR)

install_h:

$(BWOSDIRS) $(SUBDIRS) $($(MACH)_SUBDIR) : FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

