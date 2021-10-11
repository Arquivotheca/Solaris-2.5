#include <sys/param.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/dumphdr.h>

dumphdr
./"magic"16t"version"16t"flags"n{dump_magic,X}{dump_version,X}{dump_flags,X}
+/"pagesize"16t"chunksize"16t"bitmapsize"n{dump_pagesize,X}{dump_chunksize,X}{dump_bitmapsize,X}
+/"nchunks"16t"dumpsize"n{dump_nchunks,X}{dump_dumpsize,X}
+/"versionoff"16t"panicstringoff"16t"headersize"n{dump_versionoff,X}{dump_panicstringoff,X}{dump_headersize,X}
