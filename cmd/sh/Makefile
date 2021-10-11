#
#ident	"@(#)Makefile	1.17	95/01/09 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

# build two versions, dynamic and static
PROG= sh
STATPROG= sh

OBJS=	args.o blok.o cmd.o defs.o error.o fault.o hash.o hashserv.o \
	io.o msg.o print.o service.o stak.o string.o word.o xec.o \
	ctype.o echo.o expand.o func.o macro.o pwd.o setbrk.o test.o \
	bltin.o jobs.o ulimit.o
DOBJS=	main.o name.o
SRCS=	$(OBJS:%.o=%.c) $(DOBJS:%.o=%.c)

DYNAMIC_OBJS=	$(DOBJS:%=dobjs/%)
STATIC_OBJS=	$(DOBJS:%=sobjs/%)

# install rule for static version
# deliberately ahead of a similar rule in ../Makefile.cmd
#
ROOT= /proto
$(ROOT)/sbin/%: %.static
	$(INS.rename)

include ../Makefile.cmd
#
# for message cataloge
#
POFILE= sh.po
POFILES= $(SRCS:%.c=%.po)
XGETFLAGS += -a -x sh.xcl


$(OBJS)		:= CPPFLAGS += -DACCT
$(STATIC_OBJS)	:= CPPFLAGS += -DACCT
$(DYNAMIC_OBJS)	:= CPPFLAGS += -DACCT -DSETLOCALE_FIX

LDLIBS += -lgen -lw -lintl
GROUP = root
CLOBBERFILES += $(STATIC)

.KEEP_STATE:

.PARALLEL:	$(OBJS) $(DYNAMIC_OBJS) $(STATIC_OBJS) $(POFILES)

all: $(PROG) .WAIT $(STATIC) 

$(PROG): dobjs .WAIT $(DYNAMIC_OBJS) $(OBJS)
	$(LINK.c) $(DYNAMIC_OBJS) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(STATIC): sobjs .WAIT $(STATIC_OBJS) $(OBJS)
	$(LINK.c) $(STATIC_OBJS) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

dobjs/%.o sobjs/%.o:	%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

dobjs sobjs:
	-@mkdir -p $@

$(POFILE): $(POFILES)
	$(RM) $@
	cat     $(POFILES) > $@

install: all $(ROOTSBINPROG) $(ROOTPROG)
	$(RM) $(ROOTSBIN)/jsh
	$(LN) -f $(ROOTSBIN)/sh $(ROOTSBIN)/jsh
	$(RM) $(ROOTLIB)/rsh
	$(LN) -f $(ROOTBIN)/sh $(ROOTLIB)/rsh
	$(RM) $(ROOTBIN)/jsh
	$(LN) -f $(ROOTBIN)/sh $(ROOTBIN)/jsh

clean:
	$(RM) $(DYNAMIC_OBJS) $(STATIC_OBJS) $(OBJS)

lint: lint_SRCS

include ../Makefile.targ
