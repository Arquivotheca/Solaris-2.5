# Copyright (c) 1993 Sun Microsystems, Inc.  All Rights Reserved.
#
#	"@(#)ru_RU.s	1.1	93/03/02 SMI"
#
# This mapfile is provided as example only. Sun Microsystems Inc.,
# does not warrant that this file is error-free.
#
#	ru_RU.866
#
# This mapping file is indended for use with the 866 loadfont.
# It implements a keyboard mapping which allows the use of the
# 866 code set, using the Danish Standards Association's recommended
# short names (but not their "compose" value), such that the "compose" key,
# followed by the 2-letter code, generates the character.
# In addition, more "graphic" combinations are also encoded
# (for instance, E^ in addition to E>, and ?? in addition to ?I
# for inverted question mark). The graphic characters are encoded
# using special names.
#
# This ttymap file uses scan codes to map Cyrillic and Latin letters;
# It means that we lose the customary ALT+char codes. If you depend on
# those, use the ru_RU.t instead.
#
# Input mapping is not done 
input:
#
# The toggle key is CTRL t.
#
toggle: 0x14
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
'%'  '"'  0x9c          # SOFT SIGN
'%'  '''  0xec          # soft sign
'''  ' '  '''          # apostrophe
'('  'U'  0xc5          # intersection
'*'  '*'  0xf8          # degree
'+'  '-'  0xf1          # plus-minus
'-'  ':'  0xf6          # division sign
'.'  '.'  0xfa          # middle dot
'.'  'M'  0xfa          # middle dot
'.'  'S'  0xb0          # box drawing light shade (25%)
'2'  'S'  0xfd          # superscript 2
':'  'S'  0xb1          # box drawing medium shade (50%)
'<'  '='  0xf3          # less than or equal
'='  '"'  0x9a          # HARD SIGN
'='  '''  0xea          # hard sign
'='  '3'  0xf0          # Is identical to
'>'  '='  0xf2          # greater than or equal to
'?'  '2'  0xf7          # almost equal
'?'  'S'  0xb2          # box drawing dark shade (75%)
'A'  '='  0x80          # A
'B'  '='  0x81          # BE
'B'  'T'  0xca          # "Bottom T Intersection Double"
'B'  't'  0xd0          # "Bottom T (_||_)
'C'  '%'  0x97          # CHE
'C'  '='  0x96          # TSE
'D'  '='  0x84          # DE
'D'  'G'  0xf8          # degree
'E'  '='  0x85          # IE
'F'  '='  0x94          # EF
'F'  'B'  0xdb          # "Solid Full Block"
'G'  '='  0x83          # GHE
'H'  '='  0x95          # HA
'H'  'L'  0xcd          # "Horizontal Line Double" (||)
'I'  '='  0x88          # I
'I'  'B'  0xf4          # Integral sign (bottom half)
'I'  'T'  0xf5          # Integral sign (top half)
'J'  '='  0x89          # SHORT I
'J'  'A'  0x9f          # YA
'J'  'E'  0x9d          # E
'J'  'U'  0x9e          # YU
'K'  '='  0x8a          # KA
'L'  '='  0x8b          # EL
'L'  'B'  0xdc          # "Solid Lower Half Block"
'L'  'L'  0xc8          # "Lower Left Corner Double"
'L'  'R'  0xbc          # "Lower Right Corner Double"
'L'  'T'  0xcc          # "Left T Intersection Double" (|-) 
'L'  'l'  0xd4          # "Lower Left Corner" (h. double)
'L'  'r'  0xbe          # "Lower Right Corner" (h. double)
'L'  't'  0xc6          # "Left T Intersection" (|=)
'M'  '='  0x8c          # EM
'N'  '='  0x8d          # EN
'N'  'S'  0xff          # no breaking space 
'O'  '='  0x8e          # O
'P'  '='  0x8f          # PE
'R'  '='  0x90          # ER
'R'  'B'  0xde          # Solid block right half
'R'  'T'  0xb9          # "Right T intersection Double" (-|)
'S'  '%'  0x98          # SHA
'S'  '='  0x91          # ES
'S'  'c'  0x99          # SHCHA
'T'  '='  0x92          # TE
'U'  '='  0x93          # U
'U'  'B'  0xdf          # "Solid Upper Half Block"
'U'  'L'  0xc9          # "Upper Left Corner Double" (|~)
'U'  'R'  0xbb          # "Upper Right Corner Double" (~|)
'U'  'T'  0xcb          # "Upper T Intersection Double"
'U'  'l'  0xd5          # "Upper Left Corner" (h. double)
'U'  'r'  0xb8          # "Upper Right Corner" (h. double)
'U'  't'  0xd1          # "Upper T" (h. double)
'V'  '='  0x82          # VE
'V'  'L'  0xba          # "Vertical line Double" (=)
'X'  'T'  0xce          # "Middle Cross Heavy" (=||=)
'X'  't'  0xd7          # "Middle Cross" (-||-)
'Y'  '='  0x9b          # YERU
'Z'  '%'  0x86          # ZHE
'Z'  '='  0x87          # ZE
'a'  '='  0xa0          # a
'b'  '='  0xa1          # be
'b'  't'  0xc1          # "Bottom T intersection" (_|_)
'c'  '%'  0xe7          # che
'c'  '='  0xe6          # tse
'd'  '='  0xa4          # de
'e'  '='  0xa5          # ie
'f'  '='  0xe4          # ef
'g'  '='  0xa3          # ghe
'h'  '='  0xe5          # ha
'h'  'l'  0xc4          # "Horizontal Line"
'i'  '='  0xa8          # i
'j'  '='  0xa9          # short i
'j'  'a'  0xef          # ya
'j'  'e'  0xed          # e
'j'  'u'  0xee          # yu
'k'  '='  0xaa          # ka
'l'  '='  0xab          # el
'l'  'B'  0xdd          # Solid block left half
'l'  'L'  0xd3          # "Lower Left Corner" (||_)
'l'  'R'  0xbd          # "Lower Right Corner" (_||)
'l'  'T'  0xc7          # "Left T Intersection" (|=))
'l'  'l'  0xc0          # "Lower Left Corner" (|_)
'l'  'r'  0xd9          # "Lower Right Corner" (_|)
'l'  't'  0xc3          # "Left T Intersection" (|-)
'm'  '='  0xac          # em
'n'  '='  0xad          # en
'o'  '='  0xae          # o
'p'  '='  0xaf          # pe
'r'  '='  0xe0          # er
'r'  'T'  0xb6          # "Right T Intersection" (=|)
'r'  't'  0xb4          # "Right T Intersection" (-|)
's'  '%'  0xe8          # sha
's'  '='  0xe1          # es
's'  'c'  0xe9          # shcha
's'  'q'  0xfe          # solid square
't'  '='  0xe2          # te
'u'  '='  0xe3          # u
'u'  'L'  0xd6          # "Upper Left Corner" (||~)
'u'  'R'  0xb7          # "Upper Right Corner" (~||))
'u'  'T'  0xd2          # "Upper T Intersection" (~||~)
'u'  'l'  0xda          # "Upper Left Corner" (|~)
'u'  'r'  0xbf          # "Upper Right Corner" (~|)
'u'  't'  0xc2          # "Upper T intersection" (~|~)
'v'  '='  0xa2          # ve
'v'  'l'  0xb3          # "Vertical Line" (-)
'x'  'T'  0xd7          # "Middle Cross Intersection" (-||-)
'x'  't'  0xc5          # "Middle Cross(Intersection" (-|-)
'y'  '='  0xeb          # yeru
'z'  '%'  0xa6          # zhe
'z'  '='  0xa7          # ze
#
#
# The following output section maps '9b' (an ANSI CSI code,
# which unfortunately is also an IBM character) such that
# it prints.
#
output:
0x9b	0x1b 0x9b
#
# scan codes for ru_RU
# Note that the placement of characters in the upper row varies
# between keyboards. If the keyboard layout does not match the
# one below, the user must change the encoding.
# Furthermore, this mapping places the Latin characters in the
# ALT and ALT-SHIFT areas, thus making the toggling unneccessary
scancodes:
#      NORM    SHIFT     ALT   ALT_SHIFT
0x2	'1'	'!'	0x9f	'+'
0x3	'2'	'@'|C	0xff	'!'
0x4	'3'	'#'	'~'	'2'
0x5	'4'	'$'	'/'	'3'
0x6	'5'	'%'	'"'	'4'
0x7	'6'	'^'	':'	'5'
0x8	'7'	'&'	'''	'6'
0x9	'8'	'*'	'.'	'7'
0xa	'9'	'('	'-'	'8'
0xb	'0'	')'	'?'	'9'
0xc	'-'	'_'	'%'	'0'
0xd	'='	'+'	'!'	'='
0x10    0xa9	0x89	'q'	'Q'   CAPS
0x11	0xe6	0x96	'w'	'W'	CAPS
0x12	0xe3	0x93	'e'	'E'	CAPS
0x13	0xaa	0x8a	'r'	'R'	CAPS
0x14	0xa5	0x85	't'	'T'	CAPS
0x15	0xad	0x8d	'y'	'Y'	CAPS
0x16	0xa3	0x83	'u'	'U'	CAPS
0x17	0xe8	0x98	'i'	'I'	CAPS
0x18	0xe9	0x99	'o'	'O'	CAPS
0x19	0xa7	0x87	'p'	'P'	CAPS
0x1a	0xe5	0x95	'['|C	'{'	CAPS
0x1b	0xea	0x9a	']'|C	'}'	CAPS
0x1e	0xe4	0x94	'a'	'A'	CAPS
0x1f	0xeb	0x9b	's'	'S'	CAPS
0x20	0xa2	0x82	'd'	'D'	CAPS
0x21	0xa0	0x80	'f'	'F'	CAPS
0x22	0xaf	0x8f	'g'	'G'	CAPS
0x23	0xe0	0x90	'h'	'H'	CAPS
0x24	0xae	0x8e	'j'	'J'	CAPS
0x25	0xab	0x8b	'k'	'K'	CAPS
0x26	0xa4	0x84	'l'	'L'	CAPS
0x27	0xa6	0x86	';'	':'	CAPS
0x28	0xa7	0x87	'''	'"'	CAPS
0x2b    '\'|C   '|'     '\'|N   '|'|N
0x29    ')'     '('     '`'|N   '~'|N
# the following line is for the 102nd key, if present
0x56    '\'|C   '|'     '\'|N   '|'|N
0x2c    0xef	0x9f	'z'	'Z'	CAPS
0x2d    0xe7	0x97	'x'	'X'	CAPS
0x2e    0xe1	0x91	'c'	'C'	CAPS
0x2f    0xac	0x8c	'v'	'V'	CAPS
0x30    0xa8	0x88	'b'	'B'	CAPS
0x31    0xe2	0x92	'n'	'N'	CAPS
0x32    0xec	0x9c	'm'	'M'	CAPS
0x33    0xa1	0x81	','	'<'	CAPS
0x34    0xee	0x9e	'.'	'>'	CAPS
0x35    0xff	0xff	'/'	'?'	CAPS
#
# map CTRL SHIFT F1  to be 0x18 for the compose character key
F37     0x18
# map CTRL SHIFT F2 to be 0x14 for the toggle key
F38     0x14
