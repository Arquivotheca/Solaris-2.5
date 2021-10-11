# Copyright (c) 1993 Sun Microsystems, Inc.  All Rights Reserved.
#
#	"@(#)el_GR.t	1.1	93/03/02 SMI"
#
# This mapfile is provided as example only. Sun Microsystems Inc.,
# does not warrant that this file is error-free.
#
#	el_GR.t.greek
#
# This mapping file is indended for use with the 437 loadfont.
# It implements a keyboard mapping which allows the use of the
# 437 code set, using the Danish Standards Association's recommended
# short names (but not their "compose" value), such that the "compose" key,
# followed by the 2-letter code, generates the character.
# In addition, more "graphic" combinations are also encoded
# (for instance, E^ in addition to E>, and ?? in addition to ?I
# for inverted question mark). The graphic characters are encoded
# using special names.
#
# Note that this ttymap uses input/output mapping, rather than scan
# codes (as does the el_GR.t map); this is to support e.g. 10+ and other
# programs depending on the ALT+key mapping. If you do not need that,
# the other one supports both Cyrillic and Latin characters at the same
# time. With this one, you must use the toggle key.
#
input:
'`'     '<'
'~'     '>'
'@'     '"'
'#'     0xfa
'q'	0xf3	
'Q'	0xf2
'w'	0xaa	
'W'	0xfb
'e'	0x9c	
'E'	0x84
'r'	0xa8	
'R'	0x90
't'	0xab	
'T'	0x92
'y'	0xac	
'Y'	0x93
'u'	0x9f	
'U'	0x87
'i'	0xa0	
'I'	0x88
'o'	0xa6	
'O'	0x8e
'p'	0xa7	
'P'	0x8f
'a'	0x98	
'A'	0x80
's'	0xa9	
'S'	0x91
'd'	0x9b	
'D'	0x83
'f'	0xad	
'F'	0x94
'g'	0x9a	
'G'	0x82
'h'	0x9e	
'H'	0x86
'j'	0xa5	
'J'	0x8d
'k'	0xa1	
'K'	0x89
'l'	0xa2
'L'	0x8a
'z'	0x9d
'Z'	0x85
'x' 	0xae
'X' 	0x95
'c'	0xaf
'C'	0x96
'v'	0xe0
'V'	0x97
'b'	0x99
'B'	0x81
'n'	0xa4
'N'	0x8c
'm'	0xa3
'M'	0x8b
#
# The toggle key is CTRL t.
#
toggle: 0x14
#
#
dead: '''		# tonos (acute>
'A'	0xea          # ALPHA accent
'E'	0xeb         # EPSILON accent
'H'	0xec          # ETA accent
'I'	0xed          # IOTA accent
'O'	0xee          # OMIKRON accent
'Y'	0xef          # UPSILON accent
'V'	0xf0          # OMEGA accent
'a'	0xe1          # alpha accent
'e'	0xe2         # epsilon accent
'h'	0xe3          # eta accent
'i'	0xe5          # iota accent
'o'	0xe6          # omikron accent
'y'	0xe7          # upsilon accent
'v'	0xe9          # omega accent
#
dead:	0xf8		# diaeresis
'i'	0xe4         # iota diaeresis
'y'	0xe8         # upsilon diaeresis
#
dead: 0xff			# diaeresis and tonos
' '   ' '
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
