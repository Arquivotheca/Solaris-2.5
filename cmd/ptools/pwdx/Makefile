#
#ident	"@(#)Makefile	1.2	95/03/01 SMI"
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/ptools/pwdx/Makefile
#

PROG = pwdx
LIBS = libcwd.so.1
OBJS = cwd.o
SRCS = $(OBJS:%.o=%.c)

include ../../Makefile.cmd
include ../Makefile.com

$(ROOTPROCBINPROG) := FILEMODE = 0555
$(ROOTPROCBINPROG) := OWNER = bin
$(ROOTPROCBINPROG) := GROUP = bin

$(PROG) := CPPFLAGS +=	-I../libproc
$(PROG) := LDLIBS +=	../libproc/libproc.a

# Optimization ruins libcwd
$(OBJS) := CFLAGS =	-K PIC

.KEEP_STATE:

all: $(PROG) $(LIBS)

$(LIBS):	$(OBJS)
	$(LD) -z defs -i -o $(LIBS) -e cwd -G $(OBJS)

install: all $(ROOTPROCBINPROG) $(ROOTPROCLIBLIB)

CLOBBERFILES += $(LIBS) $(OBJS)

clean:

lint:   lint_PROG lint_SRCS

include ../../Makefile.targ
