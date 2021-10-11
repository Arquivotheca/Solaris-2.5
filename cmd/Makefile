#
# Copyright (c) 1989-1995 by Sun Microsystems, Inc.
# All rights reserved.
#
#ident	"@(#)Makefile	1.127	95/06/06 SMI"
#
# cmd/Makefile
#
# include global definitions
include ../Makefile.master

#
#	The following command have been completely disabled inorder to
#	disable the sysadm build:
#	    bnu.admin   cfgintf     cfgmgmt     devintf     lp.admin
#	    nadmin.dfs  nadmin.nfs  netsel.adm  oamintf
#	    portmgmt
#	Note that 'lp' is first in the list, violating alphabetical
#	order.  This is because it is very long-running and should
#	be given the most wall-clock time for a parallel build.
#
#	Commands are listed one per line so that TeamWare can
#	auto-merge most changes.

COMMON_SUBDIRS=		\
	lp		\
	Adm		\
	acct		\
	adb		\
	arch		\
	asa		\
	aset		\
	asppp		\
	autopush	\
	awk		\
	awk_xpg4	\
	backup		\
	banner		\
	basename	\
	bc		\
	bdiff		\
	bfs		\
	bnu		\
	cal		\
	calendar	\
	cat		\
	checkeq		\
	checknr		\
	chgrp		\
	chmod		\
	chown		\
	chroot		\
	cksum		\
	clear		\
	cmd-inet	\
	cmp		\
	col		\
	comm		\
	compress	\
	cpio		\
	crash		\
	cron		\
	crypt		\
	csh		\
	csplit		\
	cut		\
	date		\
	dc		\
	dd		\
	deroff		\
	des		\
	devinfo		\
	devlinks	\
	devmgmt		\
	dfs.cmds	\
	diff		\
	diff3		\
	diffmk		\
	dircmp		\
	dirname		\
	dispadmin	\
	dmesg		\
	du		\
	dumpcs		\
	echo		\
	ed		\
	egrep		\
	env		\
	eqn		\
	expand		\
	expr		\
	exstr		\
	face		\
	factor		\
	false		\
	fdetach		\
	fdformat	\
	fgrep		\
	file		\
	find		\
	fmli		\
	fmt		\
	fmthard		\
	fmtmsg		\
	fn		\
	fold		\
	format		\
	fs.d		\
	fstyp		\
	fusage		\
	fuser		\
	gcore		\
	gencat		\
	getconf		\
	getent		\
	getfacl		\
	getmajor	\
	getopt		\
	gettext		\
	gettxt		\
	graph		\
	grep		\
	grep_xpg4	\
	groups		\
	grpck		\
	halt		\
	head		\
	hostid		\
	hostname	\
	iconv		\
	id		\
	init		\
	initpkg		\
	install.d	\
	iostat		\
	ipc		\
	join		\
	kerbd		\
	keyserv		\
	kgmon		\
	killall		\
	ksh		\
	last		\
	lastcomm	\
	line		\
	link		\
	listen		\
	locale		\
	localedef	\
	localedef_xsh3	\
	logger		\
	login		\
	logins		\
	logname		\
	look		\
	ls		\
	mach		\
	machid		\
	mail		\
	mailx		\
	makekey		\
	man		\
	mesg		\
	mkdir		\
	mkfifo		\
	mkfile		\
	mkmsgs		\
	mknod		\
	modload		\
	more		\
	more_xpg4	\
	mpstat		\
	mt		\
	mv		\
	mvdir		\
	netadm		\
	netfiles	\
	newform		\
	newgrp		\
	news		\
	nice		\
	nl		\
	nlsadmin	\
	nohup		\
	nsadmin		\
	nsadmin3b2	\
	nscd		\
	oampkg		\
	oamuser		\
	oawk		\
	od		\
	osadmin		\
	pack		\
	pagesize	\
	passmgmt	\
	passwd		\
	paste		\
	patch		\
	pathchk		\
	pax		\
	pbind		\
	pcmciad		\
	pg		\
	pr		\
	printf		\
	priocntl	\
	prtconf		\
	prtvtoc		\
	ps		\
	psradm		\
	psrinfo		\
	pt_chmod	\
	ptools		\
	pwck		\
	pwconv		\
	pwd		\
	reboot		\
	refer		\
	regcmp		\
	renice		\
	rexd		\
	rm		\
	rmdir		\
	rmt		\
	rpcbind		\
	rpcgen		\
	rpcinfo		\
	rpcsvc		\
	rpld		\
	sa		\
	saf		\
	savecore	\
	script		\
	sdiff		\
	sed		\
	sed_xpg4	\
	sendmail	\
	setfacl		\
	setmnt		\
	setpgrp		\
	setuname	\
	sgs		\
	sh		\
	sleep		\
	soelim		\
	sort		\
	spell		\
	spline		\
	split		\
	srchtxt		\
	streams		\
	strings		\
	su		\
	sulogin		\
	sum		\
	sunpc		\
	swap		\
	sync		\
	sysdef		\
	syslogd		\
	tabs		\
	tail		\
	tar		\
	tbl		\
	tcopy		\
	tee		\
	terminfo	\
	time		\
	tip		\
	tnf		\
	touch		\
	tplot		\
	tr		\
	troff		\
	true		\
	truss		\
	tty		\
	ttymon		\
	uadmin		\
	ul		\
	uname		\
	uniq		\
	units		\
	unlink		\
	unpack		\
	users		\
	utmp_update	\
	utmpd		\
	valtools	\
	vgrind		\
	vi		\
	vmstat		\
	volmgt		\
	w		\
	wall		\
	wc		\
	which		\
	who		\
	whodo		\
	write		\
	xargs		\
	xstr		\
	ypcmd		\
	zdump		\
	zic		

