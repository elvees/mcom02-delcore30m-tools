;
; Copyright 2018-2019 RnD Center "ELVEES", JSC
;
;
; Inverse input image on ELcore-30M
;

.text
imagetest:
	js _main

	nop
	stop

imagetest_interrupt:
;-----save context---
	move R0.l, R20.l

	move A3.l, R31.l

	move A7.l, R0.l
	subl 0x200, R0.l
	move R0.l, A3.l
	b _save_regs;
	nop
_end_save:
;--------------------
	;clear IRQR
	move 0, IRQR.l

;------read INTMIS------
	move 0x37220028, r0.l
	lsrl 0x2, r0.l
	move r0.l, a0
	move (a0), r0.l
;-------------------------

;get interrupt number
	move r24.s, r12.s
	btstl r12.s, r0.l
	j.cs _reset_intclr

	move r25.s, r12.s
	btstl r12.s, r0.l
	j.cc _interrupt_end

_reset_intclr:
	move 0x3722002C, r2.l
	lsrl 0x2, r2.l
	move r2.l, a0.l
	move 0x1, r0.l
	lsll r12.s, r0.l
	move r0.l, (a0)
;DMA channel now is free. Reset bit in R8
	andc r0.s, r8.s

_interrupt_end:
;------restore context----
	move A7.l, R0.l
	subl 0x200, R0.l
	move R0.l, A3.l
	b _restore_regs
	nop
_end_restore:
	move R20.l, R0.l
	rti
	nop

_main:

	andl 0xFFFFF, R4.l
	asrl 2, R4.l, R4.l

	move 0, r6.l
address_transform:
	move A7, R0.l
	addl r6.l, R0.l
	move R0.l, A0
	move (A0), R0.l
	andl 0xFFFFF, R0.l
	asrl 2, R0.l, R0.l
	move R0.l, (A0)

	addl 2, r6.l, r6.l

	cmpl 18, r6.l
	b.ne address_transform

	;tile_count
	move (A7+12), R6.l
	move R6.l, A0.l
	move (A0.l), R6.l
	move R6.l, R10.l

	;channel0 number
	move (A7+4), R14.l
	move R14.l, A0.l
	move (A0.l), R14.l
	move R14.s, R24.s
	;channel1 number
	move (A0.l+1), R14.l
	move R14.s, R25.s

;DMA transfer ready register
	clrl R8.l

	move R24.s, R12.s
	js _send_event_to_dma_

	move R24.s, R12.s
	js _wait_dma_
	cmpl 1, R6.l
	js.ne _send_event_to_dma_

	move (A7+2), R0.l
	move R0.l, A0
	move R0.l, AT
	js _calculate

	move R25.s, R12.s
	js _send_event_to_dma_
	move R25.s, R12.s
	js _wait_dma_

	DECL R6.l, R6.l
	cmpl 0, R6.l
	b.eq _main_end_

start_loop:
	move R24.s, R12.s
	js _wait_dma_
	cmpl 1, R6.l
	js.ne _send_event_to_dma_

	move (A7+6), R0.l
	move R0.l, A0
	move R0.l, AT
	js _calculate

	move R25.s, R12.s
	js _send_event_to_dma_
	move R25.s, R12.s
	js _wait_dma_

	DECL R6.l, R6.l
	cmpl 0, R6.l
	b.eq _main_end_

	move R24.s, R12.s
	js _wait_dma_
	cmpl 1, R6.l
	js.ne _send_event_to_dma_

	move (A7+2), R0.l
	move R0.l, A0
	move R0.l, AT
	js _calculate

	move R25.s, R12.s
	js _send_event_to_dma_
	move R25.s, R12.s
	js _wait_dma_

	DECL R6.l, R6.l
	cmpl 0, R6.l
	b.ne start_loop
_main_end_:

	js _wait_all_dma_channels

	nop
	rts

_send_event_to_dma_:
	;clear mask interrupt
	move imaskr, r22.l
	clrl r28.l
	move r28.l, imaskr

	;DMA channel now is busy
	move 1, r0.l
	lsll r12.s, r0.l, r0.l
	or r0.s, r8.s
	;-----------------------

	move 0x37220D04, r0.l
	lsrl 0x2, r0.l
	move r0.l, a2

	addl 0x8, r12.l
	lsll 27, r12.l
	addl 0x340000, r12.l
	move r12.l, r0.l

	move 0x38081804, r28.l
	lsrl 0x2, r28.l
	move r28.l, a5
