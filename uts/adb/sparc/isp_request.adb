#include <sys/types.h/>
#include <sys/scsi/scsi.h>
#include <sys/scsi/adapters/ispmail.h>
#include <sys/scsi/adapters/ispvar.h>

isp_request
./"req header"n{req_header,4B}
+/"token"16t"target"8t"lun"8t"cdblen"8t"flags"8t"reserved"n{req_token,X}{req_target,B}{req_lun_trn,B}{req_cdblen,x}{req_flags,x}{req_reserved,x}
+/"time"8t"seg count"n{req_time,x}{req_seg_count,x}
+/"req cdb"n{req_cdb,3X}
+/"req dataseg"n{req_dataseg,8X}{END}
