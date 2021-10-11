#include <sys/fs/cachefs_fs.h>

cachefs_metadata
.$<<vattr{OFFSETOK}
+/"cookie len"n{md_cookie.fid_len,d}
+/"cookie data"n{md_cookie.fid_data,16X}
+/"flags"16t"lruno"n{md_flags,X}{md_lruno,D}
+/"token"n{md_token,8X}
+/"fid len"n{md_fid.fid_len,d}
+/"fid data"n{md_fid.fid_data,16X}
+/"frontblks"n{md_frontblks,D}
+/"ts.sec"16t"ts.nsec"16t"gen"n{md_timestamp.tv_sec,X}{md_timestamp.tv_nsec,X}{md_gen,X}
+/"allocents"n{md_allocents,D}{END}
