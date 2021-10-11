#
#ident	"@(#)Makefile	1.13	94/08/22 SMI"
#
# Copyright (c) 1991, 1992, 1993, 1994, by Sun Microsystems, Inc.
#
# cmd/crash/Makefile
#

PROG=	crash

# Gross, gross, gross!  This is more braindeath due to the lack if a fixed
# address for msgbuf!
OBJS_i386=	dumphdr.o
OBJS_sparc=

OBJS=	main.o base.o buf.o callout.o class.o cpu.o disp.o getsym.o init.o \
	kma.o kobj.o lck.o lock.o lwp.o major.o map.o mutex.o \
	nfs.o page.o pcfs.o prnode.o proc.o pty.o \
	rt.o search.o size.o sizenet.o snode.o status.o stream.o \
	symtab.o thread.o ts.o tty.o u.o ufs_inode.o util.o var.o vfs.o \
	vfssw.o vm.o vtop.o $(OBJS_$(MACH))

SRCS=	$(OBJS:%.o=%.c)

include ../Makefile.cmd

#	The crash program itself in root/sys, set gid.
OWNER = root
GROUP = sys
FILEMODE = 755

#
#	Silly compatibility link from /etc/crash to the binary
#
ROOTETCLINK=	$(ROOTETC)/$(PROG)
REL_ETC_SBIN=	../usr/sbin

LDLIBS += -lkvm -lelf

CPPFLAGS = -D_KMEMUSER $(CPPFLAGS.master)

.KEEP_STATE:

.PARALLEL:	$(OBJS)

all: $(PROG) 

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTUSRSBINPROG) $(ROOTETCLINK)

clean:
	$(RM) $(OBJS)

lint: lint_SRCS

cscope.files: FRC
	@$(RM) -f $@
	@echo -I$(ROOT)/usr/include > $@
	find . -name SCCS -prune -o -name '*.[ch]' -print >> $@
	@wc -l cscope.files

cscope.out: cscope.files FRC
	cscope -b -f `pwd`/$@

tags: cscope.files
	ctags -wt `sed 1d cscope.files`

$(ROOTETCLINK):
	$(RM) $@; $(SYMLINK) $(REL_ETC_SBIN)/$(PROG) $@

FRC:

include ../Makefile.targ
