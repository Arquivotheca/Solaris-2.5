#ifndef lint
static char sccsid[] = "@(#)token.c 1.8 93/02/03 SMI";
#endif
 
/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <bsm/audit.h>
#include <bsm/audit_record.h>
#include <bsm/libbsm.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mkdev.h>
#include <pwd.h>
#include <grp.h>
#include <netdb.h>

#include "praudit.h"

extern char *sys_errlist[];
extern int  sys_nerr;

static int au_fetch_char();
static int au_fetch_short();
static int au_fetch_int();
static int au_fetch_bytes();
static char *get_Hname();
static void convertascii();
static int convert_char_to_string();
static int convert_int_to_string();
static int convertbinary();
static char *hexconvert();
static char *pa_gettokenstring();

/* au_read_rec:
 *	If the file pointer or the record buffer passed in are NULL,
 *	free up the static space and return an error code < 0.
 *	Otherwise, attempt to read an audit record from the file pointer.
 *
 *	If successful:
 *		Set recbuf to the pointer to the space holding the record.
 *		Advance in the stream (fp).
 *		Return 0.
 *
 *	If failed:
 *		Don't alter recbuf.
 *		Don't advance the stream.
 *		Return error code < 0.
 */

#ifdef __STDC__
int au_read_rec(FILE *fp, char **recbuf)
#else
int au_read_rec(fp, recbuf)
	FILE *fp;
	char **recbuf;
#endif
{
	static char *p_space = NULL; /* pointer to a record buffer */
	static int cur_size = 0; /* size of p_space in bytes */
	
	adr_t adr;		/* pointer to an audit trail */
	char tokenid;		/* token (attribute) identifier */
	u_long record_size;	/* length of a header attribute record */
	u_short name_len;	/* length of a file attribute record */
	long start_pos;		/* initial position in fp */
	int new_size;		/* size of the new static space in bytes */
	char date_time[8];	/* date & time of file attribute */

	if (fp == NULL || recbuf == NULL) {
		cur_size = 0;
		free(p_space);
		return -1;
	}

	/* Use the adr routines for reading the audit trail.
	 * They have a bit of overhead, but the already do
	 * the byte stream conversions that we will need.
	 */
	adrf_start(&adr, fp);

	/* Save the current position in the file.
	 * We`ll need to back up to here before
	 * reading in the entire record.
	 */
	start_pos = ftell(fp);

	/* Determine the amount of space needed for the record... */

	/* Skip passed the token id */
	if (adrf_char(&adr, &tokenid, 1) != 0) {
		return -2;
	}

	/* Read in the size of the record */
	if (tokenid == AUT_HEADER) {
		if (adrf_u_long(&adr, &record_size, 1) != 0) {
			fseek(fp, start_pos, SEEK_SET);
			return -4;
		}
	} else if (tokenid == AUT_OTHER_FILE) {
		if (adrf_char(&adr, date_time, 8) != 0) {
			fseek(fp, start_pos, SEEK_SET);
			return -5;
		}
		if (adrf_u_short(&adr, &name_len, 1) != 0) {
			fseek(fp, start_pos, SEEK_SET);
			return -6;
		}
		/* 11 is the size of an attr id, date & time, and name length */
		record_size = (u_long)name_len + 11;
	} else {
		fseek(fp, start_pos, SEEK_SET);
		return -7;
	}

	/* Go back to the starting point so we can read in the entire record */
	fseek(fp,start_pos,SEEK_SET);

	/* If the current size of the static p_space cannot hold
	 * the entire record, make it larger.
	 */
	new_size = cur_size;
	while (new_size < record_size) {
		new_size += 512;
		/* If we need more than a megabyte to hold a single record
		 * something is amiss.
		 */
		if (new_size > 1000000) {
			return -8;
		}
	}
	if (new_size != cur_size) {
		cur_size = 0;
		free(p_space);
		if ((p_space = (char *)malloc(new_size)) == NULL) {
			return -9;
		}
		cur_size = new_size;
	}

	/* Do what we came here for; read an audit record */
	if (fread(p_space, record_size, 1, fp) != 1) {
		fseek(fp,start_pos,SEEK_SET);
		return -10;
	}

	/* Pad the buffer with zeroes */
	memset(p_space + record_size, '\0', cur_size - record_size);

	*recbuf = (char *)p_space;
	return 0;
}

/* au_fetch_tok():
 *
 * Au_fetch_tok() behaves like strtok(3).  On the first call, a buffer
 * is passed in.  On subsequent calls, NULL is passed in as buffer.
 * Au_fetch_tok() manages the buffer pointer offset and returns tokens
 * until the end of the buffer is reached.  The user of the routine must
 * guarantee that the buffer starts with and contains at least one full
 * audit record.  This type of assurance is provided by au_read_rec().
 */

