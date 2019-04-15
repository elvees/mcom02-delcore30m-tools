;
; Copyright 2019 RnD Center "ELVEES", JSC
;
;
; Entry point for ELcore-30M
;

.type _start,@function
.type _interrupt_handler,@function

.text

.globl __entry
.align	4
.type __entry,@function
__entry:
        js _start

        nop
        stop


; ************************************ _inter_hnlr ********************
; description: DSP interrupt handler, its address must be written in
;                IVAR register
;
; *********************************************************************
 .globl	_inter_hnlr
 .align	4
 .type	_inter_hnlr,@function
__inter_hnlr:
        ; ---------------------  save context -----------------------------
        ; !!! do not edit till 'interrupt handler code' block
        ;
        nop
        nop
        nop
        nop

        ;FIXME: It is used instead of global variables
        move 0x8000, A0.l
        b _save_regs
        nop

_end_save:
        ; ---------------------  interrupt handler code ----------------------
        ; add interrupt cause analysis here, function call to be execukted in
        ;  interrupt handler and so on accordingly to specific of application
        ;
        ; cause of interrupt is in memory, address  _IRQR_DUMP
        ; --------------------------------------------------------------------

        ; call "interrupt" function
        ; place your own function name here
        js  _interrupt_handler
        nop

        move 0, IRQR.l

        ; ---------------------  restore context -----------------------------
        ; do not edit this block of interrupt handler
        ;

        ;FIXME: It is used instead of global variables
        move 0x8000, A0.l
        b _restore_regs
        nop

       ; move R31.l, A0.l

_end_restore:
        nop
        nop
        nop
        rti
        nop


; ************************************ _save_regs *********************
; description: it saves context in interrupt handler
; inputs: a0 - contains address of memory to store problem context
;
; ********************************************************************
_save_regs:
        ;
        ;Stack pointer
        move A6.l, R30.l
        move R30.l, (A0)+                ;A6                                [0]
        ;Frame pointer
        move A7.l, R30.l
        move R30.l, (A0)+                ;A7                                [1]
        move m0, R30.s
        move m1, R31.s
        move R30.l, (A0)+                ;m0 m1                        [2]
        move m2, R30.s
        move m3, R31.s
        move R30.l, (A0)+                ;m2 m3                        [3]
        move m4, R30.s
        move m5, R31.s
        move R30.l, (A0)+                ;m4 m5                        [4]
        move m6, R30.s
        move m7, R31.s
        move R30.l, (A0)+                ;m6 m7                        [5]
        move mt, R30.s
        move dt, R31.s
        move R30.l, (A0)+                ;MT DT                        [6]

        move sr, R30.s
        move ccr, R31.s
        move R30.l, (A0)+                ;SR CCR                        [7]
        move pdnr, R30.s
        move it, R31.s
        move R30.l, (A0)+                ;PDNR IT                [8]
        move sfr.l, R30.l
        move R30.l, (A0)+                ;SFR                        [9]
        move R31.l, R30.l
        move R30.l, (A0)+                ;A0                                [10]
        move A1.l, R30.l
        move R30.l, (A0)+                ;A1                                [11]
        move A2.l, R30.l
        move R30.l, (A0)+                ;A2                                [12]
        move A3.l, R30.l
        move R30.l, (A0)+                ;A3                                [13]
        move A4.l, R30.l
        move R30.l, (A0)+                ;A4                                [14]
        move A5.l, R30.l
        move R30.l, (A0)+                ;A5                                [15]
        move i0, R30.s
        move i1, R31.s
        move R30.l, (A0)+                ;i0 i1                        [16]
        move i2, R30.s
        move i3, R31.s
        move R30.l, (A0)+                ;i2 i3                        [17]
        move i4, R30.s
        move i5, R31.s
        move R30.l, (A0)+                ;i4 i5                        [18]
        move i6, R30.s
        move i7, R31.s
        move R30.l, (A0)+                ;i6 i7                        [19]
        move AC0.l, R30.l
        move R30.l, (A0)+                ;AC0                        [20]
        move AC1.l, R30.l
        move R30.l, (A0)+                ;AC1                        [21]
        move AC2.l, R30.l
        move R30.l, (A0)+                ;AC2                        [22]
        move AC3.l, R30.l
        move R30.l, (A0)+                ;AC3                        [23]
        move AC4.l, R30.l
        move R30.l, (A0)+                ;AC4                        [24]
        move AC5.l, R30.l
        move R30.l, (A0)+                ;AC5                        [25]
        move AC6.l, R30.l
        move R30.l, (A0)+                ;AC6                        [26]
        move AC7.l, R30.l
        move R30.l, (A0)+                ;AC7                        [27]
        move AC8.l, R30.l
        move R30.l, (A0)+                ;AC8                        [28]
        move AC9.l, R30.l
        move R30.l, (A0)+                ;AC9                        [29]
        move AC10.l, R30.l
        move R30.l, (A0)+                ;AC10                        [30]
        move AC11.l, R30.l
        move R30.l, (A0)+                ;AC11                        [31]
        move AC12.l, R30.l
        move R30.l, (A0)+                ;AC12                        [32]
        move AC13.l, R30.l
        move R30.l, (A0)+                ;AC13                        [33]
        move AC14.l, R30.l
        move R30.l, (A0)+                ;AC14                        [34]
        move AC15.l, R30.l
        move R30.l, (A0)+                ;AC15                        [35]
        move R0.q, (A0)+                ;R0.q                        [36-39]
        move R2.q, (A0)+                ;R2.q                        [40-43]
        move R4.q, (A0)+                ;R4.q                        [44-47]
        move R6.q, (A0)+                ;R6.q                        [48-51]
        move R8.q, (A0)+                ;R8.q                        [52-55]
        move R10.q, (A0)+                ;R10.q                        [56-59]
        move R12.q, (A0)+                ;R12.q                        [60-63]
        move R14.q, (A0)+                ;R14.q                        [64-67]
        move R16.q, (A0)+                ;R16.q                        [68-71]
        move R18.q, (A0)+                ;R18.q                        [72-75]
        move R20.q, (A0)+                ;R20.q                        [76-79]
        move R22.q, (A0)+                ;R22.q                        [80-83]
        move R24.q, (A0)+                ;R24.q                        [84-87]
        move R24.q, (A0)+                ;R26.q                        [88-91]
        move R24.q, (A0)+                ;R28.q                        [92-95]
        move at.l, R30.l
        move R30.l, (A0)+                ;AT                                [96]

        clrl R4.l
        clrl R30.l
        move sp, R30.s
        and 0xF, R30.s
        move R30.l, (A0)+
