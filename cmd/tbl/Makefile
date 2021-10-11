#
# ident	"@(#)Makefile	1.7	92/09/03 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/tbl/Makefile
#

include		../Makefile.cmd

LDLIBS += -lintl

PROG =		tbl

SRCS =		t0.c t1.c t2.c t3.c t4.c t5.c t6.c t7.c	\
		t8.c t9.c tb.c tc.c te.c tf.c tg.c ti.c	\
		tm.c ts.c tt.c tu.c tv.c

OBJS =		$(SRCS:%.c=%.o)

POFILES=        $(OBJS:%.o=%.po)
POFILE=         tbl.po

.KEEP_STATE :

all :		$(PROG)

$(PROG):	$(OBJS)
		$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
		$(POST_PROCESS)

$(POFILE):      $(POFILES)
	$(RM)	$@
	cat     $(POFILES)      > $@

install :	all $(ROOTPROG)

clean : 
		$(RM) $(OBJS)

lint:		lint_SRCS

strip :
		$(STRIP) $(PROG)

include		../Makefile.targ
