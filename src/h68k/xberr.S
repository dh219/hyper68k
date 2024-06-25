;//--------------------------------------------------------------------
;// Hyper68k : berr.S
;// Access error handler
;// 
;// (c)2023 Anders Granlund
;//--------------------------------------------------------------------
;//
;// !! Assumes 256 byte pagetable.
;//    Could easily be changed, look at "mmuf_ccgeta2"
;//    
;// !! Assumes Long-format page descriptors
;//
;// Invalid Long-format descriptors are assumed to contain special
;// information in both of the unused fields.
;//
;// bbbbbb00 aaaaaa
;//
;// a = required by berr handler
;//     pointer to table of 16 stage1 functions based on SSW:<rm|rw|size>
;//     (yes, the last 8 pointers should point to the same RMW handler)
;// b = stage1 specific data
;//
;// We have are four different stage1 types at the moment:
;//     1. Direct Passthrough
;//     2. Direct Passthrough (safe)
;//     3. Callbacks (single entry for entire page)
;//     4. Callbacks (individual entries for each address in page)
;//
;//--------------------------------------------------------------------
#define __asm_inc__
#include "h68k.h"
#undef __asm_inc__

;//#define BERR_BALIGN  BERR_TALIGN
;//#define BERR_TALIGN  BERR_TALIGN
#define BERR_BALIGN  .even
#define BERR_TALIGN  .even

    .bss
    BERR_BALIGN

                .long
berr_sav_sp:    .long
berr_sav_vec:   .long
berr_sav_rmt:   .long

	.text
    BERR_TALIGN


;//----------------------------------------------------------------------------------------------
;// constants
;//----------------------------------------------------------------------------------------------
#define BERRHANDLER_ASSERTS     1
#define BERR_SAVEREGS           d0-d1/a0-a2
#define BERR_SAVESIZE           20

;//----------------------------------------------------------------------------------------------
;// lookup table for data pointer offset + in/out data buffer offset in stackframe
;//----------------------------------------------------------------------------------------------
    BERR_TALIGN
berrOffsetTable:
    .long 0 + BERR_SAVESIZE+24              ;// write 4 bytes, frame+44 (input buffer)        0
    .long 3 + BERR_SAVESIZE+24              ;// write 1 byte,  frame+44 (input buffer)        4
    .long 2 + BERR_SAVESIZE+24              ;// write 2 bytes, frame+44 (input buffer)        8
    .long 1 + BERR_SAVESIZE+24              ;// write 3 bytes, frame+44 (input buffer)        12
    .long 0 + BERR_SAVESIZE+44              ;// read 4 bytes, frame+24 (output buffer)        16
    .long 3 + BERR_SAVESIZE+44              ;// read 1 byte,  frame+24 (output buffer)        20
    .long 2 + BERR_SAVESIZE+44              ;// read 2 bytes, frame+24 (output buffer)        24
    .long 1 + BERR_SAVESIZE+44              ;// read 3 bytes, frame+24 (output buffer)        28
    .long 0 - 4                             ;// rmw 4 bytes, (temp buffer on stack)           32
    .long 3 - 4                             ;// rmw 1 byte,  (temp buffer on stack)           36
    .long 2 - 4                             ;// rmw 2 bytes, (temp buffer on stack)           40
    .long 1 - 4                             ;// rmw 3 bytes, (temp buffer on stack)           44
    .long 0 - 4                             ;// rmw 4 bytes, (temp buffer on stack)           48
    .long 3 - 4                             ;// rmw 1 byte,  (temp buffer on stack)           52
    .long 2 - 4                             ;// rmw 2 bytes, (temp buffer on stack)           56
    .long 1 - 4                             ;// rmw 3 bytes, (temp buffer on stack)           60


;//----------------------------------------------------------------------------------------------
;//
;// Bus Error exception handler
;//
;//----------------------------------------------------------------------------------------------

    BERR_TALIGN
_vec68000_BusError:
    move.w  #0x2700,sr                      ;// disable interrupts
    movem.l BERR_SAVEREGS,-(sp)             ;// save regs

