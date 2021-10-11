/*
 * Copyright (c) 1986 - 1991 by Sun Microsystems, Inc.
 */

/*
 * des_crypt.h, des library routine interface
 */

#ifndef _DES_DES_CRYPT_H
#define	_DES_DES_CRYPT_H

#pragma ident	"@(#)des_crypt.h	1.8	92/07/14 SMI"

#ifdef __cplusplus
extern "C" {
#endif

#define	DES_MAXDATA 8192	/* max bytes encrypted in one call */
#define	DES_DIRMASK (1 << 0)
#define	DES_ENCRYPT (0*DES_DIRMASK)	/* Encrypt */
#define	DES_DECRYPT (1*DES_DIRMASK)	/* Decrypt */


#define	DES_DEVMASK (1 << 1)
#define	DES_HW (0*DES_DEVMASK)	/* Use hardware device */
#define	DES_SW (1*DES_DEVMASK)	/* Use software device */


#define	DESERR_NONE 0	/* succeeded */
#define	DESERR_NOHWDEVICE 1	/* succeeded, but hw device not available */
#define	DESERR_HWERROR 2	/* failed, hardware/driver error */
#define	DESERR_BADPARAM 3	/* failed, bad parameter to call */

#define	DES_FAILED(err) \
	((err) > DESERR_NOHWDEVICE)

/*
 * cbc_crypt()
 * ecb_crypt()
 *
 * Encrypt (or decrypt) len bytes of a buffer buf.
 * The length must be a multiple of eight.
 * The key should have odd parity in the low bit of each byte.
 * ivec is the input vector, and is updated to the new one (cbc only).
 * The mode is created by oring together the appropriate parameters.
 * DESERR_NOHWDEVICE is returned if DES_HW was specified but
 * there was no hardware to do it on (the data will still be
 * encrypted though, in software).
 */


/*
 * Cipher Block Chaining mode
 */

/*
 * int
 * cbc_crypt(key, buf, len, mode, ivec);
 * 	char *key;
 *	char *buf;
 *	unsigned len;
 *	unsigned mode;
 *	char *ivec;
 */
#ifdef __STDC__
int cbc_crypt(char *, char *, unsigned int, unsigned int, char *);
#else
int cbc_crypt();
#endif


/*
 * Electronic Code Book mode
 */

/*
 * int
 * ecb_crypt(key, buf, len, mode);
 *	char *key;
 *	char *buf;
 *	unsigned len;
 *	unsigned mode;
 */
#ifdef __STDC__
int ecb_crypt(char *, char *, unsigned int, unsigned int);
#else
int ecb_crypt();
#endif

#ifndef _KERNEL
/*
 * Set des parity for a key.
 * DES parity is odd and in the low bit of each byte
 *
 * void
 * des_setparity(key);
 * char *key;
 */
#ifdef __STDC__
void des_setparity(char *);
#else
void des_setparity();
#endif
#endif	/* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif	/* _DES_DES_CRYPT_H */
