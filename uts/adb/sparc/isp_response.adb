#include <sys/types.h>
#include <sys/scsi/scsi.h>
#include <sys/scsi/adapters/ispmail.h>
#include <sys/scsi/adapters/ispvar.h>

isp_response
./"req header"n{resp_header,4B}
+/"token"16t"scb"8t"reason"8t"state"8t"status"n{resp_token,X}{resp_scb,x}{resp_reason,x}{resp_state,x}{resp_status_flags,x}
+/"time"8t"rqs cnt"8t"resid"n{resp_time,x}{resp_rqs_count,x}{resp_resid,X}
+/"response reserved"n{resp_reserved,2X}
+/"request sense"n{resp_request_sense,8X}{END}
