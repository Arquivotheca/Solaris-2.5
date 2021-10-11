#! /bin/sh
#
# @(#)bsmconv.sh 1.4 93/03/12 SMI
#
PROG=bsmconv
STARTUP=/etc/security/audit_startup
DEVALLOC=/etc/security/device_allocate
DEVMAPS=/etc/security/device_maps

permission()
{
WHO=`id | cut -f1 -d" "`
if [ ! "$WHO" = "uid=0(root)" ]
then
	echo "$PROG: ERROR: you must be super-user to run this script."
	exit 1
fi

RESP="x"
while [ "$RESP" != "y" -a "$RESP" != "n" ]
do
echo "This script is used to enable the Basic Security Module (BSM)."
echo "Shall we continue with the conversion now? [y/n]"
read RESP
done

if [ "$RESP" = "n" ]
then
	echo "$PROG: INFO: aborted, due to user request."
	exit 2
fi
}

# Do some sanity checks to see if the arguments to bsmconv
# are, in fact, root directories for clients.
sanity_check()
{
for ROOT in $@
do

	if [ -d $ROOT -a -w $ROOT -a -f $ROOT/etc/system -a -d $ROOT/usr ]
	then
		# There is a root directory to write to,
		# so we can potentially complete the conversion.
		:
	else
		echo "$PROG: ERROR: $ROOT doesn't look like a client's root."
		echo "$PROG: ABORTED: nothing done."
		exit 4
	fi
done
}

# bsmconvert
#	All the real work gets done in this function

bsmconvert()
{

# If there is not startup file to be ready by /etc/rc2.d/S99audit,
# then make one.

echo "$PROG: INFO: checking startup file."
if [ ! -f ${ROOT}/${STARTUP} ]
then
	cat > ${ROOT}/${STARTUP} <<EOF
#!/bin/sh
auditconfig -conf
auditconfig -setpolicy none
auditconfig -setpolicy +cnt
EOF
fi

if [ ! -f ${ROOT}/${STARTUP} ]
then
	echo "$PROG: ERROR: no ${STARTUP} file."
	echo "  continuing..."
fi

chgrp sys ${ROOT}/${STARTUP} > /dev/null 2>&1
chmod 0744 ${ROOT}/${STARTUP} > /dev/null 2>&1

# Turn on auditing in the loadable module

echo "$PROG: INFO: turning on audit module."
if [ ! -f ${ROOT}/etc/system ]
then
	echo "" > ${ROOT}/etc/system
fi

grep -v "c2audit:audit_load" ${ROOT}/etc/system > /tmp/etc.system.$$
echo "set c2audit:audit_load = 1" >> /tmp/etc.system.$$
mv /tmp/etc.system.$$ ${ROOT}/etc/system
grep "set c2audit:audit_load = 1" ${ROOT}/etc/system > /dev/null 2>&1
if [ $? -ne 0 ]
then
	echo "$PROG: ERROR: cannot 'set c2audit:audit_load = 1' in ${ROOT}/etc/system"
	echo "$PROG: Continuing ..."
fi

# Initial device allocation files

echo "$PROG: INFO: initializing device allocation files"
if [ ! -f ${ROOT}/$DEVALLOC ]
then
	mkdevalloc > ${ROOT}/$DEVALLOC
fi
if [ ! -f $DEVMAPS ]
then
	mkdevmaps > ${ROOT}/$DEVMAPS
fi

}

# main loop

permission
sanity_check
if [ $# -eq 0 ]
then
	ROOT=
	bsmconvert
	echo
	echo "The Basic Security Module is ready."
	echo "If there were any errors, please fix them now."
	echo "Configure BSM by editing files located in /etc/security."
	echo "Reboot this system now to come up with BSM enabled."
else
	for ROOT in $@
	do
		echo "$PROG: INFO: converting host `basename $ROOT` ..."
		bsmconvert $ROOT
		echo "$PROG: INFO: done with host `basename $ROOT`."
	done
	echo
	echo "The Basic Security Module is ready."
	echo "If there were any errors, please fix them now."
	echo "Configure BSM by editing files located in etc/security"
	echo "in the root directories of each host converted."
	echo "Reboot each system converted to come up with BSM active."
fi

exit 0