_start_save_ss:
        cmp 0x0, R30
        b.eq _end_save_ss
        nop
        dec R30.s, R30.s
        move ss, R4.s
        move R4.l, (A0)+
        b _start_save_ss
        nop
_end_save_ss:
        move la, R4.s
        move lc, R5.s
        move R4.l, (A0)+
        move sp, R30.s
        lsr 0x8, R30.s, R30.s
        and 0x7, R30
        move R30.l, (A0)+
_start_save_cs:
        cmp 0x0, R30
        b.eq _end_save_cs
        nop
        dec R30, R30
        move csh, R4.s
        move csl, R5.s
        move R4.l, (A0)+
        b _start_save_cs
        nop
_end_save_cs:
        b _end_save
        nop


; ************************************ _restore_regs *****************
; description: it restores context in interrupt handler
;
; ********************************************************************
_restore_regs:

        move A0.l, R4.l
        move R4.l, R30.l
        addl 0x61, R30.l
        move R30.l, A0.l

        move (A0), R30.l
        andl 0xF, R30.l
        cmpl 0x0, R30.l
        b.eq _end_restore_ss
        nop
        move A0.l, R0.l
        addl R30.l, R0.l
        move R0.l, A0.l
_start_restore_ss:
        move (A0)-, R2.l
        move R2.s, ss
        dec R30, R30
        cmp 0x0, R30
        b.ne _start_restore_ss
        nop
_end_restore_ss:
        addl 0x1, R0.l
        move R0.l, A0.l
        move (A0)+, R30.l
        move R30.s, la
        move R31.s, lc
        move (A0), R30.l
        andl 0x7, R30.l
        cmpl 0x0, R30.l
        b.eq _end_restore_cs
        nop
        addl R30.l, R0.l
        move R0.l, A0.l
_start_restore_cs:
        move (A0)-, R2.l
        move R2.s, csh
        move R3.s, csl
        dec R30, R30
        cmp 0x0, R30
        b.ne _start_restore_cs
        nop