#ifdef __STDC__
int au_fetch_tok(au_token_t *tok, char *buf, int flags)
#else
int au_fetch_tok(tok, buf, flags)
	au_token_t *tok;
	char *buf;
	int flags;
#endif
{
	static char *invalid_txt = "invalid token id";
	static int len_invalid_txt = 17;
	static char *old_buf = NULL; /* position in buf at end of last fetch */
	char *orig_buf; /* position in buf when fetch entered */
	char *cur_buf; /* current location in buf */
	int length;
	int i;
	int valid_id;

	/* Check flags, one should be on.  */
	i = 0;
	if (flags & AUF_POINT) {
		i++;
	}
	if (flags & AUF_DUP) {
		i++;
	}
	if (flags & AUF_COPY_IN) {
		i++;
	}
	if (i != 1) {
		return -1;
	}
	/* Skip not implemented, yet */
	if (flags & AUF_SKIP) {
		return -2;
	}

	if (buf == NULL) {
		orig_buf = old_buf;
		cur_buf = old_buf;
	} else {
		orig_buf = buf;
		cur_buf = buf;
	}

	tok->data = cur_buf;
	au_fetch_char(&tok->id, &cur_buf, flags);
	tok->next = NULL;
	tok->prev = NULL;

	valid_id = 1;

	switch(tok->id) {
		case AUT_OTHER_FILE:
			(void)au_fetch_int(&tok->un.file.time,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.file.msec,&cur_buf,flags);
			(void)au_fetch_short(&tok->un.file.length,&cur_buf,flags);
			(void)au_fetch_bytes(&tok->un.file.fname,&cur_buf,tok->un.file.length,flags);
			tok->size = 11 + tok->un.file.length;
		break;
		case AUT_HEADER:
			(void)au_fetch_int(&tok->un.header.length,&cur_buf,flags);
			(void)au_fetch_char(&tok->un.header.version,&cur_buf,flags);
			(void)au_fetch_short(&tok->un.header.event,&cur_buf,flags);
			(void)au_fetch_short(&tok->un.header.emod,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.header.time,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.header.msec,&cur_buf,flags);
			tok->size = 16;
		break;
		case AUT_TRAILER:
			(void)au_fetch_short(&tok->un.trailer.magic,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.trailer.length,flags);
			tok->size = 7;
		break;
		case AUT_DATA:
			(void)au_fetch_char(&tok->un.data.pfmt,&cur_buf,flags);
			(void)au_fetch_char(&tok->un.data.size,&cur_buf,flags);
			(void)au_fetch_char(&tok->un.data.number,&cur_buf,flags);
			length = (int)tok->un.data.size * (int)tok->un.data.number;
			(void)au_fetch_bytes(&tok->un.data.data,&cur_buf,length,flags);
			tok->size = 4 + length;
		break;
		case AUT_IPC:
			(void)au_fetch_int(&tok->un.ipc.id,&cur_buf,flags);
			tok->size = 5;
		break;
		case AUT_PATH:
			(void)au_fetch_short(&tok->un.path.length,&cur_buf,flags);
			(void)au_fetch_bytes(&tok->un.path.name,&cur_buf,tok->un.path.length,flags);
			tok->size = 3 + tok->un.path.length;
		break;
		case AUT_SUBJECT:
			(void)au_fetch_int(&tok->un.subj.auid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.subj.euid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.subj.egid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.subj.ruid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.subj.rgid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.subj.pid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.subj.sid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.subj.tid.port,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.subj.tid.machine,&cur_buf,flags);
			tok->size = 37;
		break;
		case AUT_SERVER:
			(void)au_fetch_int(&tok->un.server.auid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.server.euid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.server.ruid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.server.egid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.server.pid,&cur_buf,flags);
			tok->size = 21;
		break;
		case AUT_PROCESS:
			(void)au_fetch_int(&tok->un.proc.auid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.proc.euid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.proc.ruid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.proc.rgid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.proc.pid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.proc.sid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.proc.tid.port,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.proc.tid.machine,&cur_buf,flags);
			tok->size = 33;
		break;
		case AUT_RETURN:
			(void)au_fetch_char(&tok->un.ret.error,&cur_buf,flags);
			(void)au_fetch_char(&tok->un.ret.retval,&cur_buf,flags);
			tok->size = 6;
		break;
		case AUT_TEXT:
			(void)au_fetch_short(&tok->un.text.length,&cur_buf,flags);
			(void)au_fetch_bytes(&tok->un.text.data,&cur_buf, tok->un.text.length,flags);
			tok->size = 3 + tok->un.text.length;
		break;
		case AUT_OPAQUE:
			(void)au_fetch_short(&tok->un.opaque.length,&cur_buf,flags);
			(void)au_fetch_bytes(&tok->un.opaque.data,&cur_buf,tok->un.opaque.length,flags);
			tok->size = 3 + tok->un.opaque.length;
		break;
		case AUT_IN_ADDR:
			(void)au_fetch_int(&tok->un.inaddr.ia.s_addr,&cur_buf,flags);
			tok->size = 5;
		break;
		case AUT_IP:
			(void)au_fetch_char(&tok->un.ip.version,&cur_buf,flags);
			(void)au_fetch_char(&tok->un.ip.ip.ip_tos,&cur_buf,flags);
			(void)au_fetch_short(&tok->un.ip.ip.ip_len,&cur_buf,flags);
			(void)au_fetch_short(&tok->un.ip.ip.ip_id,&cur_buf,flags);
			(void)au_fetch_short(&tok->un.ip.ip.ip_off,&cur_buf,flags);
			(void)au_fetch_char(&tok->un.ip.ip.ip_ttl,&cur_buf,flags);
			(void)au_fetch_char(&tok->un.ip.ip.ip_p,&cur_buf,flags);
			(void)au_fetch_short(&tok->un.ip.ip.ip_sum,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.ip.ip.ip_src.s_addr,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.ip.ip.ip_dst.s_addr,&cur_buf,flags);
			tok->size = 21;
		break;
		case AUT_IPORT:
			(void)au_fetch_short(&tok->un.iport.iport,&cur_buf,flags);
			tok->size = 3;
		break;
		case AUT_ARG:
			(void)au_fetch_char(&tok->un.arg.num,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.arg.val,&cur_buf,flags);
			(void)au_fetch_short(&tok->un.arg.length,&cur_buf,flags);
			(void)au_fetch_bytes(&tok->un.arg.data,&cur_buf,tok->un.arg.length,flags);
			tok->size = 7 + tok->un.arg.length;
		break;
		case AUT_SOCKET:
			(void)au_fetch_short(&tok->un.socket.type,&cur_buf,flags);
			(void)au_fetch_short(&tok->un.socket.lport,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.socket.laddr.s_addr,&cur_buf,flags);
			(void)au_fetch_short(&tok->un.socket.fport,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.socket.faddr.s_addr,&cur_buf,flags);
			tok->size = 15;
		break;
		case AUT_SEQ:
			(void)au_fetch_int(&tok->un.seq.num,&cur_buf,flags);
			tok->size = 5;
		break;
		case AUT_ATTR:
			(void)au_fetch_int(&tok->un.attr.mode,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.attr.uid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.attr.gid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.attr.fs,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.attr.node,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.attr.dev,&cur_buf,flags);
			tok->size = 25;
		break;
		case AUT_IPC_PERM:
			(void)au_fetch_int(&tok->un.ipc_perm.ipc_perm.uid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.ipc_perm.ipc_perm.gid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.ipc_perm.ipc_perm.cuid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.ipc_perm.ipc_perm.cgid,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.ipc_perm.ipc_perm.mode,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.ipc_perm.ipc_perm.seq,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.ipc_perm.ipc_perm.key,&cur_buf,flags);
			tok->size = 29;
		break;
		case AUT_GROUPS:
			for (i=0; i<NGROUPS_MAX; i++) {
				(void)au_fetch_int(&tok->un.groups.groups[i],&cur_buf,flags);
			}
			tok->size = 1 + (NGROUPS_MAX * sizeof(gid_t));
		break;
		case AUT_EXIT:
			(void)au_fetch_int(&tok->un.exit.status,&cur_buf,flags);
			(void)au_fetch_int(&tok->un.exit.retval,&cur_buf,flags);
			tok->size = 9;
		break;
		case AUT_INVALID:
		default:
			au_fetch_bytes(&tok->un.invalid.data, &invalid_txt, len_invalid_txt, flags);
			tok->un.invalid.length = len_invalid_txt;
			tok->size = len_invalid_txt;
			valid_id = 0;
	}
	if (valid_id == 0) {
		old_buf = orig_buf;
		return -3;
	}
	old_buf = cur_buf;
	return 0;
}

