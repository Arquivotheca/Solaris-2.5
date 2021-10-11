
#include <sys/kmem.h>
#include <sys/kmem_impl.h>
#include <sys/scsi/adapters/espvar.h>

esp
./"tran"16t"dev"n{e_tran,X}{e_dev,X}
+/"esp mutex:"
.$<<mutex{OFFSETOK}
+/"iblock"16t"next"16t"type"8t"idcode"8t"dmarev"n{e_iblock,X}{e_next,X}{e_type,B}{e_idcode,B}{e_dma_rev,B}
+/"conf"8t"conf2"n{e_espconf,B}{e_espconf2,B}
+/"conf3"n{e_espconf3,8B}
+/"conf3_f"8t"conf3_l"8t"conv"8t"cycle"n{e_espconf3_fastscsi,B}{e_espconf3_last,B}{e_clock_conv,B}{e_clock_cycle,x}
+/"stval"8t"sdtr"8t"stat"8t"intr"8t"step"8t"abort"8t"reset"n{e_stval,B}{e_sdtr,B}{e_stat,B}{e_intr,B}{e_step,B}{e_abort,B}{e_reset,B}
+/"lastcmd"8t"state"8t"lstate"8t"susp"n{e_last_cmd,B}{e_state,x}{e_laststate,x}{e_suspended,B}
+/"cur_msgout"n{e_cur_msgout,12B}
+/"lastmsg"8t"omsgln"8tn{e_last_msgout,B}{e_omsglen,B}
+/"imsg"n{e_imsgarea,8B}
+/"imsgln"8t"index"8t"lastmsg"n{e_imsglen,B}{e_imsgindex,B}{e_last_msgin,B}
+/"offset"n{e_offset,8B}
+/"period"n{e_period,8B}
+/"neg_period"n{e_neg_period,8B}
+/"backoff"n{e_backoff,8B}
+/"default period"n{e_default_period,8B}
+/"reqack"8t"offset"8t"period"8t"sync known"8t"nodisc"n{e_req_ack_delay,B}{e_offset_last,B}{e_period_last,B}{e_sync_known,B}8t{e_nodisc,B}
+/"weak"8t"tgts"8t"notag"8t"options"32t"scsi_opt"n{e_weak,B}{e_targets,B}{e_notag,B}{e_options,X}16t{e_scsi_options,X}
+/"target_scsi_opt"n{e_target_scsi_options,8X}
+/"tag age lim"16t"rst delay"16t"cmd area"n{e_scsi_tag_age_limit,X}{e_scsi_reset_delay,X}{e_cmdarea,X}
+/"dmaga csr"16t"dma cookie"n{e_dmaga_csr,X}{e_dmacookie,4X}
+/"dma hndle"16t"dma_atr"16t"ncmds"8t"ndisc"n{e_dmahandle,X}{e_dma_attr,X}{e_ncmds,x}{e_ndisc,x}
+/"espreg"16t"dmareg"16t"lastdma"16t"lastcount"n{e_reg,X}{e_dma,X}{e_lastdma,X}{e_lastcount,X}
+/"esc cnt"16t"dslot"8t"lstslot"8t"curslot"8t"nextslot"n{e_esc_read_count,X}{e_dslot,B}{e_last_slot,x}{e_cur_slot,x}{e_next_slot,x}
+/"slots"n{e_slots,64X}
+/"readyf"n{e_readyf,64X}
+/"readyb"n{e_readyb,64X}
+/"tagQ"n{e_tagQ,64X}
+/"throttle"n{e_throttle,64x}
+/"tcmds"n{e_tcmds,64x}
+/"reset delay"n{e_reset_delay,8X}
+/"arq pkt ptrs"n{e_arq_pkt,64X}
+/"rqsense data ptrs"n{e_rq_sense_data,64X}
+/"e_save_pkt"n{e_save_pkt,64X}
+/"cb signal"16t"cb info"n{e_callback_signal_needed,X}{e_callback_info,X}
+/"start mutex:"
.$<<mutex{OFFSETOK}
+/"startf"16t"startb"n{e_startf,X}{e_startb,X}
+/"kmem_cache"16t"reset notify"8t"restart_cmd timeid"n{e_kmem_cache,X}{e_reset_notify_listf,X}{e_restart_cmd_timeid,X}
+/"qfull retries"n{e_qfull_retries,8B}
+/"qfull retry interval"n{e_qfull_retry_interval,8x}{END}
