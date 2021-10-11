#!/bin/sh
#
# Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
#
#	@(#)dumpconfrc.sed 1.11 92/09/24
#
HSMROOT=${HSMROOT:-XXXHSMROOTXXX}
libdir=$HSMROOT/lib
sbindir=$HSMROOT/sbin

TEXTDOMAIN=hsm_dumpex; export TEXTDOMAIN
if [ -d $libdir ]; then
	TEXTDOMAINDIR=$libdir/locale; export TEXTDOMAINDIR
fi

DF="/bin/df -kl -F ufs"

#
# find out if gettext(1) exists
#
if [ -x /bin/gettext ]; then
	gettext=gettext
	n="\n"
else
	gettext=echo
	n=""
fi

# make sure that only root is running this
if /bin/id | /bin/grep "uid=0(" >/dev/null 2>&1; then :
else
	$gettext "You must be superuser to run this program.$n"
	exit 1
fi

if [ ! -x $libdir/rpc.dumpdbd -o ! -x $libdir/rpc.operd ]; then
	$gettext "rpc.dumpdbd and/or rpc.operd are not installed in $libdir.$n"
	$gettext "Either install them or set and export the shell variable \$HSMROOT$n"
	$gettext "to the root of the SUNWhsm package and re-run this program.$n"
	exit 1
fi

if [ -d /etc/init.d ]; then
	if [ -f /etc/init.d/hsm ]; then
		$gettext "SUNWhsm init.d file appears to be installed.$n"
		exit 0
	fi
else
	if grep rpc.operd /etc/rc.local >/dev/null 2>&1; then
		$gettext "SUNWhsm rc.local support appears to be installed.$n"
		exit 0
	fi
fi

$gettext "Please choose a directory in which the dump database can reside.$n"
$gettext "It must be local to this machine (i.e., not an NFS mount).$n"
$gettext "It should be able to accommodate files which will grow to consume$n"
$gettext "about 4-7% of the file space that is dumped.  Here are a few of$n"
$gettext "the local file systems that have the most free-space available:$n"
$gettext "$n"

$DF | /bin/head -1					# the header
$DF | /bin/grep "^/" | /bin/sort -n -r +3 | /bin/head -4
bigfs=`$DF | /bin/grep "^/" | /bin/sort -r -n +3 | /bin/head -1 | /bin/awk '{print $6}'`

$gettext "$n"
$gettext "Where should the database live [${bigfs}/dumpdb]: "
read file
if [ -z "$file" ]; then
	file="${bigfs}/dumpdb"
fi
$gettext "$n"
$gettext "$n"

hsm=/tmp/hsm
if cp /dev/null $hsm >/dev/null 2>&1; then :
else
	hsm=/var/tmp/hsm
	if cp /dev/null $hsm; then :
	else
		$gettext "Cannot create temporary file ${hsm}$n"
		exit 1
	fi
fi
if [ -d /etc/init.d ]; then
	cat > $hsm << HERE_EOF
#!/bin/sh
#
# SUNWhsm daemon start-up and shutdown script
# Online: Backup 2.0
#
if [ ! -d $libdir ]; then	# $libdir not mounted
	if [ "\$1" != "stop" ]; then
		exit
	fi
fi

killproc() {		# kill the named process(es)
	pid=\`/usr/bin/ps -e |
	     /usr/bin/grep \$1 |
	     /usr/bin/sed -e 's/^  *//' -e 's/ .*//'\`
	[ "\$pid" != "" ] && kill \$pid
}

case "\$1" in
start)
	echo "Starting SUNWhsm daemons:\c"
	if [ -f $libdir/rpc.operd ]; then
		echo " rpc.operd\c"
		$libdir/rpc.operd &
	fi
	if [ -f $libdir/rpc.dumpdbd -a -d $file ]; then
		echo " rpc.dumpdbd\c"
		$libdir/rpc.dumpdbd $file &
	fi
	echo "."
	# Clear any Tmp_reserved tape volumes
	$sbindir/dumptm -R >/dev/null 2>&1 &
	;;
stop)
	killproc rpc.oper
	killproc rpc.dump
	;;
*)
	echo "Usage: /etc/init.d/hsm { start | stop }"
	;;
esac
exit 0
HERE_EOF
	#
	# install in init.d and rc?.d directories
	#
	/usr/sbin/install -m 744 -u root -g sys -f /etc/init.d $hsm >/dev/null 2>&1
	rm -f /etc/rc[012].d/K25hsm /etc/rc2.d/S90hsm
	ln /etc/init.d/hsm /etc/rc0.d/K25hsm
	ln /etc/init.d/hsm /etc/rc1.d/K25hsm
	ln /etc/init.d/hsm /etc/rc2.d/K25hsm
	ln /etc/init.d/hsm /etc/rc2.d/S90hsm
	rm -f $hsm
	#
	$gettext "The /etc/init.d/hsm startup script has been created.$n"
else
	cat > $hsm << HERE_EOF
#
# Online: Backup 2.0
#
echo -n "Starting SUNWhsm daemons:"
if [ -f $libdir/rpc.operd ]; then
	echo -n " operd"
	$libdir/rpc.operd &
fi
if [ -f $libdir/rpc.dumpdbd -a -d $file ]; then
	echo -n " dumpdbd"
	$libdir/rpc.dumpdbd $file &
fi
echo "."
echo "Recovering database updates."
$sbindir/hsmdump R &
HERE_EOF
	cat $hsm >> /etc/rc.local
	#
	$gettext "Your rc.local file has now been modified.$n"
fi

if [ -d $file ]; then :
else
	mkdir -p $file
	chmod 755 $file
fi

$gettext "Starting Online: Backup daemons: rpc.operd"
$libdir/rpc.operd &
$gettext " rpc.dumpdbd"
$libdir/rpc.dumpdbd $file &
$gettext ".$n"
