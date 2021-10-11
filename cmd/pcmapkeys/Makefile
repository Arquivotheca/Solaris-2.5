#
#pragma ident "@(#)Makefile	1.10	94/03/25 SMI"
#
# Copyright (c) 1993, by Sun Microsystems, Inc.
#
# cmd/pcmapkeys/Makefile

include ../Makefile.cmd

PROG = pcmapkeys
DEFAULTKB = defaultkb

MAPDIRS	=	437	646y	850	863	865	866		\
		8859-1	8859-2	8859-3	8859-4	8859-5	8859-7	8859-9  \
		csfr	greek	japan	korean	sample	sample/dead	\
		tchinese

MAPFILES = 	437/nl_BE	437/de_CH	437/fr_CA	437/fr_CH \
		437/de_DE	437/fr_FR	437/nl_NL	437/es_ES \
		437/it_IT	437/da_DK	437/no_NO	437/sv_SE \
		437/en_UK	437/en_US \
		\
		646y/sh_YU \
		\
		850/en_UK	850/nl_BE	850/de_CH	850/fr_CA \
		850/fr_CH	850/de_DE	850/fr_FR	850/nl_NL \
		850/es_ES	850/it_IT	850/da_DK	850/no_NO \
		850/sv_SE	850/en_US \
		\
		863/fr_CA \
		\
		865/da_DK	865/no_NO \
		\
		866/ru_RU.s	866/ru_RU.t \
		\
		8859-1/nl_BE	8859-1/de_CH	8859-1/fr_CA	8859-1/fr_CH \
		8859-1/de_DE	8859-1/fr_FR	 8859-1/nl_NL	8859-1/es_ES \
		8859-1/it_IT	8859-1/da_DK	8859-1/no_NO	8859-1/sv_SE \
		8859-1/en_UK	8859-1/en_US \
		\
		8859-2/sh_YU	8859-2/de_DE	8859-2/sk_CZ \
		\
		8859-3/de_CH	8859-3/nl_BE	8859-3/fr_CH	8859-3/de_DE \
		8859-3/it_IT \
		\
		8859-4/da_DK	8859-4/no_NO	8859-4/sv_SE \
		\
		8859-5/ru_RU.s	8859-5/ru_RU.t \
		\
		8859-7/el_GR.s	8859-7/el_GR.t \
		\
		8859-9/de_DE	8859-9/nl_NL	8859-9/da_DK	8859-9/no_NO \
		8859-9/sv_SE \
		\
		csfr/sk_CZ \
		\
		greek/el_GR.s	greek/el_GR.t \
		\
		japan/j3100	japan/ja_JP \
		\
		korean/ko_KR \
		\
		tchinese/zh_TW \
		\
		sample/README	sample/compose	sample/da_DK	sample/de_CH \
		sample/de_DE	sample/el_GR.s	sample/el_GR.t	sample/en_UK \
		sample/en_US	sample/es_ES	sample/fr_BE	sample/fr_CA \
		sample/fr_CH	sample/fr_FR	sample/is_IS	sample/it_IT \
		sample/nl_BE	sample/nl_NL	sample/no_NO	sample/ru_RU.s \
		sample/ru_RU.t	sample/sh_YU	sample/sk_CZ	sample/sv_SE \
		\
		sample/dead/acute sample/dead/caron sample/dead/cedilla      \
		sample/dead/circumflex sample/dead/diaeresis     	     \
		sample/dead/dot-above sample/dead/double-acute 	   	     \
		sample/dead/grave sample/dead/stroke sample/dead/tilde

SYMLINK_MAPFILES = 437/fr_BE	850/fr_BE	8859-1/fr_BE	8859

OBJS = pcmapkeys.o lexer.o parser.o

SRCS = $(OBJS:%.o=%.c)

YFLAGS = -dvt
LDLIBS += -ly -ll -lintl

ROOTKEYBOARDS = $(ROOTSHLIB)/keyboards
ROOTMAPDIRS   = $(MAPDIRS:%=$(ROOTKEYBOARDS)/%)
ROOTDEFAULTKB = $(DEFAULTKB:%=$(ROOTETC)/%)
ROOTMAPFILES  = $(MAPFILES:%=$(ROOTKEYBOARDS)/%)
ROOTMAPSLN    = $(SYMLINK_MAPFILES:%=$(ROOTKEYBOARDS)/%)

$(ROOTDEFAULTKB) := FILEMODE = 0644
$(ROOTDEFAULTKB) := OWNER = root
$(ROOTDEFAULTKB) := GROUP = sys

$(ROOTKEYBOARDS) $(ROOTMAPDIRS) := DIRMODE = 0555
$(ROOTMAPFILES) := FILEMODE = 0444

$(ROOTKEYBOARDS)/437/fr_BE	:=	LINKDEST = nl_BE
$(ROOTKEYBOARDS)/850/fr_BE	:=	LINKDEST = nl_BE
$(ROOTKEYBOARDS)/8859-1/fr_BE	:=	LINKDEST = nl_BE
$(ROOTKEYBOARDS)/8859		:=	LINKDEST = 8859-1

.KEEP_STATE:

all:  $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

lexer.o: parser.o               # lexer.l includes y.tab.h

install: 	all $(ROOTPROG) $(ROOTKEYBOARDS) $(ROOTMAPDIRS)	\
		    $(ROOTDEFAULTKB) $(ROOTMAPFILES) $(ROOTMAPSLN)

$(ROOTKEYBOARDS): 
	$(INS.dir)

$(ROOTMAPDIRS): $(ROOTKEYBOARDS)
	$(INS.dir)

$(ROOTKEYBOARDS)/%: %
	$(INS.file)

$(ROOTMAPSLN):
	$(RM) $@; $(SYMLINK) $(LINKDEST) $@

clean:
	$(RM) $(OBJS)
	$(RM) y.tab.h y.output lex.yy.c

lint:	lint_SRCS

include ../Makefile.targ
