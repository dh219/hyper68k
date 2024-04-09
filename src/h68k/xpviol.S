;//--------------------------------------------------------------------
;// Hyper68k : xpviol.S
;// Privileged instruction emulator
;// 
;// (c)2023 Anders Granlund
;//--------------------------------------------------------------------
;// todo, in order of priority:
;//     (680xx) stop
;//     (680xx) trace emulation
;//     (68010) rte
;//     (68010) movec vbr
;//     (68010) movec sfc
;//     (68010) movec sfd
;//     (68010) moves
;//--------------------------------------------------------------------
#define __asm_inc__
#include "h68k.h"
#undef __asm_inc__

	.text
	.even

;//----------------------------------------------------------------------------------------------
;//
;// Privilege Violation exception
;//
;// d0 is saved to stack and must be restored by function that emulates the instruction
;//
;//----------------------------------------------------------------------------------------------
	.balign 4
_vec68000_PrivilegeViolation:
    move.w  #0x2700,sr                          ;// disable interrupts
    move.l  d0,-(sp)                            ;// save d0
    move.l  6(sp),d0                            ;// d0 = pc
    moves.w (d0),d0                             ;// d0 = instruction
    btst.b  #SR_BITB_S,_client_sr               ;// virtual usermode?
    beq.s   0f
	jmp     ([_pviols_table+0x20000, d0.w*4])   ;// jump to super handler
0:	jmp     ([_pviolu_table+0x20000, d0.w*4])   ;// jump to user handler


;//--------------------------------------------
;//
;// Helper macros
;//
;//--------------------------------------------

#define GET_CLIENT_SR(o,r) \
    move.w  o(sp),r                             ;/* get stacked sr                              */ \
    and.w   #SR_MASK_NS,r                       ;/* remove host super                           */ \
    or.w    _client_sr,r                        ;/* apply client super                          */

#define PVIOL_BEGIN(op) \
    .balign 4 ;\
_##op:

#if H68K_DEBUGTRACE
#define PVIOL_END(b) \
    move.l  (sp)+,d0                            ;/* restore d0 saved by _vecPrivilegeViolation  */ \
    addq.l  b,2(sp)                             ;/* step stacked PC by <b> bytes                */ \
    or.w    #0x8000,(sp)                        ;/* trace usermode                              */ \
    rte                                         ;/* and return from exception                   */
#else
#define PVIOL_END(b) \
    move.l  (sp)+,d0                            ;/* restore d0 saved by _vecPrivilegeViolation  */ \
    addq.l  b,2(sp)                             ;/* step stacked PC by <b> bytes                */ \
    rte                                         ;/* and return from exception                   */
#endif

;//--------------------------------------------
;//
;// Privilege violation -> Exception
;//
;//--------------------------------------------
.macro PVIOL_EXCEPTION_TRIGGER id,op
PVIOL_BEGIN(\op)
    move.l  (sp)+,d0                            ;/* restore d0 saved by _vecPrivilegeViolation  */ \
    and.w   #0xF000,6(sp)                       ;/* replace vector offset in the                */ \
    or.w    #\id,6(sp)                          ;/* existing stackframe and go                  */ \
    jmp     ([_vec_table+\id])                  ;/* to exception vection                        */
.endm

PVIOL_EXCEPTION_TRIGGER 0x10 pviol68000_IllegalInstruction
PVIOL_EXCEPTION_TRIGGER 0x20 pviol68000_PrivilegeViolation
PVIOL_EXCEPTION_TRIGGER 0x2C pviol68000_LineF


;//--------------------------------------------
;//
;// RTE (68000)
;//
;//--------------------------------------------
PVIOL_BEGIN(pviol68000_rte)
    move.l  d1,-(sp)                            ;// save regs
    move.l  a0,-(sp)
    movec   usp,a0                              ;// a0 = client a7 (ssp) = faked frame for the exception which we are rte'ing from
    moves.w (a0)+,d0                            ;// d0 = client frame SR
    moves.l (a0)+,d1                            ;// d1 = client frame PC
    btst.l  #SR_BITL_S,d0                       ;// switch to client usermode?
    bne     0f
    move.l  a0,_client_ssp                      ;// backup client ssp
    move.l  _client_usp,a0                      ;// a0 = client a7 (usp)
    bclr.b  #SR_BITB_S,_client_sr               ;// clear client super flag
