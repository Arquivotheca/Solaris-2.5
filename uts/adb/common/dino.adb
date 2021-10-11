#ifndef _KERNEL
#define _KERNEL
#endif
#include <sys/types.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/thread.h>
#include <vm/seg.h>
#include <sys/fs/ufs_inode.h>

icommon
./"ic_smode"16t"ic_nlink"16t"ic_suid"16t"ic_sgid"n{ic_smode,o}16t{ic_nlink,d}16t{ic_suid,d}16t{ic_sgid,d}
+/"ic_size"16t16t"ic_atime.sec"16t"ic_atime.usec"n{ic_size,2X}{ic_atime.tv_sec,X}{ic_atime.tv_usec,X}
+/"ic_mtime.sec"16t"ic_mtime.usec"16t"ic_ctime.sec"16t"ic_ctime.usec"n{ic_mtime,2X}{ic_ctime,2X}
+/"direct blocks"
+/{ic_db,12X}
+/"indirect blocks"
+/{ic_ib,3X}
+/"ic_flags"16t"ic_blocks"16t"ic_gen"16t"ic_shadow"n{ic_flags,X}{ic_blocks,D}{ic_gen,D}{ic_shadow,X}
+/"ic_uid"16t"ic_gid"16t"ic_oeftflag"n{ic_uid,D}{ic_gid,D}{ic_oeftflag,X}{END}