static int au_fetch_char(result, buf, flags)
	char *result;
	char **buf;
	int flags;
{
	*result = **buf;
	(*buf)++;
	return 0;
}

static int au_fetch_short(result, buf, flags)
	short *result;
	char **buf;
	int flags;
{
	*result = **buf << 8;
	(*buf)++;
	*result |= **buf & 0x0ff;
	(*buf)++;
	return 0;
}

static int au_fetch_int(result, buf, flags)
	int *result;
	char **buf;
	int flags;
{
	int i;

	for (i=0; i<sizeof(int); i++) {
		*result <<= 8;
		*result |= **buf & 0x000000ff;
		(*buf)++;
	}
	return 0;
}

static int au_fetch_bytes(result, buf, len, flags)
	char **result;
	char **buf;
	int len;
	int flags;
{
	if (flags & AUF_POINT) {
		*result = *buf;
		(*buf) += len;
		return 0;
	}
	if (flags & AUF_DUP) {
		*result = (char *)malloc(len);
		memcpy(*result, *buf, len);
		(*buf) += len;
		return 0;
	}
	if (flags & AUF_COPY_IN) {
		memcpy(*result, *buf, len);
		(*buf) += len;
		return 0;
	}
	return -1;
}

#ifdef __STDC__
au_fprint_tok(FILE *fp, au_token_t *tok, char *b, char *m, char *e, int flags)
#else
au_fprint_tok(fp, tok, b, m, e, flags)
	FILE *fp;
	au_token_t *tok;
	char *b, *m, *e;
	int flags;