0:  movec   a0,usp                              ;// update client a7
    and.w   #SR_MASK_NS,d0                      ;// mask valid 68000 bits (and clear super flag) on SR from client frame
#if 0
    bclr.l  #31,d1
#else
    bclr.l  #0,d1
#endif    
    beq.b   1f
    or.w    #SR_MASK_S,d0
1:  move.w  d0,12+0(sp)                         ;// SR (overwriting existing pviol frame)
    move.l  d1,12+2(sp)                         ;// PC (overwriting existing pviol frame)
    ;//move.w  #0,12+6(sp)                       ;// format (overwriting existing pviol frame)
    move.l  (sp)+,a0                            ;// restore regs
    move.l  (sp)+,d1
    move.l  (sp)+,d0                            ;// restore d0 pushed by pviol handler
    rte

;//--------------------------------------------
;//
;// RTE (68010)
;//
;//--------------------------------------------
PVIOL_BEGIN(pviol68010_rte)
    H68K_FATAL(0xdead0003)
    rte

;//--------------------------------------------
;//
;// RESET
;//
;//--------------------------------------------
;/*
;The reset line is asserted, causing all external devices connected
;to the 68000s RESET* output to be reset. The RESET instruction is
;privileged and has no effect on the operation of the 68000 itself.
;This instruction is used to perform a programmed reset of all
;peripherals connected to the 68000's RESET* pin.
;*/
PVIOL_BEGIN(pviol68000_reset)
    move.l  _h68k_OnResetDevices,d0
    beq.b   0f
    movem.l d1/a0-a1,-(sp)                      ;// save gcc scratch regs
    jsr     (d0)                                ;// call application defined callback
    movem.l (sp)+,d1/a0-a1                      ;// restore gcc scratch regs
