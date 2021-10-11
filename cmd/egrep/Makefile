#
#ident	"@(#)Makefile	1.6	92/12/15 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#

PROG= egrep

include ../Makefile.cmd

LDLIBS += -lw -lintl

.KEEP_STATE:

all: $(PROG) 

install: all $(ROOTPROG)


$(PROG).c: $(PROG).y
	yacc $<
	sed '-e 1 a\
	\#define yyerror_gettext(x) yyerror(gettext(x))' y.tab.c > y1.c
	sed '-e /syntax/ s/yyerror/yyerror_gettext/' y1.c > $@
	rm y1.c y.tab.c
	
clean:
	$(RM) $(PROG).o $(PROG).c

lint:

include ../Makefile.targ
