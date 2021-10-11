#	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
#	  All Rights Reserved

#	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
#	The copyright notice above does not evidence any
#	actual or intended publication of such source code.

#ident	"@(#)Makefile	1.4	94/07/21 SMI"	

#		PROPRIETARY NOTICE (Combined)
#
#This source code is unpublished proprietary information
#constituting, or derived under license from AT&T's UNIX(r) System V.
#In addition, portions of such source code were derived from Berkeley
#4.3 BSD under license from the Regents of the University of
#California.
#
#
#
#		Copyright Notice 
#
#Notice of copyright on this source code product does not indicate 
#publication.
#
#	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
#	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
#	          All rights reserved.

#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley Software License Agreement
# specifies the terms and conditions for redistribution.
#
#
# C Shell with process control; VM/UNIX VAX Makefile
# Bill Joy UC Berkeley; Jim Kulp IIASA, Austria
#

include ../../Makefile.cmd

PROG	= csh
MBCHAR	= -DMBCHAR	# Define this line to include multibyte input support
DEFS	=  -DVFORK -DFILEC -DBSD_COMP -DFIVE	# No TELL when MBCHAR
CPPFLAGS= -I. -s $(DEFS) $(MBCHAR) $(CPPFLAGS.master)
LDLIBS +=  -lcurses -lw -lintl -lmapmalloc 

HDDEP = sh.o sh.dir.o sh.dol.o sh.err.o sh.exec.o sh.exp.o sh.file.o \
	sh.func.o sh.glob.o sh.hist.o sh.init.o sh.lex.o sh.misc.o \
	sh.parse.o sh.proc.o sh.sem.o sh.set.o sh.time.o

COMMONOBJS=	printf.o sh.char.o sh.dir.o sh.dol.o sh.err.o \
	sh.exec.o sh.exp.o sh.file.o sh.func.o sh.glob.o sh.hist.o sh.init.o \
	sh.lex.o sh.misc.o sh.parse.o sh.print.o sh.proc.o sh.sem.o sh.set.o \
	sh.time.o sh.tchar.o sh.tconst.o sh.o \
	bcopy.o bzero.o rindex.o getwd.o gethostname.o getrusage.o killpg.o \
        wait3.o index.o alloc.o setpriority.o setpgrp.o \
	getpagesize.o

LOCALOBJS= signal.o

COMMONSRCS=   $(COMMONOBJS:%.o=../%.c)

.KEEP_STATE:

.PARALLEL: $(COMMONOBJS) $(LOCALOBJS)

all: $(PROG)

# build rule for common source above
%.o: ../%.c
	$(COMPILE.c) $<
	$(POST_PROCESS_O)

$(PROG): sh.tconst.h .WAIT $(COMMONOBJS)  $(LOCALOBJS)
	$(LINK.c) $(COMMONOBJS) $(LOCALOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

sh.tconst.h: ../sh.tconst.c ../make.sh.tconst.h.ed 	
	$(RM) $@
	ed ../sh.tconst.c < ../make.sh.tconst.h.ed 
	
$(HDDEP): sh.tconst.h

install: all $(ROOTBINPROG)  $(ROOTPROG)

lint:	sh.tconst.h
	$(LINT.c) $(COMMONSRCS) signal.c

clean:
	$(RM) $(LOCALOBJS) $(COMMONOBJS) sh.tconst.h

clobber:	clean
	$(RM)  $(PROG)
