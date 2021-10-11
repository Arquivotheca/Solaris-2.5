#
#ident	"@(#)Makefile	1.10	95/01/30 SMI"
#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
# cmd/devlinks/Makefile
#

include ../Makefile.cmd

PROG= 	tapes disks ports devlinks audlinks

DEVLINK= devlink.tab
DEVLINKSRC=$(DEVLINK).sh

# common objects shared by all programs
COMMONSRC = utils.c
COMMONOBJ = $(COMMONSRC:%.c=%.o)

# srcs
SRCS = $(COMMONSRC) $(PROG:%=%.c)

# objects
OBJECTS = $(SRCS:%.c=%.o)

ROOTDEVLINK = $(DEVLINK:%=$(ROOTETC)/%)

LDLIBS += -ldevinfo -lgen -lelf -lintl -ldl

CPPFLAGS += -Ihdrs
CFLAGS += -v

TOUCHTIME= 0101000070
OWNER= root
GROUP= sys
FILEMODE= 755
$(ROOTDEVLINK) := FILEMODE = 644

$(CH)$(ROOTDEVLINK):= INS.file += ;$(TOUCH) $(TOUCHTIME) $@

all :=		TARGET= all
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint

.KEEP_STATE:

all: $(PROG) $(DEVLINK)

$(PROG): $(COMMONOBJ) $(LIBS)

$(DEVLINK): $(DEVLINKSRC)
	$(RM) $(DEVLINK)
	/bin/sh $(DEVLINKSRC) > $(DEVLINK)

# build rule
%: %.c $(COMMONOBJ)
	$(LINK.c) -o $@ $< $(COMMONOBJ) $(LIBS) $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTUSRSBINPROG) $(ROOTDEVLINK)

clean:	$(LIBSUBDIRS)
	$(RM) $(OBJECTS) $(DEVLINK)

lint:	lint_SRCS 

FRC:

include ../Makefile.targ

# additional dependency for clobber which is defined in ../Makefile.targ
clobber: 

