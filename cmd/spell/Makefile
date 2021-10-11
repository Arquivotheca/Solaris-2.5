#
#ident	"@(#)Makefile	1.13	94/12/06 SMI"
#
# Copyright (c) 1989 by Sun Microsystems, Inc.
#
# cmd/spell/Makefile
#

HASHPROG=	hashmk1 spellin1
SHPROG=		spell compress
HASHTAB=	hlista hlistb hstop
BINPROG=	spellprog spellin hashmake hashcheck
PROG=		$(SHPROG) $(BINPROG) $(HASHPROG) $(HASHTAB) spellhist

SPELLPROGSRC=	spellprog.c hash.c hashlook.c huff.c
SPELLINSRC=	spellin.c huff.c
HASHCHECKSRC=	hashcheck.c hash.c huff.c
HASHMAKESRC=	hashmake.c hash.c

SRCS= $(SPELLPROGSRC) spellin.c hashcheck.c hashmake.c
OBJS= $(SRCS:%.c=%.o)

WORDS=	american british local list extra stop
TXTS=	compress.sh spell.sh

include ../Makefile.cmd

LDLIBS += -lgen -lintl
CFLAGS += -v

XGETFLAGS += -a -x spell.xcl

CLOBBERFILES += htemp1 htemp2

ROOTSPELLD = $(ROOTLIB)/spell
ROOTVARADMD = $(ROOT)/var/adm
ROOTDIRS= $(ROOTSPELLD)

BINF= spell
SPELLF= spellprog hashmake hashcheck spellin hstop hlistb hlista compress
VARADMF= spellhist

ROOTBINF = $(BINF:%=$(ROOTBIN)/%)
ROOTSPELLF = $(SPELLF:%=$(ROOTSPELLD)/%)
ROOTVARADMF = $(VARADMF:%=$(ROOTVARADMD)/%)

$(CH)DIRMODE = 0555
HASHFILEMODE = 0644
$(ROOTSPELLD)/hstop $(ROOTSPELLD)/hlistb \
$(ROOTSPELLD)/hlista :=	FILEMODE =	0644
$(ROOTVARADMF) :=	FILEMODE =	0666

spellin1 hashmk1 := CC = $(NATIVECC)
spellin1 hashmk1 := POST_PROCESS =
spellin1 hashmk1 := CPPFLAGS =
spellin1 hashmk1 := LDFLAGS =

$(ROOTSPELLD)/% $(ROOTVARADMD)/% : %
	$(INS.file)

.KEEP_STATE:

all: 	$(PROG) $(TXTS)

spellprog:		$(SPELLPROGSRC)
			$(LINK.c) $(SPELLPROGSRC) -o $@ $(LDLIBS)
			$(POST_PROCESS)

spellin spellin1:	$(SPELLINSRC)
			$(LINK.c) $(SPELLINSRC) -o $@ $(LDLIBS)
			$(POST_PROCESS)

spellhist:
			$(ECHO) '\c' > spellhist

hashcheck:		$(HASHCHECKSRC)
			$(LINK.c) $(HASHCHECKSRC) -o $@ $(LDLIBS)
			$(POST_PROCESS)

hashmake hashmk1:	$(HASHMAKESRC)
			$(LINK.c) $(HASHMAKESRC) -o $@ $(LDLIBS)
			$(POST_PROCESS)

htemp1:	list local extra hashmk1
	$(RM) $@; cat list local extra | $(_SH_) ./hashmk1 > $@

hlista: american $(HASHPROG) htemp1
	$(RM) htemp2; ./hashmk1 <american |sort -u - htemp1 >htemp2
	$(RM) $@; ./spellin1 `wc htemp2|sed -n 's/\([^ ]\) .*/\1/p'`<htemp2 >$@
	$(RM) htemp2

hlistb: british $(HASHPROG) htemp1
	$(RM) htemp2; ./hashmk1 <british |sort -u - htemp1 >htemp2
	$(RM) $@; ./spellin1 `wc htemp2|sed -n 's/\([^ ]\) .*/\1/p'`<htemp2 >$@
	$(RM) htemp2

hstop:	stop $(HASHPROG)
	$(RM) htemp2; ./hashmk1 <stop | sort -u >htemp2
	$(RM) $@; ./spellin1 `wc htemp2|sed -n 's/\([^ ]\) .*/\1/p'`<htemp2 >$@
	$(RM) htemp2

install: all $(ROOTDIRS) $(ROOTBINF) $(ROOTSPELLF) $(ROOTVARADMF)

$(ROOTDIRS):
	$(INS.dir)

lint:	lint_SRCS

clean:
	$(RM) $(OBJS)

include ../Makefile.targ
