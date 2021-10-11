#
#ident	"@(#)Makefile	1.8	92/12/15 SMI"
#
# cmd/ypcmd/revnetgroup/Makefile

PROG = revnetgroup

include ../../Makefile.cmd

OBJS = revnetgroup.o getgroup.o table.o util.o
SRCS = $(OBJS:.o=.c)
HDRS = getgroup.h table.h util.h
LDLIBS += -ldl


all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTUSRSBINPROG)

clean: 
	$(RM) $(OBJS)

clobber: clean
	$(RM) $(PROG)

FRC:
