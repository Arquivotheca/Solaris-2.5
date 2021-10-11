#include <sys/types.h>
#include <sys/thread.h>
#include <sys/stream.h>
#include <sys/strsubr.h>

stdata
./"wrq"16t"iocblk"16t"vnode"16t"strtab"n{sd_wrq,X}{sd_iocblk,X}{sd_vnode,X}{sd_strtab,X}
+/"flag"16t"iocid"n{sd_flag,X}{sd_iocid,X}
+/"sidp"16t"s_pgidp"16t"wroff"n{sd_sidp,X}{sd_pgidp,X}{sd_wroff,u}
+/"rerror"16t"werror"16t"pushcnt"n{sd_rerror,D}{sd_werror,D}{sd_pushcnt,D}
+/"sigflags"16t"siglist"n{sd_sigflags,X}{sd_siglist,X}
+$<<pollhead{OFFSETOK}
+/"mark"16t"closetime"n{sd_mark,X}{sd_closetime,X}