0:  PVIOL_END(#2)


;//--------------------------------------------
;//
;// STOP
;//
;//--------------------------------------------
;/*
;The immediate operand is copied into the entire status register
;(i.e., both status byte and CCR are modified), and the program
;counter advanced to point to the next instruction to be executed.
;The processor then suspends all further processing and halts.
;That is, the privileged STOP instruction stops the 68000.
;The execution of instructions resumes when a trace, an interrupt,
;or a reset exception occurs. A trace exception will occur if the
;trace bit is set when the STOP instruction is encountered. If an
;interrupt request arrives whose priority is higher than the current
;processor priority, an interrupt exception occurs, otherwise the
;interrupt request has no effect. If the bit of the immediate data
;corresponding to the S-bit is clear (i.e., user mode selected),
;execution of the STOP instruction will cause a privilege violation.
;An external reset will always initiate reset exception processing.
;*/
PVIOL_BEGIN(pviol68000_stop)
    ;// todo
	PVIOL_END(#4)


;//--------------------------------------------
;//
;// MOVE USP
;//
;//--------------------------------------------
PVIOL_BEGIN(pviol68000_move_usp_a0)
    move.l _client_usp,a0
    PVIOL_END(#2)
PVIOL_BEGIN(pviol68000_move_usp_a1)
    move.l _client_usp,a1
    PVIOL_END(#2)
PVIOL_BEGIN(pviol68000_move_usp_a2)
    move.l _client_usp,a2
    PVIOL_END(#2)
PVIOL_BEGIN(pviol68000_move_usp_a3)
    move.l _client_usp,a3
    PVIOL_END(#2)
PVIOL_BEGIN(pviol68000_move_usp_a4)
    move.l _client_usp,a4
    PVIOL_END(#2)
PVIOL_BEGIN(pviol68000_move_usp_a5)
    move.l _client_usp,a5
    PVIOL_END(#2)
PVIOL_BEGIN(pviol68000_move_usp_a6)
    move.l _client_usp,a6
    PVIOL_END(#2)
PVIOL_BEGIN(pviol68000_move_usp_a7)
    move.l  _client_usp,d0                      ;// get client usp
    movec   d0,usp                              ;// store in client a7 (ssp)
    PVIOL_END(#2)

PVIOL_BEGIN(pviol68000_move_a0_usp)
    move.l a0,_client_usp
    PVIOL_END(#2)
PVIOL_BEGIN(pviol68000_move_a1_usp)
    move.l a1,_client_usp
    PVIOL_END(#2)
PVIOL_BEGIN(pviol68000_move_a2_usp)
    move.l a2,_client_usp
    PVIOL_END(#2)
PVIOL_BEGIN(pviol68000_move_a3_usp)
    move.l a3,_client_usp
    PVIOL_END(#2)
PVIOL_BEGIN(pviol68000_move_a4_usp)
    move.l a4,_client_usp
    PVIOL_END(#2)
PVIOL_BEGIN(pviol68000_move_a5_usp)
    move.l a5,_client_usp
    PVIOL_END(#2)
PVIOL_BEGIN(pviol68000_move_a6_usp)
    move.l a6,_client_usp
    PVIOL_END(#2)
PVIOL_BEGIN(pviol68000_move_a7_usp);
    movec   usp,d0                              ;// get client a7 (ssp)
    move.l  d0,_client_usp                      ;// store in client usp
    PVIOL_END(#2)

;//--------------------------------------------
;//
;// MOVE FROM SR
;//
;//--------------------------------------------
#define MOVEW_SR_DREG(op, r) \
PVIOL_BEGIN(op) \
    GET_CLIENT_SR(4,d0)                         ; \
    move.w  d0,r                                ;/* move to reg                 */ \
	PVIOL_END(#2)                               ;

#define MOVEW_SR_AREG(op, r) \
PVIOL_BEGIN(op) \
    GET_CLIENT_SR(4,d0)                         ; \
    moves.w  d0,r                               ; /* move to (user) (ax)        */ \
	PVIOL_END(#2)                               ;

#define MOVEW_SR_USP(op, t, r) \
PVIOL_BEGIN(op) \
    GET_CLIENT_SR(4,d0)                         ; \
    move.l  r,-(sp)                             ;/* save temp reg               */ \
    movec   usp,r                               ;/* get usp                     */ \
    moves.w d0,t                                ;/* move to (user) (usp)        */ \
    movec   r,usp                               ;/* update usp                  */ \
    move.l  (sp)+,r                             ;/* restore temp reg            */ \
    PVIOL_END(#2)                               ;

#define MOVEW_SR_EA(op, calc_ea, r) \
PVIOL_BEGIN(op) \
    calc_ea()                                   ;/* d0 = displacement           */ \
    move.l  a0,-(sp)                            ;/* save regs                   */ \
    move.l  r,a0                                ;/* a0 = ax                     */ \
    add.l   d0,a0                               ;/* apply displacement          */ \
    GET_CLIENT_SR(8,d0)                         ;/* d0 = client sr              */ \
    moves.w d0,(a0)                             ;/* write sr to (user) (ea)     */ \
    move.l  (sp)+,a0                            ;/* restore regs                */ \
    PVIOL_END(#4)                               ;

#define CALC_EA0_D0() \
    move.l  6(sp),d0                            ;/* d0 = ptr to fault pc            */ \
    moves.w 2(d0),d0                            ;/* d0 = displacement word          */ \
    ext.l   d0                                  ;/* d0 = displacement long          */

#define CALC_EA1_D0() \
    bsr _pviol_calc_ea1                         ;/* d0 = displacement long          */

;//--------------------------------------------
;// move sr,dx
;//--------------------------------------------
MOVEW_SR_DREG(pviol68000_move_sr_d0, 2(sp))
MOVEW_SR_DREG(pviol68000_move_sr_d1, d1)
MOVEW_SR_DREG(pviol68000_move_sr_d2, d2)
MOVEW_SR_DREG(pviol68000_move_sr_d3, d3)
MOVEW_SR_DREG(pviol68000_move_sr_d4, d4)
MOVEW_SR_DREG(pviol68000_move_sr_d5, d5)
MOVEW_SR_DREG(pviol68000_move_sr_d6, d6)
MOVEW_SR_DREG(pviol68000_move_sr_d7, d7)
;//--------------------------------------------
;// move sr,(ax)
;//--------------------------------------------
MOVEW_SR_AREG(pviol68000_move_sr_a0, (a0))
MOVEW_SR_AREG(pviol68000_move_sr_a1, (a1))
MOVEW_SR_AREG(pviol68000_move_sr_a2, (a2))
MOVEW_SR_AREG(pviol68000_move_sr_a3, (a3))
MOVEW_SR_AREG(pviol68000_move_sr_a4, (a4))
MOVEW_SR_AREG(pviol68000_move_sr_a5, (a5))
MOVEW_SR_AREG(pviol68000_move_sr_a6, (a6))
MOVEW_SR_USP(pviol68000_move_sr_a7, (a0), a0)
;//--------------------------------------------
;// move sr,(ax)+
;//--------------------------------------------
MOVEW_SR_AREG(pviol68000_move_sr_a0a, (a0)+)
MOVEW_SR_AREG(pviol68000_move_sr_a1a, (a1)+)
MOVEW_SR_AREG(pviol68000_move_sr_a2a, (a2)+)
MOVEW_SR_AREG(pviol68000_move_sr_a3a, (a3)+)
MOVEW_SR_AREG(pviol68000_move_sr_a4a, (a4)+)
MOVEW_SR_AREG(pviol68000_move_sr_a5a, (a5)+)
MOVEW_SR_AREG(pviol68000_move_sr_a6a, (a6)+)
MOVEW_SR_USP(pviol68000_move_sr_a7a, (a0)+,a0)
;//--------------------------------------------
;// move sr,-(ax)
;//--------------------------------------------
MOVEW_SR_AREG(pviol68000_move_sr_a0b, -(a0))
MOVEW_SR_AREG(pviol68000_move_sr_a1b, -(a1))
MOVEW_SR_AREG(pviol68000_move_sr_a2b, -(a2))
MOVEW_SR_AREG(pviol68000_move_sr_a3b, -(a3))
MOVEW_SR_AREG(pviol68000_move_sr_a4b, -(a4))
MOVEW_SR_AREG(pviol68000_move_sr_a5b, -(a5))
MOVEW_SR_AREG(pviol68000_move_sr_a6b, -(a6))
MOVEW_SR_USP(pviol68000_move_sr_a7b, -(a0),a0)
;//--------------------------------------------
;// move sr,x(ax)
;//--------------------------------------------
MOVEW_SR_EA(pviol68000_move_sr_a0c, CALC_EA0_D0, a0)
MOVEW_SR_EA(pviol68000_move_sr_a1c, CALC_EA0_D0, a1)
MOVEW_SR_EA(pviol68000_move_sr_a2c, CALC_EA0_D0, a2)
MOVEW_SR_EA(pviol68000_move_sr_a3c, CALC_EA0_D0, a3)
MOVEW_SR_EA(pviol68000_move_sr_a4c, CALC_EA0_D0, a4)
MOVEW_SR_EA(pviol68000_move_sr_a5c, CALC_EA0_D0, a5)
MOVEW_SR_EA(pviol68000_move_sr_a6c, CALC_EA0_D0, a6)
MOVEW_SR_EA(pviol68000_move_sr_a7c, CALC_EA0_D0, usp)
;//--------------------------------------------
;// move sr,x(ax,dx)
;//--------------------------------------------
MOVEW_SR_EA(pviol68000_move_sr_a0d, CALC_EA1_D0, a0)
MOVEW_SR_EA(pviol68000_move_sr_a1d, CALC_EA1_D0, a1)
MOVEW_SR_EA(pviol68000_move_sr_a2d, CALC_EA1_D0, a2)
MOVEW_SR_EA(pviol68000_move_sr_a3d, CALC_EA1_D0, a3)
MOVEW_SR_EA(pviol68000_move_sr_a4d, CALC_EA1_D0, a4)
MOVEW_SR_EA(pviol68000_move_sr_a5d, CALC_EA1_D0, a5)
MOVEW_SR_EA(pviol68000_move_sr_a6d, CALC_EA1_D0, a6)
MOVEW_SR_EA(pviol68000_move_sr_a7d, CALC_EA1_D0, usp)
;//--------------------------------------------
;// move sr,<address>.w
;//--------------------------------------------
PVIOL_BEGIN(pviol68000_move_sr_absW)
    move.l  6(sp),d0                            ;// d0 = client pc
    move.l  a0,-(sp)                            ;// save regs
    moves.w 2(d0),a0                            ;// get target address
    GET_CLIENT_SR(8,d0)
    moves.w d0,(a0.w)                           ;// write sr
    move.l  (sp)+,a0
    PVIOL_END(#4)
;//--------------------------------------------
;// move sr,<address>.l
;//--------------------------------------------
PVIOL_BEGIN(pviol68000_move_sr_absL)
    move.l  6(sp),d0
    move.l  a0,-(sp)
    moves.l 2(d0),a0
    GET_CLIENT_SR(8,d0)
    moves.w d0,(a0)
    move.l  (sp)+,a0
    PVIOL_END(#6)

;//--------------------------------------------
;//
;// MOVE to SR
;//
;//--------------------------------------------
#define MODIFY_SR_WITH_D0(o) \
    move.l  d1,-(sp)                            ;/* save regs                   */ \
    move.w  8(sp),d1                            ;/* get current host sr         */ \
    or.w    _client_sr,d1                       ;/*                             */ \
    o       d0,d1                               ;/* apply operation             */ \
    move.w  d1,d0                               ;/*                             */ \
    and.w   #SR_MASK_NS,d1                      ;/* update host sr              */ \
    move.w  d1,8(sp)                            ;/* (remove supervisor bit)     */ \
    and.w   #SR_MASK_S,d0                       ;/* mask supervisor bit only    */ \
    bne.s   0f                                  ;/* requested usermode?         */ \
	movec	usp,d1				                ;/* get client a7               */ \
	move.l	d1,_client_ssp		                ;/* backup as client ssp        */ \
	move.l	_client_usp,d1		                ;/* get backed up client usp    */ \
	movec	d1,usp				                ;/* -> to client a7             */ \
0:  move.w  d0,_client_sr                       ;/* update client sr            */ \
    move.l  (sp)+,d1                            ;/* restore regs                */

#define MOVEW_DREG_SR(op, r) \
PVIOL_BEGIN(op) \
	move.w  r,d0                                ;/* d0 = value              */ \
    MODIFY_SR_WITH_D0(move.w)                   ;/*                         */ \
    PVIOL_END(#2)

#define MOVEW_AREG_SR(op, r) \
PVIOL_BEGIN(op) \
	moves.w r,d0                                ;/* d0 = value              */ \
    MODIFY_SR_WITH_D0(move.w)                   ;/*                         */ \
    PVIOL_END(#2)

#define MOVEW_USP_SR(op, t, r) \
PVIOL_BEGIN(op) \
    move.l  r,-(sp)                             ;/* save regs               */ \
    movec   usp,r                               ;/* get client ssp          */ \
    moves.w t,d0                                ;/* d0 = value              */ \
    movec   r,usp                               ;/* update client ssp       */ \
    move.l  (sp)+,r                             ;/* restore regs            */ \
    MODIFY_SR_WITH_D0(move.w)                   ;/*                         */ \
    PVIOL_END(#2)

#define MOVEW_EA_SR(op, calc_ea, r) \
PVIOL_BEGIN(op) \
    calc_ea()                                   ;/* d0 = displacement       */ \
    move.l  a0,-(sp)                            ;/* save regs               */ \
    move.l  r,a0                                ;/* a0 = ax                 */ \
    add.l   d0,a0                               ;/* apply displacement      */ \
    moves.w (a0),d0                             ;/* d0 = value              */ \
    move.l  (sp)+,a0                            ;/* restore regs            */ \
    MODIFY_SR_WITH_D0(move.w)                   ;/*                         */ \
    PVIOL_END(#4)


;//--------------------------------------------
;// move dx,sr
;//--------------------------------------------
MOVEW_DREG_SR(pviol68000_move_d0_sr, 2(sp))
MOVEW_DREG_SR(pviol68000_move_d1_sr, d1)
MOVEW_DREG_SR(pviol68000_move_d2_sr, d2)
MOVEW_DREG_SR(pviol68000_move_d3_sr, d3)
MOVEW_DREG_SR(pviol68000_move_d4_sr, d4)
MOVEW_DREG_SR(pviol68000_move_d5_sr, d5)
MOVEW_DREG_SR(pviol68000_move_d6_sr, d6)
MOVEW_DREG_SR(pviol68000_move_d7_sr, d7)
;//--------------------------------------------
;// move (ax),sr
;//--------------------------------------------
MOVEW_AREG_SR(pviol68000_move_a0_sr, (a0))
MOVEW_AREG_SR(pviol68000_move_a1_sr, (a1))
MOVEW_AREG_SR(pviol68000_move_a2_sr, (a2))
MOVEW_AREG_SR(pviol68000_move_a3_sr, (a3))
MOVEW_AREG_SR(pviol68000_move_a4_sr, (a4))
MOVEW_AREG_SR(pviol68000_move_a5_sr, (a5))
MOVEW_AREG_SR(pviol68000_move_a6_sr, (a6))
MOVEW_USP_SR(pviol68000_move_a7_sr, (a0),a0)
;//--------------------------------------------
;// move (ax)+,sr
;//--------------------------------------------
MOVEW_AREG_SR(pviol68000_move_a0a_sr, (a0)+)
MOVEW_AREG_SR(pviol68000_move_a1a_sr, (a1)+)
MOVEW_AREG_SR(pviol68000_move_a2a_sr, (a2)+)
MOVEW_AREG_SR(pviol68000_move_a3a_sr, (a3)+)
MOVEW_AREG_SR(pviol68000_move_a4a_sr, (a4)+)
MOVEW_AREG_SR(pviol68000_move_a5a_sr, (a5)+)
MOVEW_AREG_SR(pviol68000_move_a6a_sr, (a6)+)
MOVEW_USP_SR(pviol68000_move_a7a_sr, (a0)+,a0)
;//--------------------------------------------
;// move -(ax),sr
;//--------------------------------------------
MOVEW_AREG_SR(pviol68000_move_a0b_sr, -(a0))
MOVEW_AREG_SR(pviol68000_move_a1b_sr, -(a1))
MOVEW_AREG_SR(pviol68000_move_a2b_sr, -(a2))
MOVEW_AREG_SR(pviol68000_move_a3b_sr, -(a3))
MOVEW_AREG_SR(pviol68000_move_a4b_sr, -(a4))
MOVEW_AREG_SR(pviol68000_move_a5b_sr, -(a5))
MOVEW_AREG_SR(pviol68000_move_a6b_sr, -(a6))
MOVEW_USP_SR(pviol68000_move_a7b_sr, -(a0),a0)
;//--------------------------------------------
;// move x(ax),sr
;//--------------------------------------------
MOVEW_EA_SR(pviol68000_move_a0c_sr, CALC_EA0_D0, a0)
MOVEW_EA_SR(pviol68000_move_a1c_sr, CALC_EA0_D0, a1)
MOVEW_EA_SR(pviol68000_move_a2c_sr, CALC_EA0_D0, a2)
MOVEW_EA_SR(pviol68000_move_a3c_sr, CALC_EA0_D0, a3)
MOVEW_EA_SR(pviol68000_move_a4c_sr, CALC_EA0_D0, a4)
MOVEW_EA_SR(pviol68000_move_a5c_sr, CALC_EA0_D0, a5)
MOVEW_EA_SR(pviol68000_move_a6c_sr, CALC_EA0_D0, a6)
MOVEW_EA_SR(pviol68000_move_a7c_sr, CALC_EA0_D0, usp)
;//--------------------------------------------
;// move x(ax,dx),sr
;//--------------------------------------------
MOVEW_EA_SR(pviol68000_move_a0d_sr, CALC_EA1_D0, a0)
MOVEW_EA_SR(pviol68000_move_a1d_sr, CALC_EA1_D0, a1)
MOVEW_EA_SR(pviol68000_move_a2d_sr, CALC_EA1_D0, a2)
MOVEW_EA_SR(pviol68000_move_a3d_sr, CALC_EA1_D0, a3)
MOVEW_EA_SR(pviol68000_move_a4d_sr, CALC_EA1_D0, a4)
MOVEW_EA_SR(pviol68000_move_a5d_sr, CALC_EA1_D0, a5)
MOVEW_EA_SR(pviol68000_move_a6d_sr, CALC_EA1_D0, a6)
MOVEW_EA_SR(pviol68000_move_a7d_sr, CALC_EA1_D0, usp)
;//--------------------------------------------
;// move <address>.w,sr
;//--------------------------------------------
PVIOL_BEGIN(pviol68000_move_absW_sr)
    move.l  6(sp),d0                            ;// d0 = pc
    moves.w (2,d0),d0                           ;// d0 = addr.w
    moves.w (d0.w),d0                           ;// d0 = requested sr
    MODIFY_SR_WITH_D0(move.w)
    PVIOL_END(#4)
;//--------------------------------------------
;// move <address>.L,sr
;//--------------------------------------------
PVIOL_BEGIN(pviol68000_move_absL_sr)
    move.l  6(sp),d0                            ;// d0 = pc
    moves.l (2,d0),d0                           ;// d0 = addr.l
    moves.w (d0),d0                             ;// d0 = requested sr
    MODIFY_SR_WITH_D0(move.w)
    PVIOL_END(#6)
;//--------------------------------------------
;// move #imm,sr
;//--------------------------------------------
PVIOL_BEGIN(pviol68000_move_imm_sr)
	move.l	6(sp),d0
	moves.w (2,d0),d0
    MODIFY_SR_WITH_D0(move.w)
    PVIOL_END(#4)
;//--------------------------------------------
;// and #imm,sr
;//--------------------------------------------
PVIOL_BEGIN(pviol68000_and_imm_sr)
	move.l	6(sp),d0
	moves.w	(2,d0),d0
    MODIFY_SR_WITH_D0(and.w)
    PVIOL_END(#4)
;//--------------------------------------------
;// eor #imm,sr
;//--------------------------------------------
PVIOL_BEGIN(pviol68000_eor_imm_sr)
	move.l	6(sp),d0
	moves.w	(2,d0),d0
    MODIFY_SR_WITH_D0(eor.w)
    PVIOL_END(#4)
;//--------------------------------------------
;// or #imm,sr
;//--------------------------------------------
PVIOL_BEGIN(pviol68000_or_imm_sr)
	move.l	6(sp),d0
	moves.w	(2,d0),d0
    MODIFY_SR_WITH_D0(or.w)
    PVIOL_END(#4)


;//--------------------------------------------
;// _pviol_calc_ea1
;// (assumes saved regs are: d0)
;// returns displacement of x(Ax,Dx) in d0
;//--------------------------------------------
    .balign 4
_pviol_calc_ea1:
    move.l  10(sp),d0                           ;// d0 = client pc
    moves.w 2(d0),d0                            ;// d0 = control word
    extb.l  d0                                  ;// sign extend 8 -> 32bit
    move.l  d0,-(sp)                            ;// push absolut displacement
    move.l  14(sp),d0                           ;// d0 = client pc
    moves.w 2(d0),d0                            ;// d0 = control word
    and.w   #0xF800,d0                          ;// mask register + size
    lsr.w   #7,d0                               ;// each handler is 16 bytes
    jmp     0f(pc, d0.w)
    .balign 16
0:  move.l  8(sp),d0                            ;// d0.w
    ext.l   d0
    add.l   (sp)+,d0
    rts
    .balign 16
    move.l  8(sp),d0                            ;// d0.l
    add.l   (sp)+,d0
    rts
    .balign 16
    move.l  d1,d0                               ;// d1.w
    ext.l   d0
    add.l   (sp)+,d0
    rts
    .balign 16
    move.l  d1,d0                               ;// d1.l
    add.l   (sp)+,d0
    rts
    .balign 16
    move.l  d2,d0                               ;// d2.w
    ext.l   d0
    add.l   (sp)+,d0
    rts
    .balign 16
    move.l  d2,d0                               ;// d2.l
    add.l   (sp)+,d0
    rts
    .balign 16
    move.l  d3,d0                               ;// d3.w
    ext.l   d0
    add.l   (sp)+,d0
    rts
    .balign 16
    move.l  d3,d0                               ;// d3.l
    add.l   (sp)+,d0
    rts
    .balign 16
    move.l  d4,d0                               ;// d4.w
    ext.l   d0
    add.l   (sp)+,d0
    rts
    .balign 16
    move.l  d4,d0                               ;// d4.l
    add.l   (sp)+,d0
    rts
    .balign 16
    move.l  d5,d0                               ;// d5.w
    ext.l   d0
    add.l   (sp)+,d0
    rts
    .balign 16
    move.l  d5,d0                               ;// d5.l
    add.l   (sp)+,d0
    rts
    .balign 16
    move.l  d6,d0                               ;// d6.w
    ext.l   d0
    add.l   (sp)+,d0
    rts
    .balign 16
    move.l  d6,d0                               ;// d6.l
    add.l   (sp)+,d0
    rts
    .balign 16
    move.l  d7,d0                               ;// d7.w
    ext.l   d0
    add.l   (sp)+,d0
    rts
    .balign 16
    move.l  d7,d0                               ;// d7.l
    add.l   (sp)+,d0
    rts

