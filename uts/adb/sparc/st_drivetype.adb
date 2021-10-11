#include <sys/scsi/scsi.h>
#include <sys/mtio.h>
#include <sys/scsi/targets/stdef.h>
#include <sys/file.h>
#include <sys/stat.h>

st_drivetype
./n"tape name"n{name,X}
+/n"vendor_len"8t"vendor id"n{length,B}8t{vid,24c}
+/n"type"8t"bsize"16t"options"n{type,B}{bsize,X}{options,X}
+/n"max_rretries"16t"max_wretries"n{max_rretries,X}{max_wretries,X}
+/n"densities"n{densities,4B}
+/n"default_density"n{default_density,B}
+/n"speeds"n{speeds,4B}{END}