i386_SUBDIRS=		\
	addbadsec	\
	diskscan	\
	fdisk		\
	loadfont	\
	pcmapkeys	\
	rtc		

sparc_SUBDIRS=		\
	eeprom		\
	kbd		\
	loadkeys	\
	prtdiag		\
	ssa		\
	sx		

#
# commands that are messaged
# note that 'lp' comes first (see previous comment about 'lp')
#
MSGSUBDIRS=		\
	lp		\
	asa		\
	autopush	\
	awk		\
	awk_xpg4	\
	banner		\
	basename	\
	bnu		\
	cal		\
	cat		\
	checkeq		\
	checknr		\
	chgrp		\
	chmod		\
	chown		\
	cksum		\
	cmd-inet	\
	cmp		\
	col		\
	comm		\
	compress	\
	cpio		\
	cron		\
	csh		\
	csplit		\
	cut		\
	date		\
	dc		\
	dd		\
	deroff		\
	diff		\
	diffmk		\
	du		\
	dumpcs		\
	ed		\
	env		\
	eqn		\
	expand		\
	expr		\
	file		\
	find		\
	fold		\
	fs.d		\
	getconf		\
	getent		\
	gettext		\
	gettxt		\
	grep		\
	grep_xpg4	\
	halt		\
	head		\
	iconv		\
	id		\
	join		\
	last		\
	locale		\
	localedef	\
	logger		\
	logins		\
	logname		\
	ls		\
	mailx		\
	man		\
	mesg		\
	mkdir		\
	more		\
	more_xpg4	\
	mv		\
	newgrp		\
	nice		\
	nohup		\
	oampkg		\
	oawk		\
	od		\
	pack		\
	passwd		\
	paste		\
	patch		\
	pathchk		\
	pax		\
	pg		\
	pr		\
	printf		\
	prtdiag		\
	ps		\
	pwconv		\
	pwd		\
	reboot		\
	refer		\
	regcmp		\
	renice		\
	rm		\
	rmdir		\
	script		\
	sed		\
	sed_xpg4	\
	sgs		\
	sh		\
	sleep		\
	sort		\
	spell		\
	split		\
	strings		\
	su		\
	sum		\
	tabs		\
	tail		\
	tar		\
	tbl		\
	tee		\
	time		\
	tnf		\
	touch		\
	tr		\
	troff		\
	tty		\
	ttymon		\
	ul		\
	uname		\
	uniq		\
	units		\
	unlink		\
	unpack		\
	vgrind		\
	vi		\
	w		\
	wc		\
	whodo		\
	write		\
	xargs		

#
# commands that use dcgettext for localized time, LC_TIME
#
DCSUBDIRS=		\
	cal		\
	ls		\
	tar		\
	w		\
	whodo		\
	write		

#
# commands that belong only to the basic security module
#
BSMSUBDIRS=		\
	allocate	\
	audit		\
	audit_warn	\
	auditconfig	\
	auditd		\
	auditreduce	\
	auditstat	\
	dminfo		\
	praudit		\
	bsmconv		\
	bsmunconv		

#
# commands not owned by the systems group
#
BWOSDIRS= 

all :=		TARGET= all
install :=	TARGET= install
install_h :=	TARGET= install_h
check :=	TARGET= check
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint
_msg :=		TARGET= _msg
_dc :=		TARGET= _dc

.KEEP_STATE:

SUBDIRS = $(COMMON_SUBDIRS)  $($(MACH)_SUBDIRS)

.PARALLEL:	$(BWOSDIRS) $(SUBDIRS) $(MSGSUBDIRS)  $(BSMSUBDIRS)

all install clean clobber lint: $(SUBDIRS) $(BSMSUBDIRS)

# those commands which install headers possibly needed by other commands.
install_h check:
	@cd mail; pwd; $(MAKE) $(TARGET)

_msg: $(MSGSUBDIRS) _dc

_dc: $(DCSUBDIRS)

$(BWOSDIRS) $(SUBDIRS) $(BSMSUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
