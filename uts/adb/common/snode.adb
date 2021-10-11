#include <sys/types.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/fs/snode.h>

snode
./"next"n{s_next,X}
+$<<vnode{OFFSETOK}
+/"realvp"16t"commonvp"16t"flag"n{s_realvp,X}{s_commonvp,X}{s_flag,x}
+/"maj"8t"min"8t"fsid"16t"nextr"n{s_dev,2x}{s_fsid,X}{s_nextr,X}
+/"size"16t"atime"16t"mtime"16t"ctime"n{s_size,X}{s_atime,Y}{s_mtime,Y}{s_ctime,Y}
+/"count"16t"mapcnt"n{s_count,D}{s_mapcnt,D}