#endif
{
	char *s1, *s2;
	char s3[80], s4[80];
	char p[80];
	au_event_ent_t *p_event;
	int i;
	char *p_data;
	char c1;
	short c2;
	int c3;
	struct in_addr ia;
	char *hostname;
	char *ipstring;
	struct passwd *p_pwd;
	struct group *p_grp;

	if (flags == 0) switch(tok->id) {
		case AUT_OTHER_FILE:
			s1 = ctime(&tok->un.file.time);
			s1[24] = '\0';
			fprintf(fp,"%s%s%s%s%s + %d msec%s",
				b,"file",m,
				s1,m,
				tok->un.file.msec,e
			);
			return 0;
		break;
		case AUT_HEADER:
			s1 = ctime(&tok->un.header.time);
			s1[24] = '\0';
			i=cacheauevent(&p_event, tok->un.header.event);
			fprintf(fp,"%s%s%s%d%s%d%s%s%s%d%s%s%s + %d msec%s",
				b,"header",m,
				tok->un.header.length,m,
				tok->un.header.version,m,
				p_event->ae_desc,m,
				tok->un.header.emod,m,
				s1,m,
				tok->un.header.msec,e
			);
			free(s1);
			return 0;
		case AUT_TRAILER:
			if (tok->un.trailer.magic != AUT_TRAILER_MAGIC) {
				return -2;
			}
			fprintf(fp,"%s%s%s%d%s",
				b,"trailer",m,
				tok->un.trailer.length,e
			);
			return 0;
		case AUT_DATA:
			switch(tok->un.data.pfmt) {
				case AUP_BINARY:
					s1 = "binary";
					break;
				case AUP_OCTAL:
					s1 = "octal";
					break;
				case AUP_DECIMAL:
					s1 = "decimal";
					break;
				case AUP_HEX:
					s1 = "hex";
					break;
				case AUP_STRING:
					s1 = "string";
					break;
				default:
					s1 = "unknown print suggestion";
					break;
			}
			switch(tok->un.data.size) {
				/* case AUR_BYTE: */
				case AUR_CHAR:
					s2 = "char";
					break;
				case AUR_SHORT:
					s2 = "short";
					break;
				case AUR_INT:
					s2 = "int";
					break;
				case AUR_LONG:
					s2 = "long";
					break;
				default:
					s2 = "unknown basic unit type";
					break;
			}
			fprintf(fp, "%s%s%s%s%s%s%s", b, "data", m, s1, m, s2, m);

			p_data = tok->un.data.data;
			for (i=1; i <= (int)tok->un.data.number; i++) {
				switch(tok->un.data.size) {
					case AUR_CHAR:
						if (au_fetch_char(&c1, &p_data, 0) == 0) {
							convert_char_to_string(tok->un.data.pfmt,c1,p);
						} else {
							return -3;
						}
					break;
					case AUR_SHORT:
						if (au_fetch_short(&c2, &p_data, 0) == 0) {
							convert_short_to_string(tok->un.data.pfmt,c2,p);
						} else {
							return -4;
						}
					break;
					case AUR_INT:
					case AUR_LONG:
						if (au_fetch_int(&c3, &p_data, 0) == 0) {
							convert_int_to_string(tok->un.data.pfmt,c3,p);
						} else {
							return -5;
						}
					break;
					default:
						return -6;
						break;
				}
				fprintf(fp, "%s%s", p, i == tok->un.data.number ? m : e);
			}
			return 0;
		case AUT_IPC:
			fprintf(fp, "%s%s%s%d%s",
				b, "IPC", m,
				tok->un.ipc.id, e
			);
			return 0;
		case AUT_PATH:
			fprintf(fp, "%s%s%s%s%s",
				b, "path", m,
				tok->un.path.name, e
			);
			return 0;
		case AUT_SUBJECT:
			printf("subject\n");
			hostname = get_Hname(tok->un.subj.tid.machine);
			ia.s_addr = tok->un.subj.tid.machine;
			if ((s1 = inet_ntoa(ia)) == NULL) {
				s1 = "bad machine id";
			}
			fprintf(fp, "%s%s%s%d%s%d%s%d%s%d%s%d%s%d%s%d%s%d%s%d%s%s%s%s%s",
				b, "subject", m,
				tok->un.subj.auid, m,
				tok->un.subj.euid, m,
				tok->un.subj.egid, m,
				tok->un.subj.ruid, m,
				tok->un.subj.rgid, m,
				tok->un.subj.pid, m,
				tok->un.subj.sid, m,
				major((dev_t)tok->un.subj.tid.port), m,
				minor((dev_t)tok->un.subj.tid.port), m,
				hostname, m,
				s1, e
			);
			return 0;
		case AUT_SERVER:
			fprintf(fp, "%s%s%s%d%s%d%s%d%s%d%s%d%s",
				b, "server", m,
				tok->un.server.auid, m,
				tok->un.server.euid, m,
				tok->un.server.ruid, m,
				tok->un.server.egid, m,
				tok->un.server.pid, e
			);
			return 0;
		case AUT_PROCESS:
			hostname = get_Hname(tok->un.proc.tid.machine);
			ia.s_addr = tok->un.proc.tid.machine;
			if ((s1 = inet_ntoa(ia)) == NULL) {
				s1 = "bad machine id";
			}
			fprintf(fp, "%s%s%s%d%s%d%s%d%s%d%s%d%s%d%s%d%s%d%s%d%s%s%s%s%s",
				b, "process", m,
				tok->un.proc.auid, m,
				tok->un.proc.euid, m,
				tok->un.proc.egid, m,
				tok->un.proc.ruid, m,
				tok->un.proc.rgid, m,
				tok->un.proc.pid, m,
				tok->un.proc.sid, m,
				major((dev_t)tok->un.proc.tid.port), m,
				minor((dev_t)tok->un.proc.tid.port), m,
				hostname, m,
				s1, e
			);
			return 0;
		case AUT_RETURN:
			if (tok->un.ret.error == 0) {
				(void) strcpy(s3, "success");
			} else if (tok->un.ret.error == -1) {
				(void) strcpy(s3, "failure");
			} else {
				if (tok->un.ret.error < (u_char)sys_nerr) {
					sprintf(s3, "failure: %s", sys_errlist[tok->un.ret.error]);
				} else {
					(void) strcpy(s3, "Unknown errno");
				}
			}
			
			fprintf(fp,"%s%s%s",
				b, "return", m,
				s3, m,
				tok->un.ret.retval, e
			);
			return 0;
		case AUT_TEXT:
			fprintf(fp,"%s%s%s%s%s",
				b, "text", m,
				tok->un.text.data, e
			);
			return 0;
		case AUT_OPAQUE:
			s1 = hexconvert(tok->un.opaque.data,tok->un.opaque.length,0);
			fprintf(fp,"%s%s%s%s%s",
				b, "opaque", m,
				s1, e
			);
			free(s1);
			return 0;
		case AUT_IN_ADDR:
			s1 = get_Hname(tok->un.inaddr.ia);
			fprintf(fp, "%s%s%s%s%s",
				b, "ip address", m,
				s1, e
			);
			return 0;
		case AUT_IP:
			fprintf(fp, "%s%s%s%x%s%x%s%d%s%d%s%d%s%x%s%x%s%d%s%x%s%x%s",
				b, "ip", m,
				(int)tok->un.ip.version, m,
				(int)tok->un.ip.ip.ip_tos, m,
				tok->un.ip.ip.ip_len, m,
				tok->un.ip.ip.ip_id, m,
				tok->un.ip.ip.ip_off, m,
				(int)tok->un.ip.ip.ip_ttl, m,
				(int)tok->un.ip.ip.ip_p, m,
				tok->un.ip.ip.ip_sum, m,
				tok->un.ip.ip.ip_src, m,
				tok->un.ip.ip.ip_dst, e
			);
			return 0;
		case AUT_IPORT:
			fprintf(fp, "%s%s%s%x%s",
				b, "ip port", m,
				(int)tok->un.iport.iport, e
			);
			return 0;
		case AUT_ARG:
			fprintf(fp, "%s%s%s%d%s%x%s%s%s",
				b, "argument", m,
				tok->un.arg.num, m,
				tok->un.arg.val, m,
				tok->un.arg.data, e
			);
			return 0;
		case AUT_SOCKET:
			s1 = get_Hname(tok->un.socket.laddr);
			s2 = get_Hname(tok->un.socket.faddr);
			fprintf(fp, "%s%s%s%x%s%x%s%s%s%x%s%s%s",
				b, "socket", m,
				(int)tok->un.socket.type, m,
				(int)tok->un.socket.lport, m,
				s1, m,
				(int)tok->un.socket.fport, m,
				s2, e
			);
			free(s1);
			free(s2);
			return 0;
		case AUT_SEQ:
			fprintf(fp, "%s%s%s%d%s",
				b, "sequence", m,
				tok->un.seq.num, e
			);
			return 0;
		case AUT_ATTR:
			setpwent();
			if ((p_pwd = getpwuid(tok->un.attr.uid)) == NULL) {
				sprintf(s3, "%d", tok->un.attr.uid);
			} else {
				(void)strcpy(s3, p_pwd->pw_name);
			}
			endpwent();
			setgrent();
			if ((p_grp = getgrgid(tok->un.attr.gid)) == NULL) {
				sprintf(s4, "%d", tok->un.attr.gid);
			} else {
				(void)strcpy(s4, p_grp->gr_name);
			}
			endgrent();
			fprintf(fp, "%s%s%s%o%s%s%s%s%s%d%s%d%s%u%s",
				b, "attribute", m,
				tok->un.attr.mode, m,
				s3, m,
				s4, m,
				tok->un.attr.fs, m,
				tok->un.attr.node, m,
				tok->un.attr.dev, e
			);
			return 0;
		case AUT_IPC_PERM:
			setpwent();
			if ((p_pwd = getpwuid(tok->un.ipc_perm.ipc_perm.uid)) == NULL) {
				sprintf(s3, "%d", tok->un.ipc_perm.ipc_perm.uid);
			} else {
				(void)strcpy(s3, p_pwd->pw_name);
			}
			endpwent();
			setgrent();
			if ((p_grp = getgrgid(tok->un.ipc_perm.ipc_perm.gid)) == NULL) {
				sprintf(s4, "%d", tok->un.ipc_perm.ipc_perm.gid);
			} else {
				(void)strcpy(s4, p_grp->gr_name);
			}
			endgrent();
			fprintf(fp, "%s%s%s%s%s%s%s%s%s%s%s%o%s%d%s%x%s",
				b, "IPC perm", m,
				s3, m,
				s4, m
			);
			setpwent();
			if ((p_pwd = getpwuid(tok->un.ipc_perm.ipc_perm.cuid)) == NULL) {
				sprintf(s3, "%d", tok->un.ipc_perm.ipc_perm.cuid);
			} else {
				(void)strcpy(s3, p_pwd->pw_name);
			}
			endpwent();
			setgrent();
			if ((p_grp = getgrgid(tok->un.ipc_perm.ipc_perm.cgid)) == NULL) {
				sprintf(s4, "%d", tok->un.ipc_perm.ipc_perm.cgid);
			} else {
				(void)strcpy(s4, p_grp->gr_name);
			}
			endgrent();
			fprintf(fp, "%s%s%s%s%o%s%d%s%x%s",
				s3, m,
				s4, m,
				tok->un.ipc_perm.ipc_perm.mode, m,
				tok->un.ipc_perm.ipc_perm.seq, m,
				tok->un.ipc_perm.ipc_perm.key, e
			);
			return 0;
		case AUT_GROUPS:
			fprintf(fp, "%s%s%s",
				b, "group", m
			);
			for (i=0; i<NGROUPS_MAX; i++) {
				setgrent();
				if ((p_grp = getgrgid(tok->un.groups.groups[i])) == NULL) {
					sprintf(s4, "%d", tok->un.groups.groups[i]);
				} else {
					(void)strcpy(s4, p_grp->gr_name);
				}
				endgrent();
				fprintf(fp, "%s%s", s3, i == NGROUPS_MAX - 1 ? m : e);
			}
		case AUT_EXIT:
			if (tok->un.exit.retval < -1 && tok->un.exit.retval < sys_nerr) {
				sprintf(s3, "%s", sys_errlist[tok->un.exit.retval]);
			} else {
				sprintf(s3, "%s", "Unknown errno");
			}
			fprintf(fp, "%s%s%s%s%s%d%s",
				b, "exit", m,
				s3, m,
				tok->un.exit.status, e
			);
			return 0;
		case AUT_INVALID:
		default:
			fprintf(fp, "%s%s%s",
				b, "invalid token", e
			);
			return(-1);
		break;
	} 
}

		
au_fprint_tok_hex(fp, tok, b, m, e, flags)
	FILE *fp;
	au_token_t *tok;
	char b, m, e;
	int flags;
{
	char *str;
	char *prefix;

	str = hexconvert(tok->data, tok->size, tok->size);

	switch (tok->id) {
		case AUT_ARG:
			prefix = "arg";
			break;
		case AUT_ATTR:
			prefix = "attr";
			break;
		case AUT_DATA:
			prefix = "data";
			break;
		case AUT_EXIT:
			prefix = "exit";
			break;
		case AUT_GROUPS:
			prefix = "groups";
			break;
		case AUT_HEADER:
			prefix = "header";
			break;
		case AUT_INVALID:
			prefix = "invalid";
			break;
		case AUT_IN_ADDR:
			prefix = "in_addr";
			break;
		case AUT_IP:
			prefix = "ip";
			break;
		case AUT_IPC:
			prefix = "ipc";
			break;
		case AUT_IPC_PERM:
			prefix = "ipc_perm";
			break;
		case AUT_IPORT:
			prefix = "iport";
			break;
		case AUT_OPAQUE:
			prefix = "opaque";
			break;
		case AUT_OTHER_FILE:
			prefix = "file";
			break;
		case AUT_PATH:
			prefix = "path";
			break;
		case AUT_PROCESS:
			prefix = "process";
			break;
		case AUT_RETURN:
			prefix = "return";
			break;
		case AUT_SEQ:
			prefix = "seq";
			break;
		case AUT_SERVER:
			prefix = "server";
			break;
		case AUT_SOCKET:
			prefix = "socket";
			break;
		case AUT_SUBJECT:
			prefix = "subject";
			break;
		case AUT_TEXT:
			prefix = "text";
			break;
		case AUT_TRAILER:
			prefix = "trailer";
			break;
		default:
			prefix = "invalid";
			break;
	}
	fprintf(fp,"%s:%s\n", prefix, str);
}

