#! /bin/sh
#
# @(#)bsmconv.sh 1.2 93/03/11 SMI
#
PROG=bsmunconv
STARTUP=/etc/security/audit_startup

permission()
{
cd /usr/lib
WHO=`id | cut -f1 -d" "`
if [ ! "$WHO" = "uid=0(root)" ]
then
	echo "$PROG: you must be super-user to run this script."
	exit 1
fi

RESP="x"
while [ "$RESP" != "y" -a "$RESP" != "n" ]
do
echo "This script is used disable the Basic Security Module (BSM)."
echo "Shall we continue the reversion to a non-BSM system now? [y/n]"
read RESP
done

if [ "$RESP" = "n" ]
then
	echo "$PROG: INFO: aborted, due to user request."
	exit 2
fi
}

bsmunconvert()
{
# Move the startup script aside

echo "$PROG: INFO: moving aside ${ROOT}/etc/security/audit_startup."
if [ -f ${ROOT}/etc/security/audit_startup ]
then
mv ${ROOT}/etc/security/audit_startup ${ROOT}/etc/security/audit_startup.sav
fi

# Turn off auditing in the loadable module

if [ -f ${ROOT}/etc/system ]
then
	echo "$PROG: INFO: removing c2audit:audit_load from ${ROOT}/etc/system."
	grep -v "c2audit:audit_load" ${ROOT}/etc/system > /tmp/etc.system.$$
	mv /tmp/etc.system.$$ ${ROOT}/etc/system
else
	echo "$PROG: ERROR: can't find ${ROOT}/etc/system."
	echo "$PROG: ERROR: audit module may not be disabled."
fi
}

# main

permission

if [ $# -eq 0 ]
then
	ROOT=
	bsmunconvert
	echo
	echo "The Basic Security Module has been disabled."
	echo "Reboot this system now to come up without BSM."
else
	for ROOT in $@
	do
		bsmunconvert $ROOT
	done
	echo
	echo "The Basic Security Module has been disabled."
	echo "Reboot each system that was disabled to come up without BSM."
fi

exit 0
