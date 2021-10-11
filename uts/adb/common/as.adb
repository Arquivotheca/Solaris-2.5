#include <sys/param.h>
#include <sys/types.h>
#include <vm/as.h>

as
./"contents (mutex)"
.$<<mutex{OFFSETOK}
+/"FLAGS"8t"vbits"8t"cv"8t"hat             hrm"nB{OFFSETOK}{a_vbits,B}{a_cv,x}{a_hat,X}{a_hrm,X}
+/"rss"16t"seglast"n{a_rss,X}{a_cache,X}n"lock (rwlock)"
+$<<rwlock{OFFSETOK}
+/"segs"16t"size"16t"tail"n{a_segs,X}{a_size,X}{a_tail,X}
+/"nsegs"8t"lrep"8t"hilevel"n{a_nsegs,D}{a_lrep,b}{a_hilevel,b}