/*
 * Convert binary data to ASCII for printing.
 */
static void
convertascii(p,c,size)
register char *p;
register char *c;
register int size;
{
	register int i;

	for (i=0; i<size; i++) {
		*(c+i) = (char)toascii(*(c+i));
		if ((int)iscntrl(*(c+i))) {
			*p++ = '^';
			*p++ = (char)(*(c+i)+0x40);
		} else
			*p++ = *(c+i);
	}

	*p = '\0';

	return;
}

/* =========================================================
   convert_char_to_string:
   Converts a byte to string depending on the print mode
   input	: printmode, which may be one of AUP_BINARY,
		  AUP_OCTAL, AUP_DECIMAL, and AUP_HEX
		  c, which is the byte to convert
   output	: p, which is a pointer to the location where
		  the resulting string is to be stored
   ========================================================== */

static int
convert_char_to_string (printmode, c, p)
char printmode;
char c;
char *p;
{
	union {
		char c1[4];
		long c2;
	} dat;

	dat.c2 = 0;
	dat.c1[3] = c;

	if (printmode == AUP_BINARY)
		convertbinary (p,&c,sizeof(char));
	else if (printmode == AUP_OCTAL)
		sprintf(p,"%o",dat.c2);
	else if (printmode == AUP_DECIMAL)
		sprintf(p,"%d",c);
	else if (printmode == AUP_HEX)
		sprintf(p,"0x%x",dat.c2);
	else if (printmode == AUP_STRING)
		convertascii (p, &c, sizeof(char));
	return 0;
}

