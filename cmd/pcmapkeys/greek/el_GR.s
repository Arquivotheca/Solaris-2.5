# Copyright (c) 1993 Sun Microsystems, Inc.  All Rights Reserved.
#
#	"@(#)el_GR.s	1.1	93/03/02 SMI"
#
# This mapfile is provided as example only. Sun Microsystems Inc.,
# does not warrant that this file is error-free.
#
#	el_EL.437
#
# ttymap file for keyboard mapping in IBM Greek.
#
# This mapping file is indended for use with the Greek loadfont.
# It implements a keyboard mapping which allows the use of the
# 437 code set, using the Danish Standards Association's recommended
# short names (but not their "compose" value), such that the "compose" key,
# followed by the 2-letter code, generates the character.
# In addition, more "graphic" combinations are also encoded
# (for instance, E^ in addition to E>, and ?? in addition to ?I
# for inverted question mark). The graphic characters are encoded
# using special names.
#
# This ttymap file modifies the scan codes, thus allowing concurrent
# usage of Greek and Latin letters. However, that implies the loss of
# the traditional ALT+char values used by e.g. 10+ and other applications.
# If your application uses these, the mapfile el_GR.t could be used.
#
input:
#
# The toggle key is CTRL t.
#
toggle: 0x14
#
dead: '''		# tonos (acute>
0x80	0xea          # ALPHA accent
0x84	0xeb         # EPSILON accent
0x86	0xec          # ETA accent
0x88	0xed          # IOTA accent
0x8e	0xee          # OMIKRON accent
0x93	0xef          # UPSILON accent
0x97	0xf0          # OMEGA accent
0x98	0xe1          # alpha accent
0x9c	0xe2         # epsilon accent
0x9e	0xe3          # eta accent
0xa0	0xe5          # iota accent
0xa6	0xe6          # omikron accent
0xac	0xe7          # upsilon accent
0xe0	0xe9          # omega accent
#
dead:	0xf8		# diaeresis
0xa0	0xe4         # iota diaeresis
0xac	0xe8         # upsilon diaeresis
#
dead: 0xff			# diaeresis and tonos
' '   ' '
#
# The "compose" character is CTRL x.
# 
# Compose mode: 
# 
#
# The "compose" character is CTRL x.
# Compose mode: 
# 
compose:  0x18      # unfrequently used ctrl character
#			
' '  ' '  0xff          # no breaking space 
' '  '2'  0xfd          # superscript 2
' '  'n'  0xfc          # superscript n
'''  ' '  '''          # apostrophe
'('  'U'  0xc5          # intersection
'*'  '*'  0xf8          # degree
'*'  's'  0xaa          # final sigma
'+'  '-'  0xf1          # plus-minus
'-'  ':'  0xf6          # division sign
'.'  '.'  0xfa          # middle dot
'.'  'M'  0xfa          # middle dot
'.'  'S'  0xb0          # box drawing light shade (25%)
'2'  'S'  0xfd          # superscript 2
':'  'S'  0xb1          # box drawing medium shade (50%)
'<'  '='  0xf3          # less than or equal
'>'  '='  0xf2          # greater than or equal to
'?'  '2'  0xf7          # almost equal
'?'  'S'  0xb2          # box drawing dark shade (75%)
'A'  '%'  0xea          # Alpha accent
'A'  '*'  0x80          # Greek letter ALPHA
'B'  '*'  0x81          # Beta
'B'  'T'  0xca          # "Bottom T Intersection Double"
'B'  't'  0xd0          # "Bottom T (_||_)
'C'  '*'  0x8d          # Xi
'D'  '*'  0x83          # Delta
'D'  'G'  0xf8          # degree
'E'  '%'  0xeb          # Epsilon accent
'E'  '*'  0x84          # Epsilon
'F'  '*'  0x94          # Greek letter PHI
'F'  'B'  0xdb          # "Solid Full Block"
'G'  '*'  0x82          # Greek letter GAMMA
'H'  '*'  0x87          # Greek letter THETA
'H'  'L'  0xcd          # "Horizontal Line Double" (||)
'I'  '%'  0xed          # Iota accent
'I'  '*'  0x88          # Iota
'I'  'B'  0xf4          # Integral sign (bottom half)
'I'  'T'  0xf5          # Integral sign (top half)
'K'  '*'  0x89          # Kappa
'L'  '*'  0x8a          # Lambda
'L'  '-'  0xf9          # pound sign
'L'  'B'  0xdc          # "Solid Lower Half Block"
'L'  'L'  0xc8          # "Lower Left Corner Double"
'L'  'R'  0xbc          # "Lower Right Corner Double"
'L'  'T'  0xcc          # "Left T Intersection Double" (|-) 
'L'  'l'  0xd4          # "Lower Left Corner" (h. double)
'L'  'r'  0xbe          # "Lower Right Corner" (h. double)
'L'  't'  0xc6          # "Left T Intersection" (|=)
'M'  '*'  0x8b          # Mu
'N'  '*'  0x8c          # Nu
'N'  'S'  0xff          # no breaking space 
'O'  '%'  0xee          # Omikron accent
'O'  '*'  0x8e          # Omikron
'P'  '*'  0x8f          # Pi
'P'  'd'  0xf9          # pound sign
'Q'  '*'  0x96          # Psi
'R'  '*'  0x90          # Rho
'R'  'B'  0xde          # Solid block right half
'R'  'T'  0xb9          # "Right T intersection Double" (-|)
'S'  '*'  0x91          # Greek letter SIGMA
'T'  '*'  0x92          # Tau
'U'  '%'  0xef          # Upsilon accent
'U'  '*'  0x93          # Upsilon
'U'  'B'  0xdf          # "Solid Upper Half Block"
'U'  'L'  0xc9          # "Upper Left Corner Double" (|~)
'U'  'R'  0xbb          # "Upper Right Corner Double" (~|)
'U'  'T'  0xcb          # "Upper T Intersection Double"
'U'  'l'  0xd5          # "Upper Left Corner" (h. double)
'U'  'r'  0xb8          # "Upper Right Corner" (h. double)
'U'  't'  0xd1          # "Upper T" (h. double)
'V'  'L'  0xba          # "Vertical line Double" (=)
'W'  '%'  0xf0          # Omega accent
'W'  '*'  0x97          # Greek letter OMEGA
'X'  '*'  0x95          # Chi
'X'  'T'  0xce          # "Middle Cross Heavy" (=||=)
'X'  't'  0xd7          # "Middle Cross" (-||-)
'Y'  '%'  0xec          # Eta accent
'Y'  '*'  0x86          # Eta
'Z'  '*'  0x85          # Zeta
'a'  '%'  0xe1          # alpha accent
'a'  '*'  0x98          # Greek letter alpha
'b'  '*'  0x99          # Greek letter beta
'b'  't'  0xc1          # "Bottom T intersection" (_|_)
'c'  '*'  0xa5          # xi
'd'  '*'  0x9b          # Greek letter delta
'e'  '%'  0xe2          # epsilon accent
'e'  '*'  0x9c          # Greek letter epsilon
'f'  '*'  0xad          # Greek letter phi
'g'  '*'  0x9a          # gamma
'h'  '*'  0x9f          # theta
'h'  'l'  0xc4          # "Horizontal Line"
'i'  '%'  0xe5          # iota accent
'i'  '*'  0xa0          # iota
'j'  '*'  0xe4          # iota diaeresis
'k'  '*'  0xa1          # kappa
'l'  '*'  0xa2          # lambda
'l'  'B'  0xdd          # Solid block left half
'l'  'L'  0xd3          # "Lower Left Corner" (||_)
'l'  'R'  0xbd          # "Lower Right Corner" (_||)
'l'  'T'  0xc7          # "Left T Intersection" (|=))
'l'  'l'  0xc0          # "Lower Left Corner" (|_)
'l'  'r'  0xd9          # "Lower Right Corner" (_|)
'l'  't'  0xc3          # "Left T Intersection" (|-)
'm'  '*'  0xa3          # Greek letter mu
'n'  '*'  0xa4          # nu
'o'  '%'  0xe6          # omikron accent
'o'  '*'  0xa6          # omikron
'p'  '*'  0xa7          # Greek letter pi
'p'  's'  0xf9          # pound sign
'q'  '*'  0xaf          # psi
'r'  '*'  0xa8          # rho
'r'  'T'  0xb6          # "Right T Intersection" (=|)
'r'  't'  0xb4          # "Right T Intersection" (-|)
's'  '*'  0xa9          # Greek letter sigma
's'  'q'  0xfe          # solid square
't'  '*'  0xab          # Greek letter tau
'u'  '%'  0xe7          # upsilon accent
'u'  '*'  0xac          # upsilon
'u'  'L'  0xd6          # "Upper Left Corner" (||~)
'u'  'R'  0xb7          # "Upper Right Corner" (~||))
'u'  'T'  0xd2          # "Upper T Intersection" (~||~)
'u'  'l'  0xda          # "Upper Left Corner" (|~)
'u'  'r'  0xbf          # "Upper Right Corner" (~|)
'u'  't'  0xc2          # "Upper T intersection" (~|~)
'v'  '*'  0xe8          # upsilon diaeresis
'v'  'l'  0xb3          # "Vertical Line" (-)
'w'  '%'  0xe9          # omega accent
'w'  '*'  0xe0          # omega
'x'  '*'  0xae          # chi
'x'  'T'  0xd7          # "Middle Cross Intersection" (-||-)
'x'  't'  0xc5          # "Middle Cross(Intersection" (-|-)
'y'  '%'  0xe3          # eta accent
'y'  '*'  0x9e          # eta
'z'  '*'  0x9d          # zeta
#
#
# The following output section maps '9b' (an ANSI CSI code,
# which unfortunately is also an IBM character) such that
# it prints.
#
output:
0x9b	0x1b 0x9b
#
#
#
scancodes:
#scan codes for el_EL
#      NORM    SHIFT     ALT   ALT_SHIFT
0x3     '2'     '"'     '@'|C   '@'|C
0x4	'3'	0xfa	'3'|N	0xfa|N
0x10	0xf3	0xf2	'q'|C	'Q'|C
0x11	0xaa	0xfb	'w'|C	'W'|C
0x12	0x9c	0x84	'e'|C	'E'|C	CAPS
0x13	0xa8	0x90	'r'|C	'R'|C	CAPS
0x14	0xab	0x92	't'|C	'T'|C	CAPS
0x15	0xac	0x93	'y'|C	'Y'|C	CAPS
0x16	0x9f	0x87	'u'|C	'U'|C	CAPS
0x17	0xa0	0x88	'i'|C	'I'|C	CAPS
0x18	0xa6	0x8e	'o'|C	'O'|C	CAPS
0x19	0xa7	0x8f	'p'|C	'P'|C	CAPS
0x1a	'['|C	'{'	'['|N	'{'|N
0x1b	']'|C	'}'	']'|N	'}'|N
0x1e	0x98	0x80	'a'|C	'A'|C	CAPS
0x1f	0xa9	0x91	's'|C	'S'|C	CAPS
0x20	0x9b	0x83	'd'|C	'D'|C	CAPS
0x21	0xad	0x94	'f'|C	'F'|C	CAPS
0x22	0x9a	0x82	'g'|C	'G'|C	CAPS
0x23	0x9e	0x86	'h'|C	'H'|C	CAPS
0x24	0xa5	0x8d	'j'|C	'J'|C	CAPS
0x25	0xa1	0x89	'k'|C	'K'|C	CAPS
0x26	0xa2	0x8a	'l'|C	'L'|C	CAPS
0x28	'''	0xf8	0xff	-
0x2b	'#'	'~'	'#'|N	'~'|N
0x2c	0x9d	0x85	'z'|C	'Z'|C	CAPS
0x2d	0xae	0x95	'x'|C	'X'|C	CAPS
0x2e	0xaf	0x96	'c'|C	'C'|C	CAPS
0x2f	0xe0	0x97	'v'|C	'V'|C	CAPS
0x30	0x99	0x81	'b'|C	'B'|C	CAPS
0x31	0xa4	0x8c	'n'|C	'N'|C	CAPS
0x32	0xa3	0x8b	'm'|C	'M'|C	CAPS
0x29	'<'	'>'	'<'|N	'>'|N
0x56	'\'|C	'|'	'\'|N	'|'|N
#
# map CTRL SHIFT F1  to be 0x18 for the compose character key
F37     0x18
# map CTRL SHIFT F2 to be 0x14 for the toggle key
F38     0x14
