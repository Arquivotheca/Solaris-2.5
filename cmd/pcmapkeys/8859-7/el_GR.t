# Copyright (c) 1993 Sun Microsystems, Inc.  All Rights Reserved.
#
#	"@(#)el_GR.t	1.1	93/03/02 SMI"
#
# This mapfile is provided as example only. Sun Microsystems Inc.,
# does not warrant that this file is error-free.
#
#	el_GR.t.8859-7
#
# This mapping file is indended for use with the 8859-7 loadfont.
# It implements a keyboard mapping which allows the use of the
# 8859-7 code set, using the Danish Standards Association's recommended
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
'#'     0xb7
'e'	0xe5	
'E'	0xc5
'r'	0xf1	
'R'	0xd1
't'	0xf4	
'T'	0xd4
'y'	0xf5	
'Y'	0xd5
'u'	0xe8	
'U'	0xc8
'i'	0xe9	
'I'	0xc9
'o'	0xef	
'O'	0xcf
'p'	0xf0	
'P'	0xd0
'a'	0xe1	
'A'	0xc1
's'	0xf3	
'S'	0xd3
'd'	0xe4	
'D'	0xc4
'f'	0xf6	
'F'	0xd6
'g'	0xe3	
'G'	0xc3
'h'	0xe7	
'H'	0xc7
'j'	0xee	
'J'	0xce
'k'	0xea	
'K'	0xca
'l'	0xeb
'L'	0xcb
'"'     0xa8
'z'	0xe6
'Z'	0xc6
'x' 	0xf7
'X' 	0xd7
'c'	0xf8
'C'	0xd8
'v'	0xf9
'V'	0xd9
'b'	0xe2
'B'	0xc2
'n'	0xed
'N'	0xcd
'm'	0xec
'M'	0xcc
#
# The toggle key is CTRL t.
#
toggle: 0x14
#
#
dead: '''		# tonos (acute>
'A'	0xb6          # ALPHA accent
'E'	0xb8         # EPSILON accent
'H'	0xb9          # ETA accent
'I'	0xba          # IOTA accent
'O'	0xbc          # OMIKRON accent
'Y'	0xbe          # UPSILON accent
'V'	0xbf          # OMEGA accent
'a'	0xdc          # alpha accent
'e'	0xdd         # epsilon accent
'h'	0xde          # eta accent
'i'	0xdf          # iota accent
'o'	0xfc          # omikron accent
'y'	0xfd          # upsilon accent
'v'	0xfe          # omega accent
#
dead:	0xa8		# diaeresis
'I'	0xda         # IOTA diaeresis
'Y'	0xdb         # UPSILON diaeresis
'i'	0xfa         # iota diaeresis
'y'	0xfb         # upsilon diaeresis
#
dead: 0xb5			# diaeresis and tonos
' '   ' '
'i'	0xc0         # iota diaeresis & accent
'y'	0xe0         # upsilon diaresis & accent
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
