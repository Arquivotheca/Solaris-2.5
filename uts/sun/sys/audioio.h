/*
 * Copyright (c) 1991-92 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_AUDIOIO_H
#define	_SYS_AUDIOIO_H

#pragma ident	"@(#)audioio.h	1.21	94/01/21 SMI"

#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioccom.h>

/*
 * These are the ioctl calls for generic audio devices, including the
 * SPARCstation-1 audio device.
 *
 * You are encouraged to design your code in a modular fashion so that
 * future changes to the interface can be incorporated with little
 * trouble.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This structure contains state information for audio device IO streams.
 */
typedef struct audio_prinfo {
	/*
	 * The following values describe the audio data encoding.
	 */
	uint_t sample_rate;	/* samples per second */
	uint_t channels;	/* number of interleaved channels */
	uint_t precision;	/* bit-width of each sample */
	uint_t encoding;	/* data encoding method */

	/*
	 * The following values control audio device configuration
	 */
	uint_t gain;		/* gain level: 0 - 255 */
	uint_t port;		/* selected I/O port (see below) */
	uint_t avail_ports;	/* available I/O ports (see below) */
	uint_t _xxx[2];		/* Reserved for future use */

	uint_t buffer_size;	/* I/O buffer size */

	/*
	 * The following values describe driver state
	 */
	uint_t samples;		/* number of samples converted */
	uint_t eof;		/* End Of File counter (play only) */

	uchar_t	pause;		/* non-zero for pause, zero to resume */
	uchar_t	error;		/* non-zero if overflow/underflow */
	uchar_t	waiting;	/* non-zero if a process wants access */
	uchar_t balance;	/* stereo channel balance */

	ushort_t minordev;

	/*
	 * The following values are read-only state flags
	 */
	uchar_t open;		/* non-zero if open access permitted */
	uchar_t active;		/* non-zero if I/O is active */
} audio_prinfo_t;


/*
 * This structure describes the current state of the audio device.
 */
typedef struct audio_info {
	/*
	 * Per-stream information
	 */
	audio_prinfo_t play;	/* output status information */
	audio_prinfo_t record;	/* input status information */

	/*
	 * Per-unit/channel information
	 */
	uint_t monitor_gain;	/* input to output mix: 0 - 255 */
	uchar_t output_muted;	/* non-zero if output is muted */
	uchar_t _xxx[3];	/* Reserved for future use */
	uint_t _yyy[3];		/* Reserved for future use */
} audio_info_t;


/*
 * Audio encoding types
 */
#define	AUDIO_ENCODING_NONE	(0) /* no encoding assigned */
#define	AUDIO_ENCODING_ULAW	(1) /* u-law encoding */
#define	AUDIO_ENCODING_ALAW	(2) /* A-law encoding */
#define	AUDIO_ENCODING_LINEAR	(3) /* Linear PCM encoding */
#define	AUDIO_ENCODING_DVI	(104) /* DVI ADPCM */
#define	AUDIO_ENCODING_LINEAR8	(105) /* 8 bit UNSIGNED */

/*
 * These ranges apply to record, play, and monitor gain values
 */
#define	AUDIO_MIN_GAIN	(0)	/* minimum gain value */
#define	AUDIO_MAX_GAIN	(255)	/* maximum gain value */

/*
 * These values apply to the balance field to adjust channel gain values
 */
#define	AUDIO_LEFT_BALANCE	(0) /* left channel only */
#define	AUDIO_MID_BALANCE	(32) /* equal left/right channel */
#define	AUDIO_RIGHT_BALANCE	(64) /* right channel only */
#define	AUDIO_BALANCE_SHIFT	(3)

/*
 * Define some convenient names for SPARCstation audio ports
 */
	/* output ports (several may be enabled simultaneously) */
#define	AUDIO_SPEAKER		0x01	/* output to built-in speaker */
#define	AUDIO_HEADPHONE		0x02	/* output to headphone jack */
#define	AUDIO_LINE_OUT		0x04	/* output to line out */
	/* input ports (usually only one at a time) */
#define	AUDIO_MICROPHONE	0x01	/* input from microphone */
#define	AUDIO_LINE_IN		0x02	/* input from line in */
#define	AUDIO_INTERNAL_CD_IN	0x04	/* input from internal CDROM */


/*
 * This macro initializes an audio_info structure to 'harmless' values.
 * Note that (~0) might not be a harmless value for a flag that was
 * a signed int.
 */
#define	AUDIO_INITINFO(i)	{					\
	uint_t	*__x__;						\
	for (__x__ = (uint_t *)(i);				\
	    (char *) __x__ < (((char *)(i)) + sizeof (audio_info_t));	\
	    *__x__++ = ~0);						\
}


/*
 * Parameter for the AUDIO_GETDEV ioctl to determine current
 * audio devices.
 */
#define	MAX_AUDIO_DEV_LEN	(16)
typedef struct audio_device audio_device_t;
struct audio_device {
	char name[MAX_AUDIO_DEV_LEN];
	char version[MAX_AUDIO_DEV_LEN];
	char config[MAX_AUDIO_DEV_LEN];
};


/*
 * Ioctl calls for the audio device.
 */

/*
 * AUDIO_GETINFO retrieves the current state of the audio device.
 *
 * AUDIO_SETINFO copies all fields of the audio_info structure whose
 * values are not set to the initialized value (-1) to the device state.
 * It performs an implicit AUDIO_GETINFO to return the new state of the
 * device.  Note that the record.samples and play.samples fields are set
 * to the last value before the AUDIO_SETINFO took effect.  This allows
 * an application to reset the counters while atomically retrieving the
 * last value.
 *
 * AUDIO_DRAIN suspends the calling process until the write buffers are
 * empty.
 *
 * AUDIO_GETDEV returns a structure of type audio_device_t which contains
 * three strings.  The string "name" is a short identifying string (for
 * example, the SBus Fcode name string), the string "version" identifies
 * the current version of the device, and the "config" string identifies
 * the specific configuration of the audio stream.  All fields are
 * device-dependent -- se the device specific manual pages for details.
 */
#define	AUDIO_GETINFO	_IOR('A', 1, audio_info_t)
#define	AUDIO_SETINFO	_IOWR('A', 2, audio_info_t)
#define	AUDIO_DRAIN	_IO('A', 3)
#define	AUDIO_GETDEV	_IOR('A', 4, audio_device_t)

/*
 * The following ioctl sets the audio device into an internal loopback mode,
 * if the hardware supports this.  The argument is TRUE to set loopback,
 * FALSE to reset to normal operation.  If the hardware does not support
 * internal loopback, the ioctl should fail with EINVAL.
 */
#define	AUDIO_DIAG_LOOPBACK	_IOW('A', 101, int)


/*
 * Structure sent up as a M_PROTO message on trace streams
 */
typedef struct audtrace_hdr audtrace_hdr_t;
struct audtrace_hdr {
	uint_t seq;		/* Sequence number (per-aud_stream) */
	int type;		/* device-dependent */
	struct timeval timestamp;
	char _f[8];		/* filler */
};


#ifdef __cplusplus
}
#endif

#endif /* _SYS_AUDIOIO_H */
