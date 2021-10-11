/** Copyright (c) 1990  Mentat Inc.
 ** ndd.c 2.1, last change 11/14/90
 **/

#pragma ident	"@(#)ndd.c	1.4	93/01/29 SMI" 

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#ifdef USE_STDARG
#include <stdarg.h>
#else
#include <varargs.h>
#endif
#include <sys/types.h>
#include <inet/common.h>
#include <sys/stropts.h>
#include <inet/nd.h>
#include <string.h>

#define	FMNAMESZ	8

#ifndef	NULL_DEVICE_NAME
#define	NULL_DEVICE_NAME	"/dev/null"
#endif

staticf	void	do_getset(   int fd, int cmd, char * buf, int buf_len   );
staticf	int	get_value(   char * msg, char * buf, int buf_len   );
staticf	void	name_print(   char * buf   );
staticf	int	ndd_interactive(   int fd, boolean_t do_one_only   );
staticf	int	push_module(   void   );
	char *  errmsg( int err);
#ifdef	USE_STDARG
staticf	void	fatal(   char * fmt, ... );
	int	printe(   boolean_t print_errno, ... );
#else
staticf void	fatal( );
#endif

extern	void	usage(   char * str   );
extern	void	exit(   int status   );

extern	int	errno;
	char	gbuf[65536];	/* Need 20k for 160 IREs ... */
static	char	usage_str[] = "ndd [-set] [-get] module_to_push names_to_get|name_to_set value_to_set\n";

main (argc, argv)
	int	argc;
	char	** argv;
{
	char	* cp, * value;
	int	cmd;
	int	fd;
	int	len;

	if (!(cp = *++argv))
		return ndd_interactive(-1, false);
	cmd = ND_GET;
	if (cp[0] == '-') {
		if (strncmp(&cp[1], "set", 3) == 0)
			cmd = ND_SET;
		else if (strncmp(&cp[1], "get", 3) != 0)
			usage(usage_str);
		if (!(cp = *++argv))
			usage(usage_str);
	}
	if ((fd = stream_open(cp, 2)) != -1) {
		if (stream_ioctl(fd, I_NREAD, (char *)&len) == -1)
			fatal("device name given is not a stream device (%s)", errmsg(0));
	} else {
		char	* cp1;
		
		if ( cp1 = strrchr(cp, '/') )
			cp = ++cp1;
		if (strlen(cp) > FMNAMESZ)
			fatal("module name is too long");
		if ((fd = stream_open(NULL_DEVICE_NAME, 2)) == -1)
			fatal("couldn't open %s, %s", NULL_DEVICE_NAME, errmsg(0));
		if (stream_ioctl(fd, I_PUSH, cp) == -1)
			fatal("couldn't push module '%s', %s", cp, errmsg(0));
	}
	if (!(cp = *++argv))
		return ndd_interactive(fd, true);
	if (cmd == ND_SET) {
		if (!(value = *++argv))
			usage(usage_str);
		sprintf(gbuf, "%s%c%s%c", cp, 0, value, 0);
		do_getset(fd, cmd, gbuf, sizeof(gbuf));
	} else {
		do {
			memset(gbuf, '\0', sizeof(gbuf));
			sprintf(gbuf, "%s", cp);
			do_getset(fd, cmd, gbuf, sizeof(gbuf));
			if (cp = *++argv)
				putchar('\n');
		} while (cp);
	}
	stream_close(fd);
}

staticf void
name_print (buf)
	char	* buf;
{
	char	* cp, * rwtag;

	for (cp = buf; cp[0]; ) {
		for (rwtag = cp; !isspace(*rwtag); rwtag++)
			noop;
		*rwtag++ = '\0';
		while (isspace(*rwtag))
			rwtag++;
		printf("%-30s%s\n", cp, rwtag);
		for (cp = rwtag; *cp++; )
			noop;
	}	
}

staticf void
do_getset (fd, cmd, buf, buf_len)
	int	fd;
	int	cmd;
	char	* buf;
	int	buf_len;
{
	char	* cp;
	struct strioctl	stri;
	boolean_t	is_name_get;

	stri.ic_cmd = cmd;
	stri.ic_timout = 0;
	stri.ic_len = buf_len;
	stri.ic_dp = buf;
	is_name_get = stri.ic_cmd == ND_GET && buf[0] == '?' && buf[1] =='\0';
	if (stream_ioctl(fd, I_STR, (char *)&stri) == -1) {
		if (errno == ENOENT) {
			printf("name is non-existent for this module\n");
			printf("for a list of valid names, use name '?'\n");
		} else
			printf("operation failed, %s\n", errmsg(0));
		return;
	}
	if (is_name_get)
		name_print(buf);
	else if (stri.ic_cmd == ND_GET) {
		for (cp = buf; cp[0]; ) {
			printf("%s\n", cp);
			while (*cp++)
				noop;
		}
	}
	fflush(stdout);
}