/* ==============================================================
   convert_short_to_string:
   Converts a short integer to string depending on the print mode
   input	: printmode, which may be one of AUP_BINARY,
		  AUP_OCTAL, AUP_DECIMAL, and AUP_HEX
		  c, which is the short integer to convert
   output	: p, which is a pointer to the location where
		  the resulting string is to be stored
   =============================================================== */
static int
convert_short_to_string (printmode, c, p)
char printmode;
short c;
char *p;
{
	union {
		short c1[2];
		long c2;
	} dat;

	dat.c2 = 0;
	dat.c1[1] = c;

	if (printmode == AUP_BINARY)
		convertbinary (p,&c,sizeof(short));
	else if (printmode == AUP_OCTAL)
		sprintf(p,"%o",dat.c2);
	else if (printmode == AUP_DECIMAL)
		sprintf(p,"%hd",c);
	else if (printmode == AUP_HEX)
		sprintf(p,"0x%x",dat.c2);
	else if (printmode == AUP_STRING)
		convertascii (p,&c,sizeof(short));
	return 0;
}

/* =========================================================
   convert_int_to_string:
   Converts a integer to string depending on the print mode
   input	: printmode, which may be one of AUP_BINARY,
		  AUP_OCTAL, AUP_DECIMAL, and AUP_HEX
		  c, which is the integer to convert
   output	: p, which is a pointer to the location where
		  the resulting string is to be stored
   ========================================================== */
