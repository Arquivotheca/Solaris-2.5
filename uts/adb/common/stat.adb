#include <sys/stat.h>

stat
./"dev"16t"ino"16t"mode"16t"link"n{st_dev,X}{st_ino,D}{st_mode,X}{st_nlink,D}
+/"uid"16t"gid"16t"rdev"16t"size"n{st_uid,D}{st_gid,D}{st_rdev,X}{st_size,D}
+/"atime.sec"16t"atime.nse"16t"mtime.sec"16t"mtime.nse"n{st_atime.tv_sec,D}{st_atime.tv_nsec,D}{st_mtime.tv_sec,D}{st_mtime.tv_nsec,D}
+/"ctime.sec"16t"ctime.nse"16t"blksize"n{st_ctime.tv_sec,D}{st_ctime.tv_nsec,D}{st_blksize,D}
