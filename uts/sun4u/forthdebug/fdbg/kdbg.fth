
\ "@(#)kdbg.fth	1.8	95/06/13 SMI"

hex

only forth also definitions
vocabulary kdbg-words
also kdbg-words definitions

: next-word ( alf voc-acf -- false | alf' true )
   over  if  drop  else  nip >threads  then
   another-link?  if  >link true  else  false  then
;

\ another? that allows nesting
: another? ( alf voc-acf -- false | alf' voc-acf anf true )
   dup >r next-word  if         ( alf' ) ( r: voc-acf )
      r> over l>name true       ( alf' voc-acf anf true )
   else                         ( ) ( r: voc-acf )
      r> drop false             ( false )
   then
;


create err-no-sym ," symbol not found"

\ guard against bad symbols
: symbol ( -- n ) \ symbol-name
   parse-word $handle-literal? 0=  if
      +level compile err-no-sym compile throw -level
   then
; immediate


\ print in octal
: .o ( n -- ) base @ >r octal . r> base ! ;

\ print string
: .str ( str -- )
   ?dup  if
      cscount type
   else
      ." NULL"
   then
;

\ new actions
: print 2 perform-action ;
: index 3 perform-action ;
: sizeof 1 perform-action ;

\ indent control
-8 value plevel
: +plevel ( -- ) plevel 8 + to plevel ;
: -plevel ( -- ) plevel 8 - to plevel ;
: 0plevel ( -- ) -8 to plevel ;

\ new print words
: name-print ( apf -- apf ) plevel spaces dup body> .name ." = " ;
: voc-print ( addr acf -- )
   ??cr +plevel
   0 swap                         ( addr 0 acf )
   begin  another?  while         ( addr alf acf anf )
      3 pick swap name> print     ( addr alf acf )
      exit?  if                   ( addr alf acf )
         0plevel true throw       ( )
      then                        ( addr alf acf )
   repeat                         ( addr )
   drop -plevel                   ( )
;


3 actions ( offset print-acf )
action: ( addr apf -- x )       @ + x@ ;        \ get
action: ( addr x apf -- )       @ rot + x! ;    \ set
action: ( addr apf -- )
   name-print
   dup @ rot + x@ swap         ( x apf )
   na1+ @ execute cr ;                          \ print

: ext-field ( acf offset -- ) create , , use-actions ;


3 actions ( offset print-acf )
action: ( addr apf -- l )       @ + l@ ;        \ get
action: ( addr l apf -- )       @ rot + l! ;    \ set
action: ( addr apf -- )
   name-print
   dup @ rot + l@ swap          ( l apf )
   na1+ @ execute cr ;                          \ print

: long-field ( acf offset -- ) create , , use-actions ;


3 actions ( offset print-acf )
action: ( addr apf -- w )       @ + w@ ;        \ get
action: ( addr w apf -- )       @ rot + w! ;    \ set
action: ( addr apf -- )
   name-print
   dup @ rot + w@ swap          ( w apf )
   na1+ @ execute cr ;                          \ print

: short-field ( acf offset -- ) create , , use-actions ;


3 actions ( offset print-acf )
action: ( addr apf -- c )       @ + c@ ;        \ get
action: ( addr c apf -- )       @ rot + c! ;    \ set
action: ( addr apf -- )
   name-print
   dup @ rot + c@ swap          ( c apf )
   na1+ @ execute cr ;                          \ print

: byte-field ( acf offset -- ) create , , use-actions ;


3 actions ( offset print-acf )
action: ( addr apf -- ptr )     @ + l@ ;        \ get
action: ( addr l apf -- )       @ rot + l! ;    \ set
action: ( addr apf -- )
   name-print
   dup @ rot + l@ ?dup  if     ( apf ptr )
      swap na1+ @ execute      ( )
   else                        ( apf )
      drop ." NULL"            ( )
   then                        ( )
   cr ;                                         \ print
 
: ptr-field ( acf offset -- ) create , , use-actions ;
 

3 actions ( offset print-acf )
action: ( addr apf -- saddr )   @ + ;           \ get
action: ( -- )                  quit ;          \ error
action: ( addr apf -- )
   name-print
   dup @ rot + swap             ( saddr apf )
   na1+ @ execute ??cr ;                       \ print
 
: struct-field ( acf offset -- ) create , , use-actions ;


4 actions ( offset inc limit print-acf fetch-acf )
action: ( addr apf -- araddr )  @ + ;           \ get
action: ( -- )                  quit ;          \ set
action: ( addr apf -- )
   name-print
   dup @ rot + swap         ( base apf )
   na1+ dup @ -rot          ( inc base apf' )
   na1+ dup @ swap          ( inc base limit apf' )
   na1+ dup @ swap          ( inc base limit p-acf apf' )
   na1+ @ 2swap             ( inc p-acf f-acf base limit )
   bounds  do               ( inc p-acf f-acf )
      3dup                  ( inc p-acf f-acf inc p-acf f-acf )
      i swap execute        ( inc f-acf p-acf inc p-acf n )
      swap execute          ( inc f-acf p-acf inc )
   +loop                    ( inc f-acf p-acf )
   3drop ??cr ;                                 \ print
action: ( addr index apf -- ith-item )
   rot swap                 ( index addr apf )
   dup @ rot + swap         ( index base apf )
   na1+ dup @ 3 roll *      ( base apf' ioff )
   rot + swap 3 na+ @       ( iaddr f-acf )
   execute ;                                    \ index

: array-field ( f-acf p-acf limit inc offset -- ) create , , , , , use-actions ;


3 actions ( offset mask shift print-acf )
action: ( addr apf -- bits )
   dup @ rot + l@ swap         ( b-word apf )
   na1+ dup @ rot and swap     ( b-masked apf' )
   na1+ @ >> ;                               \ get
action: ( addr bits apf -- )
   rot over @ + dup l@ 2swap   ( b-addr b-word nbits apf )
   na1+ dup @ -rot             ( b-addr b-word mask nbits apf' )
   na1+ @ << over and          ( b-addr b-word mask nb-masked )
   -rot invert and or swap l! ;              \ set
action: ( addr apf -- )
   name-print
   dup @ rot + l@ swap         ( b-word apf )
   na1+ dup @ rot and swap     ( b-mask apf' )
   na1+ dup @ rot swap >> swap ( bits apf' )
   na1+ @ execute cr ;                       \ print

: bits-field ( acf shift mask offset -- ) create , , , , use-actions ;


2 actions ( voc-acf size )
action: ( apf -- )              @ voc-print ;   \ print vocabulary
action: ( apf -- size )         na1+ @ ;        \ sizeof

: c-struct ( size acf -- ) create , , use-actions ;

: c-enum ( {str value}+ n-values -- )
   create   ( n-values {value str}+ )
      dup 2* 1+ 0  do  ,  loop
   does>    ( enum apf -- )
      dup @ 0  do                     ( enum apf' )
         na1+ 2dup @ =  if            ( enum apf' )
            na1+ @ .str               ( enum )
            drop unloop exit          ( )
         then                         ( enum apf' )
         na1+                         ( enum apf' )
      loop                            ( enum apf' )
      drop .d cr                      ( )
;

\ end kdbg section