spinlock_lock:
	move (a5), r28.l
	cmpl 0, r28.l
	b.ne spinlock_lock
	nop

	js _wait_dbgstatus
	nop
	move r0.l, (a2+1)
	clrl r0.l
	move r0.l, (a2+2)
	move r0.l, (a2)
	move r0.l, (a5)

	move r22.l, imaskr
	nop
	nop
	rts

_wait_dma_:
	btst r12.s, R8.s
	b.cs _wait_dma_
	nop
	nop
	rts

_wait_all_dma_channels:
	cmp 0, r8.s
	b.ne _wait_all_dma_channels
	nop
	nop
	rts

_calculate:
	;tile_size
	move (A7+12), R0.l
	move R0.l, A4
	move (A4 + 1), R26.l

	andl    0x000F, r26.l, r0.l
	addl.ne 0x0010, r26.l
	lsrl    4, r26.l

	; iteration minimum = 3
	and     0xFFFC, r26.s, r0.s
	or.eq   0x0003, r26.s

	; address offset
	move 4, it.s

	; fill r16.q by 16th values 255
	clrd r16.d
	notd r16.d, r16.d
	trd  r16.d, r17.d

	; data prepairation for loop
	move (at)+it, r0.q
	sb16 r0.q, r16.q, r18.q
	move (at)+it, r0.q

	do r26.s, end_BitwiseNot
end_BitwiseNot:
	sb16 r0.q, r16.q, r18.q	   move r18.q, (a0)+	move (at)+it, r0.q
	nop
	nop
	rts

_wait_dbgstatus:
;------read DBGSTATUS------
	move 0x37220D00, r28.l
	lsrl 0x2, r28.l
	move r28.l, a0
	move (a0), r28.l
;-------------------------
	andl 0x1, r28.l
	cmpl 0x1, r28.l
	nop
	b.eq _wait_dbgstatus
	nop
	nop
	rts

_save_regs:
	;Stack pointer
	move A6.l, R30.l
	move R30.l, (A3)+		;A6			[0]
	;Frame pointer
	move A7.l, R30.l
	move R30.l, (A3)+		;A7			[1]
	move m0, R30.s
	move m1, R31.s
	move R30.l, (A3)+		;m0 m1			[2]
	move m2, R30.s
	move m3, R31.s
	move R30.l, (A3)+		;m2 m3			[3]
	move m4, R30.s
	move m5, R31.s
	move R30.l, (A3)+		;m4 m5			[4]
	move m6, R30.s
	move m7, R31.s
	move R30.l, (A3)+		;m6 m7			[5]
	move mt, R30.s
	move dt, R31.s
	move R30.l, (A3)+		;MT DT			[6]

	move sr, R30.s
	move ccr, R31.s
	move R30.l, (A3)+		;SR CCR			[7]
	move pdnr, R30.s
	move it, R31.s
	move R30.l, (A3)+		;PDNR IT		[8]
	move sfr.l, R30.l
	move R30.l, (A3)+		;SFR			[9]
	move A0.l, R30.l
	move R30.l, (A3)+		;A0			[10]
	move A1.l, R30.l
	move R30.l, (A3)+		;A1			[11]
	move A2.l, R30.l
	move R30.l, (A3)+		;A2			[12]
	move R31.l, R30.l
	move R30.l, (A3)+		;A3			[13]
	move A4.l, R30.l
	move R30.l, (A3)+		;A4			[14]
	move A5.l, R30.l
	move R30.l, (A3)+		;A5			[15]
	move i0, R30.s
	move i1, R31.s
	move R30.l, (A3)+		;i0 i1			[16]
	move i2, R30.s
	move i3, R31.s
	move R30.l, (A3)+		;i2 i3			[17]
	move i4, R30.s
	move i5, R31.s
	move R30.l, (A3)+		;i4 i5			[18]
	move i6, R30.s
	move i7, R31.s
	move R30.l, (A3)+		;i6 i7			[19]
	move AC0.l, R30.l
	move R30.l, (A3)+		;AC0			[20]
	move AC1.l, R30.l
	move R30.l, (A3)+		;AC1			[21]
	move AC2.l, R30.l
	move R30.l, (A3)+		;AC2			[22]
	move AC3.l, R30.l
	move R30.l, (A3)+		;AC3			[23]
	move AC4.l, R30.l
	move R30.l, (A3)+		;AC4			[24]
	move AC5.l, R30.l
	move R30.l, (A3)+		;AC5			[25]
	move AC6.l, R30.l
	move R30.l, (A3)+		;AC6			[26]
	move AC7.l, R30.l
	move R30.l, (A3)+		;AC7			[27]
	move AC8.l, R30.l
	move R30.l, (A3)+		;AC8			[28]
	move AC9.l, R30.l
	move R30.l, (A3)+		;AC9			[29]
	move AC10.l, R30.l
	move R30.l, (A3)+		;AC10			[30]
	move AC11.l, R30.l
	move R30.l, (A3)+		;AC11			[31]
	move AC12.l, R30.l
	move R30.l, (A3)+		;AC12			[32]
	move AC13.l, R30.l
	move R30.l, (A3)+		;AC13			[33]
	move AC14.l, R30.l
	move R30.l, (A3)+		;AC14			[34]
	move AC15.l, R30.l
	move R30.l, (A3)+		;AC15			[35]
	move R0.q, (A3)+		;R0.q			[36-39]
	move R2.q, (A3)+		;R2.q			[40-43]
	move R4.q, (A3)+		;R4.q			[44-47]
	move R6.q, (A3)+		;R6.q			[48-51]
	move R8.q, (A3)+		;R8.q			[52-55]
	move R10.q, (A3)+		;R10.q			[56-59]
	move R12.q, (A3)+		;R12.q			[60-63]
	move R14.q, (A3)+		;R14.q			[64-67]
	move R16.q, (A3)+		;R16.q			[68-71]
	move R18.q, (A3)+		;R18.q			[72-75]
	move R20.q, (A3)+		;R20.q			[76-79]
	move R22.q, (A3)+		;R22.q			[80-83]
	move R24.q, (A3)+		;R24.q			[84-87]
	move R26.q, (A3)+		;R26.q			[88-91]
	move R28.q, (A3)+		;R28.q			[92-95]
	move at.l, R30.l
	move R30.l, (A3)+		;AT			[96]

	clrl R4.l
	clrl R30.l
	move sp, R30.s
	and 0xF, R30.s
	move R30.l, (A3)+
