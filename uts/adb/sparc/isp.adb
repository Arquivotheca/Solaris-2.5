#include <sys/types.h>
#include <sys/scsi/scsi.h>
#include <sys/scsi/adapters/ispmail.h>
#include <sys/scsi/adapters/ispvar.h>

isp
./"tran"16t"dev"16t"iblock"16t"next"n{isp_tran,X}{isp_dip,X}{isp_iblock,X}{isp_next,X}
+/"major"8t"minor"8t"options"n{isp_major_rev,x}{isp_minor_rev,x}{isp_scsi_options,X}
+/"target options"n{isp_target_scsi_options,16X}
+/"tag limit"8t"reset delay"16t"hostid"8t"suspended"n{isp_scsi_tag_age_limit,X}{isp_scsi_reset_delay,X}{isp_initiator_id,B}{isp_suspended,B}
+/"cap"n{isp_cap,16x}
+/"synch"n{isp_synch,16x}
+/"ispreg"n{isp_reg,X}
+/"mbox"n{isp_mbox,44B}
+/"shutdown"16t"cmd area"n{isp_shutdown,B}{isp_cmdarea,X}
+/"dma cookie"n{isp_dmacookie,4X}
+/"dma hndl"16t"request dvma"16t"response dvma"n{isp_dmahandle,X}{isp_request_dvma,X}{isp_response_dvma,X}
+/"access hdl"16t"que space"n{isp_acc_handle,X}{isp_queue_space,X}
+/"request mutex:"
.$<<mutex{OFFSETOK}
+/"response mutex:"
.$<<mutex{OFFSETOK}
+/n"req in"8t"req out"8t"res in"8t"res out"n{isp_request_in,x}{isp_request_out,x}{isp_response_in,x}{isp_response_out,x}
+/"request ptr"16t"request base"16t"response ptr"16t"response base"n{isp_request_ptr,X}{isp_request_base,X}{isp_response_ptr,X}{isp_response_base,X}
+/"waitq mutex:"
.$<<mutex{OFFSETOK}
+/"waitf"16t"waitb"16t"waitq timeout"n{isp_waitf,X}{isp_waitb,X}{isp_waitq_timeout,X}
+/"burst sz"16t"conf1 burst"n{isp_burst_size,X}{isp_conf1_burst_flag,x}
+/"free"8t"alive"n{isp_free_slot,x}{isp_alive,x}
+/"reset list"16t"kmem cache"n{isp_reset_notify_listf,X}{isp_kmem_cache,X}{END}
