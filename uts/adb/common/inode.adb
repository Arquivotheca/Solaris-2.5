#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <vm/seg.h>
#include <sys/fs/ufs_inode.h>

inode
./"forw"16t"back"n{i_chain[0],X}{i_chain[1],X}
+$<<vnode{OFFSETOK}
+/"devvp"16t"flag"8t"maj"8t"min"8t"ino"n{i_devvp,X}{i_flag,x}{i_dev,2x}{i_number,D}
+/"diroff"16t"ufsvfs"16t"dquot"n{i_diroff,X}{i_ufsvfs,X}{i_dquot,X}n"rwlock"
+$<<rwlock{OFFSETOK}
+/"contents"
.$<<rwlock{OFFSETOK}
+/"tlock"
.$<<mutex
+/"nextr"16t"freef"16t"freeb"n{i_nextr,X}{i_freef,X}{i_freeb,X}
+/"vcode"16t"mapcnt"16t"map"16t"rdev"n{i_vcode,D}{i_mapcnt,D}{i_map,X}{i_rdev,X}
+/"delaylen"16t"delayoff"16t"nextrio"16t"writes"n{i_delaylen,D}{i_delayoff,D}{i_nextrio,D}{i_writes,D}
+/"wrcv"n{i_wrcv,x}
+/"owner"16t"doff"16t16t"acl"n{i_owner,X}{i_doff,2X}{i_ufs_acl,X}
+$<<dino{OFFSETOK}
.,<9-1$<inode
