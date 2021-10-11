#
#ident	"@(#)Makefile	1.3	94/03/30 SMI"
#

PROG= sed

OBJS= sed0.o sed1.o
SRCS= $(OBJS:%.o=%.c)

include ../Makefile.ucbcmd

.KEEP_STATE:

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTPROG)

clean: 
	$(RM) $(OBJS)

lint: lint_SRCS

include ../Makefile.ucbtarg