_start_save_ss:
	cmp 0x0, R30
	b.eq _end_save_ss
	nop
	dec R30.s, R30.s
	move ss, R4.s
	move R4.l, (A3)+
	b _start_save_ss
	nop
_end_save_ss:
	move la, R4.s
	move lc, R5.s
	move R4.l, (A3)+
	move sp, R30.s
	lsr 0x8, R30.s, R30.s
	and 0x7, R30
	move R30.l, (A3)+
_start_save_cs:
	cmp 0x0, R30
	b.eq _end_save_cs
	nop
	dec R30, R30
	move csh, R4.s
	move csl, R5.s
	move R4.l, (A3)+
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

	move A3.l, R4.l
	move R4.l, R30.l
	addl 0x61, R30.l
	move R30.l, A3.l

	move (A3), R30.l
	andl 0xF, R30.l
	cmpl 0x0, R30.l
	b.eq _end_restore_ss
	nop
	move A3.l, R0.l
	addl R30.l, R0.l
	move R0.l, A3.l
_start_restore_ss:
	move (A3)-, R2.l
	move R2.s, ss
	dec R30, R30
	cmp 0x0, R30
	b.ne _start_restore_ss
	nop
_end_restore_ss:
	addl 0x1, R0.l
	move R0.l, A3.l
	move (A3)+, R30.l
	move R30.s, la
	move R31.s, lc
	move (A3), R30.l
	andl 0x7, R30.l
	cmpl 0x0, R30.l
	b.eq _end_restore_cs
	nop
	addl R30.l, R0.l
	move R0.l, A3.l
_start_restore_cs:
	move (A3)-, R2.l
	move R2.s, csh
	move R3.s, csl
	dec R30, R30
	cmp 0x0, R30
	b.ne _start_restore_cs
	nop
