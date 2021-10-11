# Copyright (c) 1993 Sun Microsystems, Inc.  All Rights Reserved.
#
#	"@(#)ru_RU.s	1.1	93/03/02 SMI"
#
# This mapfile is provided as example only. Sun Microsystems Inc.,
# does not warrant that this file is error-free.
#
#	ru_RU.8859-5
#
# This mapping file is indended for use with the 8859-5 loadfont.
# It implements a keyboard mapping which allows the use of the
# 8859-5 code set, using the Danish Standards Association's recommended
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
' '  ' '  0xa0          # no breaking space 
'$'  '$'  0xfd          # paragraph sign
'%'  '"'  0xcc          # SOFT SIGN
'%'  '''  0xec          # soft sign
'''  ' '  '''          # apostrophe
'('  'U'  0x98          # intersection
'-'  '-'  0xad          # soft hyphen
'='  '"'  0xca          # HARD SIGN
'='  '''  0xea          # hard sign
'A'  '='  0xb0          # A
'B'  '='  0xb1          # BE
'C'  '%'  0xc7          # CHE
'C'  '='  0xc6          # TSE
'D'  '%'  0xa2          # DJE (Serbocroatian)
'D'  '='  0xb4          # DE
'D'  'S'  0xa5          # DZE (Macedonian)
'D'  'Z'  0xaf          # DZHE
'E'  '='  0xb5          # IE
'F'  '='  0xc4          # EF
'G'  '%'  0xa3          # GJE (Macedonian)
'G'  '='  0xb3          # GHE
'H'  '='  0xc5          # HA
'I'  '='  0xb8          # I
'I'  'E'  0xa4          # IE (Ukrainian)
'I'  'I'  0xa6          # I (Byelorussian-Ukrainian)
'I'  'O'  0xa1          # IO
'J'  '%'  0xa8          # JE
'J'  '='  0xb9          # SHORT I
'J'  'A'  0xcf          # YA
'J'  'E'  0xcd          # E
'J'  'U'  0xce          # YU
'K'  '='  0xba          # KA
'K'  'J'  0xac          # KJE (Macedonian)
'L'  '='  0xbb          # EL
'L'  'J'  0xa9          # LJE
'M'  '='  0xbc          # EM
'N'  '='  0xbd          # EN
'N'  'J'  0xaa          # NJE
'N'  'S'  0xa0          # no breaking space 
'O'  '='  0xbe          # O
'P'  '='  0xbf          # PE
'R'  '='  0xc0          # ER
'S'  '%'  0xc8          # SHA
'S'  '='  0xc1          # ES
'S'  'E'  0xfd          # paragraph sign
'S'  'c'  0xc9          # SHCHA
'T'  '='  0xc2          # TE
'T'  's'  0xab          # TSHE (Serbocroatian)
'U'  '='  0xc3          # U
'V'  '%'  0xae          # SHORT U (Byelorussian)
'V'  '='  0xb2          # VE
'Y'  '='  0xcb          # YERU
'Y'  'I'  0xa7          # YI (Ukrainian)
'Z'  '%'  0xb6          # ZHE
'Z'  '='  0xb7          # ZE
'a'  '='  0xd0          # a
'b'  '='  0xd1          # be
'b'  't'  0x94          # "Bottom T intersection" (_|_)
'c'  '%'  0xe7          # che
'c'  '='  0xe6          # tse
'd'  '%'  0xf2          # dje (Serbocroatian)
'd'  '='  0xd4          # de
'd'  's'  0xf5          # dze (Macedonian)
'd'  'z'  0xff          # dzhe
'e'  '='  0xd5          # ie
'f'  '='  0xe4          # ef
'g'  '%'  0xf3          # gje (Macedonian)
'g'  '='  0xd3          # ghe
'h'  '='  0xe5          # ha
'h'  'l'  0x97          # "Horizontal Line"
'i'  '='  0xd8          # i
'i'  'e'  0xf4          # ie (Ukrainian)
'i'  'i'  0xf6          # i (Byelorussian-Ukrainian)
'i'  'o'  0xf1          # io
'j'  '%'  0xf8          # je
'j'  '='  0xd9          # short i
'j'  'a'  0xef          # ya
'j'  'e'  0xed          # e
'j'  'u'  0xee          # yu
'k'  '='  0xda          # ka
'k'  'j'  0xfc          # kje (Macedonian)
'l'  '='  0xdb          # el
'l'  'j'  0xf9          # lje
'l'  'l'  0x93          # "Lower Left Corner" (|_)
'l'  'r'  0x99          # "Lower Right Corner" (_|)
'l'  't'  0x96          # "Left T Intersection" (|-)
'm'  '='  0xdc          # em
'n'  '='  0xdd          # en
'n'  'j'  0xfa          # nje
'o'  '='  0xde          # o
'p'  '='  0xdf          # pe
'r'  '='  0xe0          # er
'r'  't'  0x91          # "Right T Intersection" (-|)
's'  '%'  0xe8          # sha
's'  '='  0xe1          # es
's'  'c'  0xe9          # shcha
't'  '='  0xe2          # te
't'  's'  0xfb          # tshe (Serbocroatian)
'u'  '='  0xe3          # u
'u'  'l'  0x9a          # "Upper Left Corner" (|~)
'u'  'r'  0x92          # "Upper Right Corner" (~|)
'u'  't'  0x95          # "Upper T intersection" (~|~)
'u'  '%'  0xfe          # short u (Byelorussian)
'v'  '='  0xd2          # ve
'v'  'l'  0x90          # "Vertical Line" (-)
'x'  't'  0x98          # "Middle Cross(Intersection" (-|-)
'y'  '='  0xeb          # yeru
'y'  'i'  0xf7          # yi (Ukrainian)
'z'  '%'  0xd6          # zhe
'z'  '='  0xd7          # ze
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
0x2	'1'	'!'	0xfd	'+'
0x3	'2'	'@'|C	0xf0	'!'
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
0x10    0xd9	0xb9	'q'	'Q'   CAPS
0x11	0xe6	0xc6	'w'	'W'	CAPS
0x12	0xe3	0xc3	'e'	'E'	CAPS
0x13	0xda	0xba	'r'	'R'	CAPS
0x14	0xd5	0xb5	't'	'T'	CAPS
0x15	0xdd	0xbd	'y'	'Y'	CAPS
0x16	0xd3	0xb3	'u'	'U'	CAPS
0x17	0xe8	0xc8	'i'	'I'	CAPS
0x18	0xe9	0xc9	'o'	'O'	CAPS
0x19	0xd7	0xb7	'p'	'P'	CAPS
0x1a	0xe5	0xc5	'['|C	'{'	CAPS
0x1b	0xea	0xca	']'|C	'}'	CAPS
0x1e	0xe4	0xc4	'a'	'A'	CAPS
0x1f	0xeb	0xcb	's'	'S'	CAPS
0x20	0xd2	0xb2	'd'	'D'	CAPS
0x21	0xd0	0xb0	'f'	'F'	CAPS
0x22	0xdf	0xbf	'g'	'G'	CAPS
0x23	0xe0	0xc0	'h'	'H'	CAPS
0x24	0xde	0xbe	'j'	'J'	CAPS
0x25	0xdb	0xbb	'k'	'K'	CAPS
0x26	0xd4	0xb4	'l'	'L'	CAPS
0x27	0xd6	0xb6	';'	':'	CAPS
0x28	0xd7	0xb7	'''	'"'	CAPS
0x2b    '\'|C   '|'     '\'|N   '|'|N
0x29    ')'     '('     '`'|N   '~'|N
# the following line is for the 102nd key, if present
0x56    '\'|C   '|'     '\'|N   '|'|N
0x2c    0xef	0xcf	'z'	'Z'	CAPS
0x2d    0xe7	0xc7	'x'	'X'	CAPS
0x2e    0xe1	0xc1	'c'	'C'	CAPS
0x2f    0xdc	0xbc	'v'	'V'	CAPS
0x30    0xd8	0xb8	'b'	'B'	CAPS
0x31    0xe2	0xc2	'n'	'N'	CAPS
0x32    0xec	0xcc	'm'	'M'	CAPS
0x33    0xd1	0xb1	','	'<'	CAPS
0x34    0xee	0xce	'.'	'>'	CAPS
0x35    0xf1	0xa1	'/'	'?'	CAPS
#
# map CTRL SHIFT F1  to be 0x18 for the compose character key
F37     0x18
# map CTRL SHIFT F2 to be 0x14 for the toggle key
F38     0x14
