#include <sys/buf.h>

buf
./"flags"n{b_flags,X}
+/"forw"16t"back"16t"av_forw"16t"av_back"n{b_forw,X}{b_back,X}{av_forw,X}{av_back,X}
+/"bcount"16t"bufsize"16t"error"16t"edev"n{b_bcount,D}{b_bufsize,D}{b_error,D}{b_edev,X}
+/"addr"16t"blkno"16t"resid"16t"proc"n{b_un.b_addr,X}{b_blkno,X}{b_resid,D}{b_proc,X}
+/"iodone"16t"vp"16t"pages"n{b_iodone,X}{b_vp,X}{b_pages,X}
