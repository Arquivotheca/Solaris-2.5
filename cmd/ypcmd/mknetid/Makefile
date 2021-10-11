#
#ident	"@(#)Makefile	1.7	92/12/15 SMI"
#
# cmd/ypcmd/mknetid/Makefile

PROG = mknetid

include ../../Makefile.cmd

LDLIBS += -ldl -lnsl

OBJS = mknetid.o getname.o 
SRCS = $(OBJS:.o=.c)


.KEEP_STATE:

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
