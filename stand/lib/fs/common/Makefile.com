#
#ident	"@(#)Makefile.com	1.1	94/07/27 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
# 
# stand/lib/fs/common/Makefile.com
#
# Standalone Library COMMON makefile
#
# This Makefile is included by ../../[sun,i386]/Makefile and is used
# when building $(LIBCOM).  The library is built in ../../[sun,i386].
#

COMOBJ=		fsswitch.o cache.o diskread.o
COMSRC=		$(COMOBJ:%.o=$(COMDIR)/%.c)
COMLINTOBJ=     $(COMOBJ:%.o=objs/%.ln)

$(LIBCOM) :=	OBJECTS = $(COMOBJ)
$(LIBCOM) :=	LOC_CPP = -I$(COMDIR)

.KEEP_STATE:
.PARALLEL:	$(COMLINTOBJ)

$(LINTLIBCOM): $(COMLINTOBJ)
	@-$(ECHO) "\n (common lint library construction):"
	@$(LINT) -o com $(LINTFLAGS) $(COMLINTOBJ)

objs/%.o: $(COMDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.ln: $(COMDIR)/%.c
	@($(LHEAD) $(LINT.c) $(BOOTCFLAGS) -c $< $(LTAIL))
	@$(MV) $(@F) $@
