# Copyright (c) 1993 Sun Microsystems, Inc.  All Rights Reserved.
#
#	"@(#)el_GR.s	1.1	93/03/02 SMI"
#
# This mapfile is provided as example only. Sun Microsystems Inc.,
# does not warrant that this file is error-free.
#
#	el_EL.8859-7
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
dead: 0xb4		# tonos (acute>
0xc1	0xb6          # ALPHA accent
0xc5	0xb8         # EPSILON accent
0xc7	0xb9          # ETA accent
0xc9	0xba          # IOTA accent
0xcf	0xbc          # OMIKRON accent
0xd5	0xbe          # UPSILON accent
0xd9	0xbf          # OMEGA accent
0xe1	0xdc          # alpha accent
0xe5	0xdd         # epsilon accent
0xe7	0xde          # eta accent
0xe9	0xdf          # iota accent
0xef	0xfc          # omikron accent
0xf5	0xfd          # upsilon accent
0xf9	0xfe          # omega accent
#
dead:	0xa8		# diaeresis
0xc9	0xda         # IOTA diaeresis
0xd5	0xdb         # UPSILON diaeresis
0xe9	0xfa         # iota diaeresis
0xf5	0xfb         # upsilon diaeresis
#
dead: 0xb5			# diaeresis and tonos
' '   ' '
0xe9	0xc0         # iota diaeresis & accent
0xf5	0xe0         # upsilon diaresis & accent
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
' '  ' '  0xa0          # no breaking space 
' '  '2'  0xb2          # superscript 2
' '  '3'  0xb3          # superscript 3
'$'  '$'  0xa7          # paragraph sign
'''  ' '  '''          # apostrophe
'''  ':'  0xa8          # diaeresis
'('  'U'  0x98          # intersection
'*'  '*'  0xb0          # degree
'*'  's'  0xf2          # final sigma
'+'  '-'  0xb1          # plus-minus
'-'  '-'  0xad          # soft hyphen
'-'  '|'  0xac          # not sign
'.'  '.'  0xb7          # middle dot
'.'  'M'  0xb7          # middle dot
'1'  'h'  0xbd          # vulgar fraction 1/2
'2'  'S'  0xb2          # superscript 2
'3'  'S'  0xb3          # superscript 3
'<'  '<'  0xab          # left double angle quotation mark
'>'  '>'  0xbb          # right double angle quotation mark
'A'  '%'  0xb6          # Alpha accent
'A'  '*'  0xc1          # Greek letter ALPHA
'B'  '*'  0xc2          # Beta
'B'  'B'  0xa6          # broken bar
'C'  '*'  0xce          # Xi
'C'  'o'  0xa9          # copyright sign
'D'  '*'  0xc4          # Delta
'D'  'G'  0xb0          # degree
'E'  '%'  0xb8          # Epsilon accent
'E'  '*'  0xc5          # Epsilon
'F'  '*'  0xd6          # Greek letter PHI
'G'  '*'  0xc3          # Greek letter GAMMA
'H'  '*'  0xc8          # Greek letter THETA
'I'  '%'  0xba          # Iota accent
'I'  '*'  0xc9          # Iota
'J'  '*'  0xda          # Iota diaeresis
'K'  '*'  0xca          # Kappa
'L'  '*'  0xcb          # Lambda
'L'  '-'  0xa3          # pound sign
'M'  '*'  0xcc          # Mu
'N'  '*'  0xcd          # Nu
'N'  'O'  0xac          # not sign
'N'  'S'  0xa0          # no breaking space 
'N'  'o'  0xac          # not sign
'O'  '%'  0xbc          # Omikron accent
'O'  '*'  0xcf          # Omikron
'P'  '*'  0xd0          # Pi
'P'  'd'  0xa3          # pound sign
'Q'  '*'  0xd8          # Psi
'R'  '*'  0xd1          # Rho
'S'  '*'  0xd3          # Greek letter SIGMA
'S'  'E'  0xa7          # paragraph sign
'T'  '*'  0xd4          # Tau
'U'  '%'  0xbe          # Upsilon accent
'U'  '*'  0xd5          # Upsilon
'V'  '*'  0xdb          # Upsilon diaeresis
'W'  '%'  0xbf          # Omega accent
'W'  '*'  0xd9          # Greek letter OMEGA
'X'  '*'  0xd7          # Chi
'Y'  '%'  0xb9          # Eta accent
'Y'  '*'  0xc7          # Eta
'Z'  '*'  0xc6          # Zeta
'a'  '%'  0xdc          # alpha accent
'a'  '*'  0xe1          # Greek letter alpha
'b'  '*'  0xe2          # Greek letter beta
'b'  't'  0x94          # "Bottom T intersection" (_|_)
'c'  '*'  0xee          # xi
'c'  'O'  0xa9          # copyright sign
'd'  '*'  0xe4          # Greek letter delta
'e'  '%'  0xdd          # epsilon accent
'e'  '*'  0xe5          # Greek letter epsilon
'f'  '*'  0xf6          # Greek letter phi
'g'  '*'  0xe3          # gamma
'h'  '*'  0xe8          # theta
'h'  'l'  0x97          # "Horizontal Line"
'i'  '%'  0xdf          # iota accent
'i'  '*'  0xe9          # iota
'j'  '*'  0xfa          # iota diaeresis
'k'  '*'  0xea          # kappa
'l'  '*'  0xeb          # lambda
'l'  'l'  0x93          # "Lower Left Corner" (|_)
'l'  'r'  0x99          # "Lower Right Corner" (_|)
'l'  't'  0x96          # "Left T Intersection" (|-)
'm'  '*'  0xec          # Greek letter mu
'n'  '*'  0xed          # nu
'o'  '%'  0xfc          # omikron accent
'o'  '*'  0xef          # omikron
'p'  '*'  0xf0          # Greek letter pi
'p'  's'  0xa3          # pound sign
'q'  '*'  0xf8          # psi
'r'  '*'  0xf1          # rho
'r'  't'  0x91          # "Right T Intersection" (-|)
's'  '*'  0xf3          # Greek letter sigma
't'  '*'  0xf4          # Greek letter tau
'u'  '%'  0xfd          # upsilon accent
'u'  '*'  0xf5          # upsilon
'u'  'l'  0x9a          # "Upper Left Corner" (|~)
'u'  'r'  0x92          # "Upper Right Corner" (~|)
'u'  't'  0x95          # "Upper T intersection" (~|~)
'v'  '%'  0xe0          # upsilon with diaeresis and accent
'v'  '*'  0xfb          # upsilon diaeresis
'v'  'l'  0x90          # "Vertical Line" (-)
'w'  '%'  0xfe          # omega accent
'w'  '*'  0xf9          # omega
'x'  '*'  0xf7          # chi
'x'  't'  0x98          # "Middle Cross(Intersection" (-|-)
'y'  '%'  0xde          # eta accent
'y'  '*'  0xe7          # eta
'z'  '*'  0xe6          # zeta
'|'  '|'  0xa6          # broken bar
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
0x4	'3'	0xb7	'3'|N	0xb7|N
0x10	0xa4	0xa5	'q'|C	'Q'|C
0x11	0xf2	0xaa	'w'|C	'W'|C
0x12	0xe5	0xc5	'e'|C	'E'|C	CAPS
0x13	0xf1	0xd1	'r'|C	'R'|C	CAPS
0x14	0xf4	0xd4	't'|C	'T'|C	CAPS
0x15	0xf5	0xd5	'y'|C	'Y'|C	CAPS
0x16	0xe8	0xc8	'u'|C	'U'|C	CAPS
0x17	0xe9	0xc9	'i'|C	'I'|C	CAPS
0x18	0xef	0xcf	'o'|C	'O'|C	CAPS
0x19	0xf0	0xd0	'p'|C	'P'|C	CAPS
0x1a	'['|C	'{'	'['|N	'{'|N
0x1b	']'|C	'}'	']'|N	'}'|N
0x1e	0xe1	0xc1	'a'|C	'A'|C	CAPS
0x1f	0xf3	0xd3	's'|C	'S'|C	CAPS
0x20	0xe4	0xc4	'd'|C	'D'|C	CAPS
0x21	0xf6	0xd6	'f'|C	'F'|C	CAPS
0x22	0xe3	0xc3	'g'|C	'G'|C	CAPS
0x23	0xe7	0xc7	'h'|C	'H'|C	CAPS
0x24	0xee	0xce	'j'|C	'J'|C	CAPS
0x25	0xea	0xca	'k'|C	'K'|C	CAPS
0x26	0xeb	0xcb	'l'|C	'L'|C	CAPS
0x28	'''	0xa8	0xb5	-
0x2b	'#'	'~'	'#'|N	'~'|N
0x2c	0xe6	0xc6	'z'|C	'Z'|C	CAPS
0x2d	0xf7	0xd7	'x'|C	'X'|C	CAPS
0x2e	0xf8	0xd8	'c'|C	'C'|C	CAPS
0x2f	0xf9	0xd9	'v'|C	'V'|C	CAPS
0x30	0xe2	0xc2	'b'|C	'B'|C	CAPS
0x31	0xed	0xcd	'n'|C	'N'|C	CAPS
0x32	0xec	0xcc	'm'|C	'M'|C	CAPS
0x29	'<'	'>'	'<'|N	'>'|N
0x56	'\'|C	'|'	'\'|N	'|'|N
#
# map CTRL SHIFT F1  to be 0x18 for the compose character key
F37     0x18
# map CTRL SHIFT F2 to be 0x14 for the toggle key
F38     0x14
