#define L_BITSMAJOR     14      /* # of SVR4 major device bits */
#define L_BITSMINOR     18      /* # of SVR4 minor device bits */
#define L_MAXMIN        0x3ffff /* MAX minor for 3b2 software drivers.
                                ** For 3b2 hardware devices the minor is
                                ** restricted to 256 (0-255)
                                */
#define O_BITSMINOR     8       /* # of SunOS 4.x minor device bits */
#define O_MAXMAJ        0xff    /* SunOS 4.x max major value */
#define O_MAXMIN        0xff    /* SunOS 4.x max minor value */

/* convert to old dev format */

#define cmpdev(x)       (unsigned long)((((x)>>L_BITSMINOR) > O_MAXMAJ || \
                                ((x)&L_MAXMIN) > O_MAXMIN) ? NODEV : \
                                ((((x)>>L_BITSMINOR)<<O_BITSMINOR)|((x)&O_MAXMIN)))
