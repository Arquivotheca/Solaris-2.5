#
#ident	"@(#)Makefile	1.16	94/02/01 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= login
OBJS= login.o
SRCS= $(OBJS:%.o=%.c)

include ../Makefile.cmd

DFLTD= $(ROOTETC)/default
DIRS= $(DFLTD)
LOGINDEVPERM= $(ROOTETC)/logindevperm

ETCDFLTPROG = $(PROG:%=$(DFLTD)/%)
$(ETCDFLTPROG) := FILEMODE = $(LIBFILEMODE)
$(ETCDFLTPROG) := GROUP = sys
$(LOGINDEVPERM) := FILEMODE = 644
$(LOGINDEVPERM) := GROUP = sys
FILEMODE= 4555
OWNER= root

CPPFLAGS += -DSYSV -DCONSOLE='"/dev/console"' -DSECURITY

# libsocket is included for the ruserok() function.
LDLIBS += -lcmd -lgen -lcrypt_i -lbsm -lsocket -lnsl -ldl -lauth -lintl
CFLAGS += -v

.KEEP_STATE:

all: $(PROG) $(PROG).dfl 

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(DIRS) $(ROOTPROG) $(PROG).dfl $(ETCDFLTPROG) $(LOGINDEVPERM)

$(DIRS):
	$(INS.dir)

$(DFLTD)/% : %
	$(MV) $(PROG) $(PROG).save
	$(LN) $(PROG).dfl $(PROG)
	$(INS.file)
	$(RM) $(PROG)
	$(MV) $(PROG).save $(PROG)

clean:
	$(RM) $(OBJS)

lint:	lint_SRCS

include ../Makefile.targ