_end_restore_cs:
	move R4.l, A3.l

	;Stack pointer
	move (A3)+, R30.l		 ;A6			[0]
	move R30.l, A6.l
	;Frame pointer
	move (A3)+, R30.l		 ;A7			[1]
	move R30.l, A7.l
	move (A3)+, R30.l		;m0 m1			[2]
	move R30.s, m0
	move R31.s, m1
	move (A3)+, R30.l		;m2 m3			[3]
	move R30.s, m2
	move R31.s, m3
	move (A3)+, R30.l		;m4 m5			[4]
	move R30.s, m4
	move R31.s, m5
	move (A3)+, R30.l		;m6 m7			[5]
	move R30.s, m6
	move R31.s, m7
	move (A3)+, R30.l		;MT DT			[6]
	move R30.s, mt
	move R31.s, dt

	move (A3)+, R30.l		;SR CCR			[7]
	move R30.s, sr
	move R31.s, ccr
	move (A3)+, R30.l		;PDNR IT		[8]
	move R30.s, pdnr
	move R31.s, it
	move (A3)+, R30.l		 ;SFR			[9]
	move R30.l, sfr.l
	move (A3)+, R30.l		 ;A0			[10]
	move R30.l, A0.l
	move (A3)+, R30.l		 ;A1			[11]
	move R30.l, A1.l
	move (A3)+, R30.l		 ;A2			[12]
	move R30.l, A2.l
	move (A3)+, R30.l		 ;A3			[13]
	move R30.l, R31.l
	move (A3)+, R30.l		 ;A4			[14]
	move R30.l, A4.l
	move (A3)+, R30.l		 ;A5			[15]
	move R30.l, A5.l
	move (A3)+, R30.l		 ;i0 i1			[16]
	move R30.s, i0
	move R31.s, i1
	move (A3)+, R30.l		 ;i2 i3			[17]
	move R30.s, i2
	move R31.s, i3
	move (A3)+, R30.l		 ;i4 i5			[18]
	move R30.s, i4
	move R31.s, i5
	move (A3)+, R30.l		 ;i6 i7			[19]
	move R30.s, i6
	move R31.s, i7
	move (A3)+, R30.l		;AC0			[20]
	move R30.l, AC0.l
	move (A3)+, R30.l		;AC1			[21]
	move R30.l, AC1.l
	move (A3)+, R30.l		;AC2			[22]
	move R30.l, AC2.l
	move (A3)+, R30.l		;AC3			[23]
	move R30.l, AC3.l
	move (A3)+, R30.l		;AC4			[24]
	move R30.l, AC4.l
	move (A3)+, R30.l		;AC5			[25]
	move R30.l, AC5.l
	move (A3)+, R30.l		;AC6			[26]
	move R30.l, AC6.l
	move (A3)+, R30.l		;AC7			[27]
	move R30.l, AC7.l
	move (A3)+, R30.l		;AC8			[28]
	move R30.l, AC8.l
	move (A3)+, R30.l		;AC9			[29]
	move R30.l, AC9.l
	move (A3)+, R30.l		;AC10			[30]
	move R30.l, AC10.l
	move (A3)+, R30.l		;AC11			[31]
	move R30.l, AC11.l
	move (A3)+, R30.l		;AC12			[32]
	move R30.l, AC12.l
	move (A3)+, R30.l		;AC13			[33]
	move R30.l, AC13.l
	move (A3)+, R30.l		;AC14			[34]
	move R30.l, AC14.l
	move (A3)+, R30.l		;AC15			[35]
	move R30.l, AC15.l
	move (A3)+, R0.q		;R0.q			[36-39]
	move (A3)+, R2.q		;R2.q			[40-43]
	move (A3)+, R4.q		;R4.q			[44-47]
	move (A3)+, R6.q		;R6.q			[48-51]
	move (A3)+, R10.q		;R8.q			[52-55]
	move (A3)+, R10.q		;R10.q			[56-59]
	move (A3)+, R12.q		;R12.q			[60-63]
	move (A3)+, R14.q		;R14.q			[64-67]
	move (A3)+, R16.q		;R16.q			[68-71]
	move (A3)+, R18.q		;R18.q			[72-75]
	move (A3)+, R20.q		;R20.q			[76-79]
	move (A3)+, R22.q		;R22.q			[80-83]
	move (A3)+, R24.q		;R24.q			[84-87]
	move (A3)+, R26.q		;R26.q			[88-91]
	move (A3)+, R28.q		;R28.q			[92-95]
	move (A3)+, R30.l		;AT			[96]
	move R30.l, at.l
	b _end_restore
	nop