staticf int
get_value (msg, buf, buf_len)
	char	* msg;
	char	* buf;
	int	buf_len;
{
	int	len;

	if (msg) {
		printf("%s", msg);
		fflush(stdout);
	}
	buf[buf_len-1] = '\0';
	if (!fgets(buf, buf_len-1, stdin))
		exit(0);
	len = strlen(buf);
	if (buf[len-1] == '\n')
		buf[len - 1] = '\0';
	else
		len++;
	return len;
}

staticf int
ndd_interactive (fd, do_one_only)
	int	fd;
	boolean_t	do_one_only;
{
	int	cmd;
	char	* cp;
	char	getset[4];
	int	len;
	int	buf_len;
	char	len_buf[10];
	
	if (fd == -1  &&  (fd = push_module()) == -1)
		return 0;
	do {
		for (;;) {
			memset(gbuf, '\0', sizeof(gbuf));
			len = get_value("name to get/set ? ", gbuf, sizeof(gbuf));
			if (len == 1
			|| (gbuf[0] == 'q'  &&  gbuf[1] == '\0'))
				break;
			for (cp = gbuf; cp < &gbuf[len]; cp++) {
				if (isspace(*cp))
					*cp = '\0';
			}
			cmd = ND_GET;
			if (gbuf[0] != '?'
			&& get_value("value ? ",&gbuf[len], sizeof(gbuf)-len) >1)
				cmd = ND_SET;
			if (cmd == ND_GET  &&  gbuf[0] != '?'
			&&  get_value("length ? ", len_buf,sizeof(len_buf))>1){
				if (!isdigit(len_buf[0])) {
					printf("invalid length\n");
					continue;
				}
				buf_len = atoi(len_buf);
			} else
				buf_len = sizeof(gbuf);
			do_getset(fd, cmd, gbuf, buf_len);
		}
		stream_close(fd);
		if (do_one_only)
			return 1;
	} while ((fd = push_module()) != -1);
	return 1;
}

#ifdef USE_STDARG
int
printe (print_errno, ... )
	boolean_t	print_errno;
#else
int
printe (print_errno, va_alist )
	boolean_t	print_errno;
	va_dcl
#endif
{
	va_list	ap;
	char	* fmt;

#ifdef USE_STDARG
	va_start(ap, print_errno);
#else
	va_start(ap);
#endif
	printf("*ERROR* ");
	if (fmt = va_arg(ap, char *))
		(void)vprintf(fmt, ap);
	va_end(ap);
	if (print_errno)
		printf(", %s\n", errmsg(0));
	else
		printf("\n");
	errno = 0;
	return false;
}

staticf int
push_module ()
{
	char	name[80];
	int	fd, len;

	for (;;) {
		len = get_value("module to query ? ", name, sizeof(name));
		if (len <= 1 || (len == 2 && (name[0] == 'q' || name[0] == 'Q')))
			return -1;
		if ((fd = stream_open(name, 2)) != -1) {
			if (stream_ioctl(fd, I_NREAD, (char *)&len) != -1)
				return fd;
			stream_close(fd);
		}
		if ((fd = stream_open(NULL_DEVICE_NAME, 2)) == -1)
			fatal("couldn't open /dev/null, %s", errmsg(0));
		if (len > FMNAMESZ) {
			printe(false, "module name is too long");
			continue;
		}
		if (stream_ioctl(fd, I_PUSH, name) != -1) {
			fflush(stdin);
			return fd;
		}
		printe(true, "couldn't push '%s'", name);
	}
}

void
#ifdef	USE_STDARG
fatal (fmt, ... )
	char	* fmt;
#else
fatal (fmt, va_alist)
	char	* fmt;
	va_dcl
#endif
{
	va_list	ap;

	if (fmt  &&  *fmt) {
#ifdef	USE_STDARG
		va_start(ap, fmt);
#else
		va_start(ap);
#endif
		(void)vfprintf(stderr, fmt, ap);
		va_end(ap);
		while (*fmt)
			fmt++;
		if (fmt[-1] != '\n')
			(void)fprintf(stderr, "\n");
	}
	exit(BAD_EXIT_STATUS);
	/*NOTREACHED*/
}


extern	int	errno;

char *
errmsg (err)
	int	err;
{
	static	char	buf[40];

	if (err  ||  (err = errno)) {
		char *errstr;

		if ((errstr = strerror(err)) != (char *) NULL)
			return (errstr);
	}
	if (err)
		(void)sprintf(buf, "error number %d", err);
	else
		(void)sprintf(buf, "unspecified error");
	return buf;
}

extern	void	exit(   int status   );

void
usage (str)
	char	* str;
{
	if (!str  ||  !*str)
		str = "\n";
	else if (strncmp("usage:", str, 6) == 0)
		(void)fprintf(stderr, "%s", str);
	else
		(void)fprintf(stderr, "usage: %s", str);
	while (*str)
		str++;
	if (str[-1] != '\n')
		(void)fprintf(stderr, "\n");
	exit(BAD_EXIT_STATUS);
	/*NOTREACHED*/
}
