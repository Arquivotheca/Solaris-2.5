# Copyright (c) 1993 Sun Microsystems, Inc.  All Rights Reserved.
#
#	"@(#)ru_RU.t	1.1	93/03/02 SMI"
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
# Note that this ttymap uses input/output mapping, rather than scan
# codes (as does the ru_RU map); this is to support e.g. 10+ and other
# programs depending on the ALT+key mapping. If you do not need that,
# the other one supports both Cyrillic and Latin characters at the same
# time. With this one, you must use the toggle key.
#
input:
'`'     ')'
'~'     '('
'1'     0xfd
'!'     '+'
'2'     0xf0
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
'q'     0xd9
'Q'     0xb9
'w'     0xe6
'W'     0xc6
'e'     0xe3
'E'     0xc3
'r'     0xda
'R'     0xba
't'     0xd5
'T'     0xb5
'y'     0xdd
'Y'     0xbd
'u'     0xd3
'U'     0xb3
'i'     0xe8
'I'     0xc8
'o'     0xe9
'O'     0xc9
'p'     0xd7
'P'     0xb7
'['     0xe5
'{'     0xc5
']'     0xea
'}'     0xca
'a'     0xe4
'A'     0xc4
's'     0xeb
'S'     0xcb
'd'     0xd2
'D'     0xb2
'f'     0xd0
'F'     0xb0
'g'     0xdf
'G'     0xbf
'h'     0xe0
'H'     0xc0
'j'     0xde
'J'     0xbe
'k'     0xdb
'K'     0xbb
'l'     0xd4
'L'     0xb4
';'     0xd6
':'     0xb6
'''     0xed
'"'     0xcd
'z'     0xef
'Z'     0xcf
'x'     0xe7
'X'     0xc7
'c'     0xe1
'C'     0xc1
'v'     0xdc
'V'     0xbc
'b'     0xd8
'B'     0xb8
'n'     0xe2
'N'     0xc2
'm'     0xec
'M'     0xcc
','     0xd1
'<'     0xb1
'.'     0xee
'>'     0xce
'/'     0xf1
'?'     0xa1
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