_end_restore_cs:
        move R4.l, A0.l

        ;Stack pointer
        move (A0)+, R30.l                 ;A6                                [0]
        move R30.l, A6.l
        ;Frame pointer
        move (A0)+, R30.l                 ;A7                                [1]
        move R30.l, A7.l
        move (A0)+, R30.l                ;m0 m1                        [2]
        move R30.s, m0
        move R31.s, m1
        move (A0)+, R30.l                ;m2 m3                        [3]
        move R30.s, m2
        move R31.s, m3
        move (A0)+, R30.l                ;m4 m5                        [4]
        move R30.s, m4
        move R31.s, m5
        move (A0)+, R30.l                ;m6 m7                        [5]
        move R30.s, m6
        move R31.s, m7
        move (A0)+, R30.l                ;MT DT                        [6]
        move R30.s, mt
        move R31.s, dt

        move (A0)+, R30.l                ;SR CCR                        [7]
        move R30.s, sr
        move R31.s, ccr
        move (A0)+, R30.l                ;PDNR IT                [8]
        move R30.s, pdnr
        move R31.s, it
        move (A0)+, R30.l                 ;SFR                        [9]
        move R30.l, sfr.l
        move (A0)+, R30.l                 ;A0                                [10]
        move R30.l, R31.l
        move (A0)+, R30.l                 ;A1                                [11]
        move R30.l, A1.l
        move (A0)+, R30.l                 ;A2                                [12]
        move R30.l, A2.l
        move (A0)+, R30.l                 ;A3                                [13]
        move R30.l, A3.l
        move (A0)+, R30.l                 ;A4                                [14]
        move R30.l, A4.l
        move (A0)+, R30.l                 ;A5                                [15]
        move R30.l, A5.l
        move (A0)+, R30.l                 ;i0 i1                        [16]
        move R30.s, i0
        move R31.s, i1
        move (A0)+, R30.l                 ;i2 i3                        [17]
        move R30.s, i2
        move R31.s, i3
        move (A0)+, R30.l                 ;i4 i5                        [18]
        move R30.s, i4
        move R31.s, i5
        move (A0)+, R30.l                 ;i6 i7                        [19]
        move R30.s, i6
        move R31.s, i7
        move (A0)+, R30.l                ;AC0                        [20]
        move R30.l, AC0.l
        move (A0)+, R30.l                ;AC1                        [21]
        move R30.l, AC1.l
        move (A0)+, R30.l                ;AC2                        [22]
        move R30.l, AC2.l
        move (A0)+, R30.l                ;AC3                        [23]
        move R30.l, AC3.l
        move (A0)+, R30.l                ;AC4                        [24]
        move R30.l, AC4.l
        move (A0)+, R30.l                ;AC5                        [25]
        move R30.l, AC5.l
        move (A0)+, R30.l                ;AC6                        [26]
        move R30.l, AC6.l
        move (A0)+, R30.l                ;AC7                        [27]
        move R30.l, AC7.l
        move (A0)+, R30.l                ;AC8                        [28]
        move R30.l, AC8.l
        move (A0)+, R30.l                ;AC9                        [29]
        move R30.l, AC9.l
        move (A0)+, R30.l                ;AC10                        [30]
        move R30.l, AC10.l
        move (A0)+, R30.l                ;AC11                        [31]
        move R30.l, AC11.l
        move (A0)+, R30.l                ;AC12                        [32]
        move R30.l, AC12.l
        move (A0)+, R30.l                ;AC13                        [33]
        move R30.l, AC13.l
        move (A0)+, R30.l                ;AC14                        [34]
        move R30.l, AC14.l
        move (A0)+, R30.l                ;AC15                        [35]
        move R30.l, AC15.l
        move (A0)+, R0.q                ;R0.q                        [36-39]
        move (A0)+, R2.q                ;R2.q                        [40-43]
        move (A0)+, R4.q                ;R4.q                        [44-47]
        move (A0)+, R6.q                ;R6.q                        [48-51]
        move (A0)+, R8.q                ;R8.q                        [52-55]
        move (A0)+, R10.q                ;R10.q                        [56-59]
        move (A0)+, R12.q                ;R12.q                        [60-63]
        move (A0)+, R14.q                ;R14.q                        [64-67]
        move (A0)+, R16.q                ;R16.q                        [68-71]
        move (A0)+, R18.q                ;R18.q                        [72-75]
        move (A0)+, R20.q                ;R20.q                        [76-79]
        move (A0)+, R22.q                ;R22.q                        [80-83]
        move (A0)+, R24.q                ;R24.q                        [84-87]
        move (A0)+, R24.q                ;R26.q                        [88-91]
        move (A0)+, R24.q                ;R28.q                        [92-95]
        move (A0)+, R30.l                ;AT                                [96]
        move R30.l, at.l
        b _end_restore
        nop
