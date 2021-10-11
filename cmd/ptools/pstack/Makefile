#ident	"@(#)Makefile	1.2	95/03/01 SMI"

# makefile for pstack(1) command
#
# make		- make pstack in local directory
# make install	- make pstack and install in $(BIN)
# make lint	- check program consistency
# make clean	- as your mother told you
# make clobber	- make it squeeky clean

PROG = pstack

include ../../Makefile.cmd
include ../Makefile.com

FILEMODE = 0555
OWNER = bin
GROUP = bin

CPPFLAGS +=	-I../libproc
LDLIBS +=	../libproc/libproc.a -lelf

SOURCES = pstack.c
OBJECTS = pstack.o

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROCBINPROG)

clean:

lint:   lint_PROG

include ../../Makefile.targ
