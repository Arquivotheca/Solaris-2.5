#
#ident	"@(#)Makefile	1.17	94/08/26 SMI"
#
# Copyright (c) 1991, by Sun Microsystems, Inc.
#
# cmd/modload/Makefile
#
# makefile for loadable module utilities

# add_drv and rem_drv 
DRVPROG = add_drv rem_drv
MODPROG = modinfo modunload modload drvconfig 
PROG = $(MODPROG) $(DRVPROG)

include ../Makefile.cmd

# Generic

COMMONSRC = modsubr.c
COMMONOBJ = $(COMMONSRC:%.c=%.o)
SRCS = $(COMMONSRC) $(PROG:%=%.c)
AOBJS = $(MACH)/inst_sync.o
OBJECTS = $(SRCS:%.c=%.o)

ROOTDRVPROG = $(DRVPROG:%=$(ROOTUSRSBIN)/%)
DRVCOMMONSRC = drvsubr.c
DRVCOMMONOBJ = $(DRVCOMMONSRC:%.c=%.o)

CLOBBERFILES = $(PROG) 

# install specifics

OWNER = root
$(ROOTDRVPROG) := FILEMODE = 0555

# mod* specific
modinfo modunload modload drvconfig:= LDLIBS += -lelf
drvconfig:= LDLIBS += -lgen

ASFLAGS += -P -D_ASM $(CPPFLAGS)

.KEEP_STATE:

all: $(PROG) 

$(MODPROG): $(COMMONOBJ)

drvconfig:	drvconfig.o $(COMMONOBJ) $(AOBJS)
	$(LINK.c) -o $@ $< drvconfig.o $(COMMONOBJ) $(AOBJS) $(LDLIBS)
	$(POST_PROCESS)

add_drv:	add_drv.o $(DRVCOMMONOBJ)
	$(LINK.c)  -o $@ $< add_drv.o $(DRVCOMMONOBJ) $(LDLIBS) -lintl
 
rem_drv:	rem_drv.o $(DRVCOMMONOBJ)
	$(LINK.c)  -o $@ $< rem_drv.o $(DRVCOMMONOBJ) $(LDLIBS) -lintl


%: %.c $(COMMONOBJ)
	$(LINK.c) -o $@ $< $(COMMONOBJ) $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTUSRSBINPROG)

clean:
	$(RM) $(OBJECTS) core

lint: lint_SRCS

include ../Makefile.targ
