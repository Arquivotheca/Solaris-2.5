#include <sys/types.h>
#include <sys/stream.h>
#include <sys/fs/fifonode.h>

fifonode
./"realvp"16t"dest"16t"mp"n{fn_realvp,X}{fn_dest,X}{fn_mp,X}
+/"count"16t"lock"16t"flag"n{fn_count,U}{fn_lock,X}{fn_flag,x}
+/"wcnt"16t"rcnt"16t"open"n{fn_wcnt,d}{fn_rcnt,d}{fn_open,d}
+/"wsynccnt"16t"rsynccnt"n{fn_wsynccnt,d}{fn_rsynccnt,d}