static int
convert_int_to_string (printmode, c, p)
char printmode;
long c;
char *p;
{
	if (printmode == AUP_BINARY)
		convertbinary (p,&c,sizeof(int));
	else if (printmode == AUP_OCTAL)
		sprintf(p,"%o",c);
	else if (printmode == AUP_DECIMAL)
		sprintf(p,"%d",c);
	else if (printmode == AUP_HEX)
		sprintf(p,"0x%x",c);
	else if (printmode == AUP_STRING)
		convertascii (p,&c,sizeof(int));
	return 0;
}

/* ===========================================================
   convertbinary:
   Converts a unit c of 'size' bytes long into a binary string 
   and returns it into the position pointed to by p
   ============================================================ */
static int
convertbinary (p, c,size)
char *p;
char *c;
int size;
{
	char *s, *t;
	int i, j;

	if ((s = (char *)malloc(8*size + 1)) == NULL)
		return (0);

	/* first convert to binary */
	t = s;
	for (i=0;i<size;i++) {
		for (j=0;j<8;j++)
			sprintf(t++,"%d", ((*c >> (7-j)) & (0x01)));
		c++;
	}
	*t = '\0';

	/* now string leading zero's if any */
	j = strlen (s) - 1;
	for (i=0; i<j; i++) {
		if (*s != '0')
			break;
		else
			s++;
	}

	/* now copy the contents of s to p */
	t = p;
	for (i=0;i<(8*size + 1);i++) {
		if (*s == '\0') {
			*t = '\0';
			break;
		}
		*t++ = *s++;
	}
	free (s);

	return 1;
}

