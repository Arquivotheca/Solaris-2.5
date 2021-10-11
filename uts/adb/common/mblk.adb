#include <sys/types.h>
#include <sys/stream.h>

msgb
.>m
./"next"16t"prev"16t"cont"n{b_next,X}{b_prev,X}{b_cont,X}
+/"rptr"16t"wptr"16t"datap"n{b_rptr,X}{b_wptr,X}{b_datap,X}
+/"band"8t"flag"n{b_band,B}{b_flag,x}
*<m,*<m$<mblk.nxt