#if BERRHANDLER_ASSERTS    
    cmp.l   #0x00FFFFFF,BERR_SAVESIZE+2(sp)
    bls.b   0f
    cmp.l   #0x02000000,BERR_SAVESIZE+2(sp)
    bge.b   0f
unhandled_berr:
1:  bra.b   1b
    H68K_FATAL(#1);
0:
#endif

    bclr.b  #0,BERR_SAVESIZE+10(sp)         ;// test and clear data fault / rerun flag

#if BERRHANDLER_ASSERTS    
    beq.w   berrNotDataFault                ;// if not data fault then something fatal has happened
#endif

    ;// fetch fault address and atc entry
    move.l  BERR_SAVESIZE+16(sp),d1         ;// d1 = fault address
    and.l   #0x00FFFFFF,d1                  ;// mask address to 24bits
    ptestr  #1,(d1),#7,a2                   ;// a2 = ATC entry
    tst.b   3(a2)                           ;// lower 8 bits 0 if handler installed for this page
    bne.b   berrTriggerClientException

    ;// fetch handler and data offsets from table based on SSW
    bfextu  BERR_SAVESIZE+10(sp){8:4},d0    ;// d0 = handler offset (ssw:rm|rw|size)
    move.l  (berrOffsetTable,d0.w*4),a0     ;// get stack offset for data in/out buffer
    add.l   sp,a0                           ;// a0 = pointer to data buffer

    ;// call read/write handler
    move.l  ([4,a2],d0.w*4),a1              ;// jump to handler (d1=fault addr, a0=databuf, a2=atc entry)
    jmp     (a1)

berrTriggerClientException:
    movem.l (sp)+,BERR_SAVEREGS             ;// restore regs
    jmp     ([_vec_table+0x8])              ;// trigger group0 exception on client


berrNotDataFault:
    ;// todo:
    ;//  *if* we wanted to support the client executing code from AccessMapped
    ;//  memory then we would have to support stage B/C faults here.
    H68K_FATAL(#0xdeadbe01)
    bra.b   berrTriggerClientException




;//----------------------------------------------------------------------------------------------
;// direct read/write/modify without application callbacks
;//     a0 = data buffer
;//     d1 = fault address
;//     a2 = atc entry
;//----------------------------------------------------------------------------------------------
.macro mmuf_done
    movem.l (sp)+,BERR_SAVEREGS             ;// restore regs
    rte                                     ;// all done!
.endm

.macro mmuf_fail
    movem.l (sp)+,BERR_SAVEREGS             ;// restore regs
    jmp     ([_vec_table+0x8])              ;// trigger group0 exception on client
.endm


    BERR_TALIGN
_h68k_mmuf_Fatal:                           ;// trigger fatal error on access
    H68K_FATAL(#0xdeadbeff);
    mmuf_fail

    BERR_TALIGN
_h68k_mmuf_Berr:                            ;// trigger client bus error on access
    mmuf_fail

    BERR_TALIGN
_h68k_mmuf_Ignore:                          ;// ignore access
    mmuf_done

    BERR_TALIGN
_h68k_mmuf_rb:                              ;// read byte
    move.b  (d1),(a0)
    mmuf_done
    BERR_TALIGN
_h68k_mmuf_rw:                              ;// read word
    move.w  (d1),(a0)
    mmuf_done
    BERR_TALIGN
_h68k_mmuf_rl:                              ;// read long
    move.l  (d1),(a0)
    mmuf_done
    BERR_TALIGN
_h68k_mmuf_r3:                              ;// read 3 bytes
    move.l  d1,a1
    move.b  (a1)+,(a0)+
    move.b  (a1)+,(a0)+
    move.b  (a1)+,(a0)+
    mmuf_done
    BERR_TALIGN
_h68k_mmuf_wb:                              ;// write byte
    move.b  (a0),(d1)
    mmuf_done
    BERR_TALIGN
_h68k_mmuf_ww:                              ;// write word
    move.w  (a0),(d1)
    mmuf_done
    BERR_TALIGN
_h68k_mmuf_wl:                              ;// write long
    move.l  (a0),(d1)
    mmuf_done
    BERR_TALIGN
_h68k_mmuf_w3:                              ;// write 3 bytes
    move.l  d1,a1
    move.b  (a0)+,(a1)+
    move.b  (a0)+,(a1)+
    move.b  (a0)+,(a1)+
    mmuf_done
    BERR_TALIGN
_h68k_mmuf_rm:                              ;// read-modify-write (TAS)
    move.w  BERR_SAVESIZE+0(sp),d0
    move    d0,ccr
    tas     (d1)
    move    ccr,d1
    and.w   #SR_MASK_NC,d0
    or.w    d1,d0
    move.w  d0,BERR_SAVESIZE+0(sp)
    mmuf_done


;//----------------------------------------------------------------------------------------------
;// direct read/write/modify without application callbacks (bus error safe)
;//     a1 = data buffer
;//     d1 = fault address
;//     a2 = atc entry
;//----------------------------------------------------------------------------------------------

.macro safe_nop
;//    nop
.endm

.macro safe_begin
    move.l  d1,a1                           ;// a1 = fault address
;//    movec   vbr,a2                          ;// get vbr
;//    move.l  8(a2),d1                        ;// save old berr vector
    move.l  _host_vbr+8,d1
    move.l  #9f,8(a2)                       ;// set temp berr vector
    move.l  sp,d0                           ;// save stack pointer
.endm

.macro safe_end
    move.l  d1,8(a2)                        ;// restore old berr vector
    mmuf_done
9:  safe_nop
    move.l  d0,sp                           ;// restore stack pointer
;//    move.l  d1,8(a2)                        ;// restore old berr vector
    move.l  d1,_host_vbr+8
    mmuf_fail
.endm

    BERR_TALIGN
_h68k_mmuf_rbs:                             ;// read byte
    safe_begin
    safe_nop
    move.b  (a1),(a0)
    safe_nop
    safe_end
    BERR_TALIGN
_h68k_mmuf_rws:                             ;// read word
    safe_begin
    safe_nop
    move.w  (a1),(a0)
    safe_nop
    safe_end
    BERR_TALIGN
_h68k_mmuf_rls:                             ;// read long
    safe_begin
    safe_nop
    move.l  (a1),(a0)
    safe_nop
    safe_end
    BERR_TALIGN
_h68k_mmuf_r3s:                             ;// read 3 bytes
    safe_begin
    safe_nop
    move.b  (a1)+,(a0)+
    safe_nop
    move.b  (a1)+,(a0)+
    safe_nop
    move.b  (a1)+,(a0)+
    safe_nop
    safe_end
    BERR_TALIGN
_h68k_mmuf_wbs:                             ;// write byte
    safe_begin
    safe_nop
    move.b  (a0),(a1)
    safe_nop
    safe_end
    BERR_TALIGN
_h68k_mmuf_wws:                             ;// write word
    safe_begin
    safe_nop
    move.w  (a0),(a1)
    safe_nop
    safe_end
    BERR_TALIGN
_h68k_mmuf_wls:                             ;// write long
    safe_begin
    safe_nop
    move.l  (a0),(a1)
    safe_nop
    safe_end
    BERR_TALIGN
_h68k_mmuf_w3s:                             ;// write 3 bytes
    safe_begin
    safe_nop
    move.b  (a0)+,(a1)+
    safe_nop
    move.b  (a0)+,(a1)+
    safe_nop
    move.b  (a0)+,(a1)+
    safe_nop
    safe_end
    BERR_TALIGN
_h68k_mmuf_rms:                             ;// read-modify-write (TAS)
    safe_begin
    move    BERR_SAVESIZE+0(sp),ccr
    safe_nop
    tas     (a1)
    safe_nop
    move    ccr,d0
    and.w   #SR_MASK_NC,BERR_SAVESIZE+0(sp)
    or.w    d0,BERR_SAVESIZE+0(sp)
    safe_end



;//----------------------------------------------------------------------------------------------
;// read/write/modify with application callbacks
;//     a0 = data buffer
;//     d1 = fault address
;//     a2 = atc entry
;//----------------------------------------------------------------------------------------------

_h68kIO_TriggerBerr:
    ;// todo
0:  bra.b 0b
    H68K_FATAL(#0xdeadbe10);

_h68kIO_TriggerFatal:
    ;// todo
0:  bra.b 0b
    H68K_FATAL(#0xdeadbe11);


.macro mmuf_cbegin
    move.l  a0,-(sp)                        ;// arg2 = data buffer
    move.l  d1,-(sp)                        ;// arg1 = fault address
.endm

.macro mmuf_cdone
    addq.l  #8,sp                           ;// get rid of pushed arguments
    mmuf_done
.endm

.macro mmuf_cgeta2 idx
    move.l  (a2),a2                         ;// atc[0] points to a table with only 1 entry
.endm

.macro mmuf_ccgeta2 idx
    sub.l   d0,d0
    move.b  d1,d0                           ;// fault address low byte is the entry index (because MMU pagesize is 256)
    lsl.l   #5,d0                           ;// 32bytes/entry
    move.l  (a2),a2                         ;// get table pointer from atc[0]
    add.l   d0,a2                           ;// offset into table
.endm

;// read/write x1
.macro mmuf_cfunc_rw1 name begin done geta2 idx
    BERR_TALIGN
_\name:
    \begin
    \geta2 \idx                             ;// a2 = callback table
    jsr     ([\idx,a2])                     ;// call function at <idx>
    \done
.endm

;// read/write x3
.macro mmuf_cfunc_rw3 name begin done geta2 idx
    BERR_TALIGN
_\name:
    \begin
    \geta2 \idx
    move.l  \idx(a2),a2                     ;// a2 = read function ptr (should be byte)
    jsr     (a2)                            ;// read byte
    addq.l  #1,(sp)
    addq.l  #1,4(sp)
    jsr     (a2)                            ;// read byte
    addq.l  #1,(sp)
    addq.l  #1,4(sp)
    jsr     (a2)                            ;// read byte
    \done
.endm

;// read-modify-write
.macro mmuf_cfunc_rm name begin done geta2 idx
    BERR_TALIGN
_\name:
    move.l  #berr_sav_rmt,-(sp)             ;// arg2 = data buffer
    move.l  d1,-(sp)                        ;// arg1 = fault address
    \geta2  \idx                            ;// a2 is callback table
    jsr     ([0,a2])                        ;// read byte
    move.w  BERR_SAVESIZE+8(sp),d0
    move    d0,ccr
    tst.b   berr_sav_rmt
    move    ccr,d1
    and.w   #SR_MASK_NC,d0
    or.w    d1,d0
    move.w  d0,BERR_SAVESIZE+8(sp)
    bset.b  #7,berr_sav_rmt                 ;// set bit 7 of destination
    jsr     ([12,a2])                       ;// write byte
    mmuf_cdone
.endm


    BERR_TALIGN
_h68k_IoIgnoreByte:
_h68k_IoIgnoreWord:
_h68k_IoIgnoreLong:
    rts

    BERR_TALIGN
_h68k_IoFatalByte:
_h68k_IoFatalWord:
_h68k_IoFatalLong:
    H68K_FATAL(#0xdead7000)
    rts

    BERR_TALIGN
_h68k_IoReadByte00:
    clr.b   ([8,sp])
    rts
    BERR_TALIGN
_h68k_IoReadWord00:
    clr.w   ([8,sp])
    rts
    BERR_TALIGN
_h68k_IoReadLong00:
    clr.l   ([8,sp])
    rts

    BERR_TALIGN
h68k_IoRWFail:
    bra.b   h68k_IoRWFail


    BERR_TALIGN
_h68k_IoReadByteFF:
    move.b  #0xFF,([8,sp])
    rts
    BERR_TALIGN
_h68k_IoReadWordFF:
    move.w  #0xFFFF,([8,sp])
    rts
    BERR_TALIGN
_h68k_IoReadLongFF:
    move.l  #0xFFFFFFFF,([8,sp])
    rts

    BERR_TALIGN
_h68k_IoReadBytePT:
    move.b  ([4,sp]),([8,sp])
    rts
    BERR_TALIGN
_h68k_IoReadWordPT:
    move.w  ([4,sp]),([8,sp])
    rts
    BERR_TALIGN
_h68k_IoReadLongPT:
    move.l  ([4,sp]),([8,sp])
    rts
    BERR_TALIGN
_h68k_IoWriteBytePT:
    move.b  ([8,sp]),([4,sp])
    rts
    BERR_TALIGN
_h68k_IoWriteWordPT:
    move.w  ([8,sp]),([4,sp])
    rts
    BERR_TALIGN
_h68k_IoWriteLongPT:
    move.l  ([8,sp]),([4,sp])
    rts



    BERR_TALIGN
_h68k_IoReadWordBB:
    addq.l  #4,sp                           ;// get rid of caller
    jsr     ([0,a2])                        ;// read byte
    addq.l  #1,(sp)
    addq.l  #1,4(sp)
    jsr     ([0,a2])                        ;// read byte
    mmuf_cdone

    BERR_TALIGN
_h68k_IoReadLongWW:
    addq.l  #4,sp                           ;// get rid of caller
    jsr     ([4,a2])                        ;// read word
    addq.l  #2,(sp)
    addq.l  #2,4(sp)
    jsr     ([4,a2])                        ;// read word
    mmuf_cdone

    BERR_TALIGN
_h68k_IoReadLongBBBB:
    addq.l  #4,sp                           ;// get rid of caller
    jsr     ([0,a2])                        ;// read byte
    addq.l  #1,(sp)
    addq.l  #1,4(sp)
    jsr     ([0,a2])                        ;// read byte
    addq.l  #1,(sp)
    addq.l  #1,4(sp)
    jsr     ([0,a2])                        ;// read byte
    addq.l  #1,(sp)
    addq.l  #1,4(sp)
    jsr     ([0,a2])                        ;// read byte
    mmuf_cdone

    BERR_TALIGN
_h68k_IoReadLongWBB:
    addq.l  #4,sp                           ;// get rid of caller
    jsr     ([4,a2])                        ;// read word
    addq.l  #2,(sp)
    addq.l  #2,4(sp)
    jsr     ([0,a2])                        ;// read byte
    addq.l  #1,(sp)
    addq.l  #1,4(sp)
    jsr     ([0,a2])                        ;// read byte
    mmuf_cdone

    BERR_TALIGN
_h68k_IoReadLongBBW:
    addq.l  #4,sp                           ;// get rid of caller
    jsr     ([0,a2])                        ;// read byte
    addq.l  #1,(sp)
    addq.l  #1,4(sp)
    jsr     ([0,a2])                        ;// read byte
    addq.l  #1,(sp)
    addq.l  #1,4(sp)
    jsr     ([4,a2])                        ;// read word
    mmuf_cdone

    BERR_TALIGN
_h68k_IoWriteWordBB:
    addq.l  #4,sp                           ;// get rid of caller
    jsr     ([12,a2])                       ;// write byte
    addq.l  #1,(sp)
    addq.l  #1,4(sp)
    jsr     ([12,a2])                       ;// write byte
    mmuf_cdone

    BERR_TALIGN
_h68k_IoWriteLongWW:
    addq.l  #4,sp                           ;// get rid of caller
    jsr     ([16,a2])                       ;// write word
    addq.l  #2,(sp)
    addq.l  #2,4(sp)
    jsr     ([16,a2])                       ;// write word
    mmuf_cdone

    BERR_TALIGN
_h68k_IoWriteLongBBBB:
    addq.l  #4,sp                           ;// get rid of caller
    jsr     ([12,a2])                       ;// write byte
    addq.l  #1,(sp)
    addq.l  #1,4(sp)
    jsr     ([12,a2])                       ;// write byte
    addq.l  #1,(sp)
    addq.l  #1,4(sp)
    jsr     ([12,a2])                       ;// write byte
    addq.l  #1,(sp)
    addq.l  #1,4(sp)
    jsr     ([12,a2])                       ;// write byte
    mmuf_cdone

    BERR_TALIGN
_h68k_IoWriteLongWBB:
    addq.l  #4,sp                           ;// get rid of caller
    jsr     ([16,a2])                       ;// write word
    addq.l  #2,(sp)
    addq.l  #2,4(sp)
    jsr     ([12,a2])                       ;// write byte
    addq.l  #1,(sp)
    addq.l  #1,4(sp)
    jsr     ([12,a2])                       ;// write byte
    mmuf_cdone

    BERR_TALIGN
_h68k_IoWriteLongBBW:
    addq.l  #4,sp                           ;// get rid of caller
    jsr     ([12,a2])                       ;// write byte
    addq.l  #1,(sp)
    addq.l  #1,4(sp)
    jsr     ([12,a2])                       ;// write byte
    addq.l  #1,(sp)
    addq.l  #1,4(sp)
    jsr     ([16,a2])                       ;// write word
    mmuf_cdone

mmuf_cfunc_rw1 h68k_mmuf_rbc mmuf_cbegin mmuf_cdone mmuf_cgeta2 0
mmuf_cfunc_rw1 h68k_mmuf_rwc mmuf_cbegin mmuf_cdone mmuf_cgeta2 4
mmuf_cfunc_rw1 h68k_mmuf_rlc mmuf_cbegin mmuf_cdone mmuf_cgeta2 8
mmuf_cfunc_rw1 h68k_mmuf_wbc mmuf_cbegin mmuf_cdone mmuf_cgeta2 12
mmuf_cfunc_rw1 h68k_mmuf_wwc mmuf_cbegin mmuf_cdone mmuf_cgeta2 16
mmuf_cfunc_rw1 h68k_mmuf_wlc mmuf_cbegin mmuf_cdone mmuf_cgeta2 20
mmuf_cfunc_rw3 h68k_mmuf_r3c mmuf_cbegin mmuf_cdone mmuf_cgeta2 0
mmuf_cfunc_rw3 h68k_mmuf_w3c mmuf_cbegin mmuf_cdone mmuf_cgeta2 12
mmuf_cfunc_rm  h68k_mmuf_rmc mmuf_cbegin mmuf_cdone mmuf_cgeta2 0

mmuf_cfunc_rw1 h68k_mmuf_rbcc mmuf_cbegin mmuf_cdone mmuf_ccgeta2 0
mmuf_cfunc_rw1 h68k_mmuf_rwcc mmuf_cbegin mmuf_cdone mmuf_ccgeta2 4
mmuf_cfunc_rw1 h68k_mmuf_rlcc mmuf_cbegin mmuf_cdone mmuf_ccgeta2 8
mmuf_cfunc_rw1 h68k_mmuf_wbcc mmuf_cbegin mmuf_cdone mmuf_ccgeta2 12
mmuf_cfunc_rw1 h68k_mmuf_wwcc mmuf_cbegin mmuf_cdone mmuf_ccgeta2 16
mmuf_cfunc_rw1 h68k_mmuf_wlcc mmuf_cbegin mmuf_cdone mmuf_ccgeta2 20
mmuf_cfunc_rw3 h68k_mmuf_r3cc mmuf_cbegin mmuf_cdone mmuf_ccgeta2 0
mmuf_cfunc_rw3 h68k_mmuf_w3cc mmuf_cbegin mmuf_cdone mmuf_ccgeta2 12
mmuf_cfunc_rm  h68k_mmuf_rmcc mmuf_cbegin mmuf_cdone mmuf_ccgeta2 0



;//----------------------------------------------------------------------------------------------
;// built in r/w handlers
;//----------------------------------------------------------------------------------------------
    BERR_TALIGN
_h68k_IoBerrByte:
_h68k_IoBerrWord:
_h68k_IoBerrLong:
    bra.b   _h68k_IoBerrByte
    lea     12(sp),sp                       ;// get rid of function call
    mmuf_fail                               ;// trigger client exception