static char *
hexconvert(c,size,chunk)
	unsigned char *c;
	int size;
	int chunk;
{
	register char *s, *t;
	register int i,j,k;
	int numchunks;
	int leftovers;

	if ((s = (char *)malloc((size*5)+1)) == NULL)
		return (NULL);

	if (size <= 0) 
		return (NULL);

	if (chunk > size || chunk <= 0)
		chunk = size;

	numchunks = size/chunk;
	leftovers = size % chunk;

	t = s;
	for (i=j=0; i < numchunks; i++) {
		if (j++) {
			*t = ' ';
			t++;
		}
		(void)sprintf(t,"0x");
		t+=2;
		for (k=0; k < chunk; k++) {
			sprintf(t,"%02x", *c++);
			t+=2;
		}
	}

	if (leftovers) {
		*t++ = ' ';
		*t++ = '0';
		*t++ = 'x';
		for (i=0; i < leftovers; i++) {
			sprintf(t,"%02x", *c++);
			t+=2;
		}
	}
		
	*t = '\0';
	return (s);
}

static char *
get_Hname(addr)
unsigned long addr;
{
	extern char *inet_ntoa(const struct in_addr);
	struct hostent *phe;
	static char buf[256];
	struct in_addr ia;

	phe = gethostbyaddr((const char *)&addr, 4, AF_INET);
	if (phe == (struct hostent *)0)
	{
		ia.s_addr = addr;
		(void) sprintf(buf, "%s", inet_ntoa(ia));
		return(buf);
	}
	ia.s_addr = addr;
	(void) sprintf(buf, "%s", phe->h_name);
	return(buf);
}

static char *
pa_gettokenstring (tokenid)
int tokenid;
{
        int i;
        struct tokentable *k;
 
        for (i=0;i<numtokenentries;i++) {
                k = &(tokentab[i]);
                if ((k->tokenid) == tokenid)
                        return (k->tokentype);
        }
        /* here if token id is not in table */
        return (NULL);
}
