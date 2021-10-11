# Copyright (c) 1993 Sun Microsystems, Inc.  All Rights Reserved.
#
#	"@(#)ru_RU.t	1.1	93/03/02 SMI"
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
# Note that this ttymap uses input/output mapping, rather than scan
# codes (as does the ru_RU map); this is to support e.g. 10+ and other
# programs depending on the ALT+key mapping. If you do not need that,
# the other one supports both Cyrillic and Latin characters at the same
# time. With this one, you must use the toggle key.
#
input:
'`'     ')'
'~'     '('
'!'     '+'
'@'     '1'
'3'     '-'
'#'     '2'
'4'     '/'
'$'     '3'
'5'     '"'
'%'     '4'
'6'     ':'
'^'     '5'
'7'     ','
'&'     '6'
'*'     '7'
'8'     '.'
'9'     '_'
'('     '8'
'0'     '?'
')'     '9'
'-'     '%'
'_'     '0'
'='     '!'
'+'     '='
'q'     0xa9
'Q'     0x89
'w'     0xe6
'W'     0x96
'e'     0xe3
'E'     0x93
'r'     0xaa
'R'     0x8a
't'     0xa5
'T'     0x85
'y'     0xad
'Y'     0x8d
'u'     0xa3
'U'     0x83
'i'     0xe8
'I'     0x98
'o'     0xe9
'O'     0x99
'p'     0xa7
'P'     0x87
'['     0xe5
'{'     0x95
']'     0xea
'}'     0x9a
'a'     0xe4
'A'     0x94
's'     0xeb
'S'     0x9b
'd'     0xa2
'D'     0x82
'f'     0xa0
'F'     0x80
'g'     0xaf
'G'     0x8f
'h'     0xe0
'H'     0x90
'j'     0xae
'J'     0x8e
'k'     0xab
'K'     0x8b
'l'     0xa4
'L'     0x84
';'     0xa6
':'     0x86
'''     0xed
'"'     0x9d
'z'     0xef
'Z'     0x9f
'x'     0xe7
'X'     0x97
'c'     0xe1
'C'     0x91
'v'     0xac
'V'     0x8c
'b'     0xa8
'B'     0x88
'n'     0xe2
'N'     0x92
'm'     0xec
'M'     0x9c
','     0xa1
'<'     0x81
'.'     0xee
'>'     0x9e
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
scancodes:
# Scancode section to make sure everything is set for U.S. keyboard
# on a U.S. keyboard.
0x2     '1'     '!'     '1'|N   '!'|N
0x3     '2'     '@'|C   '2'|N   '@'|N
0x4     '3'     '#'     '3'|N   '#'|N
0x5     '4'     '$'     '4'|N   '$'|N
0x6     '5'     '%'     '5'|N   '%'|N
0x7     '6'     '^'|C   '6'|N   '^'|N
0x8     '7'     '&'     '7'|N   '&'|N
0x9     '8'     '*'     '8'|N   '*'|N
0xa     '9'     '('     '9'|N   '('|N
0xb     '0'     ')'     '0'|N   ')'|N
0xc     '-'     '_'|C   '-'|N   '_'|N
0xd     '='     '+'     '='|N   '+'|N
0x10    'q'|C   'Q'|C   'q'|N   'Q'|N   CAPS
0x11    'w'|C   'W'|C   'w'|N   'W'|N   CAPS
0x12    'e'|C   'E'|C   'e'|N   'E'|N   CAPS
0x13    'r'|C   'R'|C   'r'|N   'R'|N   CAPS
0x14    't'|C   'T'|C   't'|N   'T'|N   CAPS
0x15    'y'|C   'Y'|C   'y'|N   'Y'|N   CAPS
0x16    'u'|C   'U'|C   'u'|N   'U'|N   CAPS
0x17    'i'|C   'I'|C   'i'|N   'I'|N   CAPS
0x18    'o'|C   'O'|C   'o'|N   'O'|N   CAPS
0x19    'p'|C   'P'|C   'p'|N   'P'|N   CAPS
0x1a    '['|C   '{'     '['|N   '{'|N   CAPS
0x1b    ']'|C   '}'     ']'|N   '}'|N   CAPS
0x1e    'a'|C   'A'|C   'a'|N   'A'|N   CAPS
0x1f    's'|C   'S'|C   's'|N   'S'|N   CAPS
0x20    'd'|C   'D'|C   'd'|N   'D'|N   CAPS
0x21    'f'|C   'F'|C   'f'|N   'F'|N   CAPS
0x22    'g'|C   'G'|C   'g'|N   'G'|N   CAPS
0x23    'h'|C   'H'|C   'h'|N   'H'|N   CAPS
0x24    'j'|C   'J'|C   'j'|N   'J'|N   CAPS
0x25    'k'|C   'K'|C   'k'|N   'K'|N   CAPS
0x26    'l'|C   'L'|C   'l'|N   'L'|N   CAPS
0x27    ';'     ':'     ';'|N   ':'|N   CAPS
0x28    '''     '"'     '''|N   '"'|N   CAPS
0x2b    '\'|C   '|'     '\'|N   '|'|N
0x29    '`'     '~'|C   '`'|N   '~'|N
# the following line is for the 102nd key, if present
0x56    '<'     '>'     '<'|N   '>'|N
0x2c    'z'|C   'Z'|C   'z'|N   'Z'|N   CAPS
0x2d    'x'|C   'X'|C   'x'|N   'X'|N   CAPS
0x2e    'c'|C   'C'|C   'c'|N   'C'|N   CAPS
0x2f    'v'|C   'V'|C   'v'|N   'V'|N   CAPS
0x30    'b'|C   'B'|C   'b'|N   'B'|N   CAPS
0x31    'n'|C   'N'|C   'n'|N   'N'|N   CAPS
0x32    'm'|C   'M'|C   'm'|N   'M'|N   CAPS
0x33    ','     '<'     ','|N   '<'|N   CAPS
0x34    '.'     '>'     '.'|N   '>'|N   CAPS
0x35    '/'     '?'|C   '/'|N   '?'|N   CAPS
#
# map CTRL SHIFT F1  to be 0x18 for the compose character key
F37     0x18
# map CTRL SHIFT F2 to be 0x14 for the toggle key
F38     0x14
