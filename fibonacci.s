;
; Copyright 2019 RnD Center "ELVEES", JSC
;
;
; Calculate Fibonacci on ELcore-30M
;
.global test_fib
test_fib:

    asrl    2, r2.l, r2.l
    clr r3.s
    move    r2.l, a0.l
    move    (a0.l), r0.l

    asrl    2, r4.l, r4.l
    clr r5.s
    move    r4.l, a1.l
    move    (a1.l), r12.l

    move    (A7), r12.l
    asrl    2, r12.l, r12.l
    clr r13.s
    dofor   end_loop
    move    r12.l, a3.l
    move    16383, r18.l
    move    2147483647, r6.l
    move    0, r8.l
    move    1, r10.l
    move    1, r16.l

    do      r18.s, end_find_magic_number
    move    r10.l, r8.l
    move    r16.l, r10.l
    addl    r8.l, r10.l, r16.l
    move    r16.l, (a3.l)+

end_find_magic_number:
    andl    r6.l, r16.l

    move    r16.l, (a1.l)
    incl    r0.l, r0.l
end_loop:
    move    r0.l, (a0.l)

    nop
    nop
    stop
    nop
