#!/bin/sh
#
# @(#)nissetup.sh	1.8 92/04/24   Copyright 1992 Sun Microsystems Inc.
#
# Initial setup for NIS+ domains. Note this only needs to be run on those
# domains that you plan to make into user level domains (that is domains
# with host maps and passwds etc)
#

USAGE="usage: nissetup [-Y] [domain]";
PASSOFF="n-r"

if [ $# -eq 0 ]
then
	A=access=og=rmcd,nw=r;
	D=`nisdefaults -d`;
else
	case $1 in
		-Y)
			if [ $# -eq 1 ]
			then
				D=`nisdefaults -d`;
			elif [ $# -eq 2 ]
			then
				D=$2;
			else
				echo $USAGE;
				exit 1;
			fi;
			A=access=og=rmcd,nw=r;
			PASSOFF="";;     # allow "nobody" read access in YP mode
		-*)
			echo $USAGE;
			exit 1;;
		*)
			if [ $# -ne 1 ]
			then
				echo $USAGE;
				exit 1;
			fi;
			A=access=og=rmcd,nw=r;
			D=$1;;
	esac;
fi;

# column access for most tables (passwd excluded, see below)
CA=nogw=

if nistest org_dir.$D;
then
	if nistest -t D org_dir.$D;
	then
		echo org_dir.$D already exists;
	else
		echo org_dir.$D exists and is not a directory
		exit 1;
	fi;
else
	if nismkdir -D $A org_dir.$D;
	then
		echo org_dir.$D created;
	else
		echo couldn\'t create org_dir.$D
		exit 1;
	fi;
fi;

if nistest groups_dir.$D;
then
	if nistest -t D groups_dir.$D;
	then
		echo groups_dir.$D already exists;
	else
		echo groups_dir.$D exists and is not a directory
		exit 1;
	fi;
else
	if nismkdir -D $A groups_dir.$D;
	then
		echo groups_dir.$D created;
	else
		echo couldn\'t create groups_dir.$D
		exit 1;
	fi;
fi;



# Table creation order is arbitrary.  We do passwd, group, then
# other standard tables in alphabetical order.

#passwd table permissions for default secure setup
#	table ----rmcdrmcdr---	
#	informational columns n+r
#	passwd column o+m
#	gcos column n+r,o+m
#	shadow (aging info) column nowg= (no added perms above those on tbl)
#This makes:
#	1. The entire table readable to authenticated users.
#	2. Unauthenticated users can still see the "informational" columns,
#	   but not the passwd or shadow columns. Unauthenticated clients must
#	   have read access to the informational columns in order for
#	   programs looking up this info while running as "daemon" to work.
#	3. Entry owner can modify passwd and gcos.
#
#Note: In the YP compat service setup, the table-level permissions get
#      n+r added. This is required for YP client access, but overrides
#      the hiding of the passwd column from unauthenticated clients.

if nistest passwd.org_dir.$D;
then
	echo passwd.org_dir.$D already exists;
else
	if nistbladm -D $A,$PASSOFF -c -s : passwd_tbl name=S,n+r passwd=C,o+m uid=S,n+r gid=,n+r gcos=,n+r,o+m home=,n+r shell=,n+r shadow=,nowg= passwd.org_dir.$D;
	then
		echo passwd.org_dir.$D created;
	else
		echo couldn\'t create passwd.org_dir.$D;
	fi;
fi;


#group table permissions for default secure setup
#	table ----rmcdrmcdr---
#	passwd column o+m
#	members column o+m,n+r
#	all other columns n+r
#
#Explanation is same as for passwd table above.
#

if nistest group.org_dir.$D;
then
	echo group.org_dir.$D already exists;
else
	if nistbladm -D $A,$PASSOFF -c -s : group_tbl name=S,n+r passwd=C,o+m gid=S,n+r members=,o+m,n+r group.org_dir.$D;
	then
		echo group.org_dir.$D created;
	else
		echo couldn\'t create group.org_dir.$D;
	fi;
fi;

# permissions on all other tables for default secure setup is:
# A=access=og=rmcd,w=r,n=r;
# this is to allow all processes run as 'daemon' to have access to
# the information stored in the NIS+ tables. 

if nistest auto_master.org_dir.$D;
then
	echo auto_master.org_dir.$D already exists;
else
	if nistbladm -D $A -c automount_map key=S,$CA value=,$CA auto_master.org_dir.$D;
	then
		echo auto_master.org_dir.$D created;
	else
		echo couldn\'t create auto_master.org_dir.$D;
	fi;
fi;

if nistest auto_home.org_dir.$D;
then
	echo auto_home.org_dir.$D already exists;
else
	if nistbladm -D $A -c automount_map key=S,$CA value=,$CA auto_home.org_dir.$D;
	then
		echo auto_home.org_dir.$D created;
	else
		echo couldn\'t create auto_home.org_dir.$D;
	fi;
fi;

if nistest bootparams.org_dir.$D;
then
	echo bootparams.org_dir.$D already exists;
else
	if nistbladm -D $A -c bootparams_tbl key=SI,$CA value=,$CA bootparams.org_dir.$D;
	then
		echo bootparams.org_dir.$D created;
	else
		echo couldn\'t create bootparams.org_dir.$D;
	fi;
fi;

if nistest cred.org_dir.$D;
then
	echo cred.org_dir.$D already exists;
else
	if nistbladm -D $A -c -s : cred_tbl cname=SI,$CA auth_type=SI,$CA auth_name=SI,$CA public_data=,$CA,o+m  private_data=C,$CA,o+m cred.org_dir.$D;
	then
		echo cred.org_dir.$D created;
	else
		echo couldn\'t create cred.org_dir.$D;
	fi;
fi;

if nistest ethers.org_dir.$D;
then
	echo ethers.org_dir.$D already exists;
else
	if nistbladm -D $A -c ethers_tbl addr=SI,$CA name=SI,$CA comment=,$CA ethers.org_dir.$D;
	then
		echo ethers.org_dir.$D created;
	else
		echo couldn\'t create ethers.org_dir.$D;
	fi;
fi;

if nistest hosts.org_dir.$D;
then
	echo hosts.org_dir.$D already exists;
else
	if nistbladm -D $A -c hosts_tbl cname=SI,$CA name=SI,$CA addr=SI,$CA comment=,$CA hosts.org_dir.$D;
	then
		echo hosts.org_dir.$D created;
	else
		echo couldn\'t create hosts.org_dir.$D;
	fi;
fi;

if nistest mail_aliases.org_dir.$D;
then
	echo mail_aliases.org_dir.$D already exists;
else
	if nistbladm -D $A -c mail_aliases alias=SI,$CA expansion=SI,$CA comments=,$CA options=,$CA mail_aliases.org_dir.$D;
	then
		echo mail_aliases.org_dir.$D created;
	else
		echo couldn\'t create mail_aliases.org_dir.$D;
	fi;
fi;

if nistest sendmailvars.org_dir.$D;
then
	echo sendmailvars.org_dir.$D already exists;
else
	if nistbladm -D $A -c sendmailvars key=S,$CA value=,$CA sendmailvars.org_dir.$D;
	then
		echo sendmailvars.org_dir.$D created;
	else
		echo couldn\'t create sendmailvars.org_dir.$D;
	fi;
fi;

if nistest netmasks.org_dir.$D;
then
	echo netmasks.org_dir.$D already exists;
else
	if nistbladm -D $A -c netmasks_tbl addr=SI,$CA mask=SI,$CA comment=,$CA netmasks.org_dir.$D;
	then
		echo netmasks.org_dir.$D created;
	else
		echo couldn\'t create netmasks.org_dir.$D;
	fi;
fi;

if nistest netgroup.org_dir.$D;
then
	echo netgroup.org_dir.$D already exists;
else
	if nistbladm -D $A -c netgroup_tbl name=S,$CA group=S,$CA host=SI,$CA user=S,$CA domain=SI,$CA comment=,$CA netgroup.org_dir.$D;
	then
		echo netgroup.org_dir.$D created;
	else
		echo couldn\'t create netgroup.org_dir.$D;
	fi;
fi;

if nistest networks.org_dir.$D;
then
	echo networks.org_dir.$D already exists;
else
	if nistbladm -D $A -c networks_tbl cname=SI,$CA name=SI,$CA addr=SI,$CA comment=,$CA networks.org_dir.$D;
	then
		echo networks.org_dir.$D created;
	else
		echo couldn\'t create networks.org_dir.$D;
	fi;
fi;


if nistest protocols.org_dir.$D;
then
	echo protocols.org_dir.$D already exists;
else
	if nistbladm -D $A -c protocols_tbl cname=SI,$CA name=SI,$CA number=SI,$CA comment=,$CA protocols.org_dir.$D;
	then
		echo protocols.org_dir.$D created;
	else
		echo couldn\'t create protocols.org_dir.$D;
	fi;
fi;

if nistest rpc.org_dir.$D;
then
	echo rpc.org_dir.$D already exists;
else
	if nistbladm -D $A -c rpc_tbl cname=SI,$CA name=SI,$CA number=SI,$CA comment=,$CA rpc.org_dir.$D;
	then
		echo rpc.org_dir.$D created;
	else
		echo couldn\'t create rpc.org_dir.$D;
	fi;
fi;

if nistest services.org_dir.$D;
then
	echo services.org_dir.$D already exists;
else
	if nistbladm -D $A -c services_tbl cname=SI,$CA name=SI,$CA proto=SI,$CA port=SI,$CA comment=,$CA services.org_dir.$D;
	then
		echo services.org_dir.$D created;
	else
		echo couldn\'t create services.org_dir.$D;
	fi;
fi;

if nistest timezone.org_dir.$D;
then
	echo timezone.org_dir.$D already exists;
else
	if nistbladm -D $A -c timezone_tbl name=SI,$CA tzone=,$CA comment=,$CA timezone.org_dir.$D;
	then
		echo timezone.org_dir.$D created;
	else
		echo couldn\'t create timezone.org_dir.$D;
	fi;
fi;

exit 0;
