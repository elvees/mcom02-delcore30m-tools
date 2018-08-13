;
; Copyright 2019 RnD Center "ELVEES", JSC
;
;
; Entry point for ELcore-30M
;

.type _start,@function

.text

.globl __entry
.type __entry,@function
.ffloat
__entry:

    bs _start
    nop

    stop
    nop
