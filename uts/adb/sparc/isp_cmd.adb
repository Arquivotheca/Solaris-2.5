#include <sys/scsi/scsi.h>
#include <sys/scsi/adapters/ispreg.h>
#include <sys/scsi/adapters/ispmail.h>
#include <sys/scsi/adapters/ispvar.h>
#include <sys/scsi/adapters/ispcmd.h>

isp_cmd
./"request"n{cmd_isp_request,16X}
+/"response"n{cmd_isp_response,16X}
+/"pkt"16t"forw"16t"cdbp"16t"scbp"n{cmd_pkt,X}{cmd_forw,X}{cmd_cdbp,X}{cmd_scbp,X}
+/"dmacount"16t"dmahandle"16t"dmacookie"n{cmd_dmacount,X}{cmd_dmahandle,X}{cmd_dmacookie,4X}
+/"start_time"16t"deadline"n{cmd_start_time,X}{cmd_deadline,X}
+/"cdb_un"n{cmd_cdb_un,12B}
+/"flags"16t"slot"8t"cdblen"16t"scblen"16t"privlen"n{cmd_flags,X}{cmd_slot,x}{cmd_cdblen,X}{cmd_scblen,X}{cmd_privlen,X}
+/"pkt_private"n{cmd_pkt_private,8B}{END}
