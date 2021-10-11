#
#ident	"@(#)Makefile	1.6	95/01/28 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# cmd/pcmciad/Makefile
#

include ../Makefile.cmd

PROG= 	pcmciad

# srcs
SRCS = $(PROG:%=%.c)

# objects
OBJECTS = $(SRCS:%.c=%.o)

LDLIBS += -ldevinfo -lvolmgt
CFLAGS += -v

ROOTLIBPROG=	$(PROG:%=$(ROOTLIB)/%)

OWNER= root
GROUP= bin
FILEMODE= 555

all :=		TARGET= all
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint

.KEEP_STATE:

all: $(PROG) $(DEVLINK)

$(PROG): $(COMMONOBJ) $(LIBS)

# build rule
%: %.c $(COMMONOBJ)
	$(LINK.c) -o $@ $< $(COMMONOBJ) $(LIBS) $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTLIBPROG) $(ROOTDEVLINK)

$(ROOTLIB)/%:	%
		$(INS.file)

clean:	$(LIBSUBDIRS)
	$(RM) $(OBJECTS) $(DEVLINK)

lint:	lint_SRCS 

FRC:

include ../Makefile.targ

# additional dependency for clobber which is defined in ../Makefile.targ
clobber:
