;//--------------------------------------------------------------------
;// Hyper68k : xvec.S
;// Default and generic exception handlers
;// 
;// (c)2023 Anders Granlund
;//--------------------------------------------------------------------
;// todo:
;//     - specialized (faster) interrupt vectors
;//     - 68010+ longframe, exception handlers
;//--------------------------------------------------------------------
#define __asm_inc__
#include "h68k.h"
#undef __asm_inc__

	.text
	.even

;//----------------------------------------------------------------------------------------------
;// Reset handler
;//
;//  This is where we enter from h68k_Run()
;//  Calls the OnResetCpu() callback to let application act on it if needed
;//
;//  It reinitializes everything necessary so could therefore be installed as, or jumped from,
;//  the hosts hardware reset vector - if you'd want the reset button to reset the
;//  client rather than the host, or perform custom actions in the OnResetCpu() callback
;//----------------------------------------------------------------------------------------------
	.balign 4
_vec68000_Reset:
    move.w  #0x2700,sr                          ;// disable interrupts
    move.l  #0x0808,d0                          ;// flush and disable cache
    movec   d0,cacr
    ;// init host regs
    move.l  _host_ssp,sp                        ;// ssp
    move.l  #1,a6
    movec   a6,sfc                              ;// sfc = usermode data space
    movec   a6,dfc                              ;// dfc = usermode data space
    move.l  _host_vbr,a6
    movec   a6,vbr                              ;// vbr
    ;// restart mmu
    nop
    pflusha                                     ;// flush atc
    nop
    sub.l   #4,sp
    pmove   tc,(sp)
    and.l   #0x7FFFFFFF,(sp)                    ;// disable E flag in tc
    pmove   (sp),tc
    pmove   tt1,(sp)
    and.l   #0xFFFF7FFF,(sp)                    ;// disable E flag in tt1
    pmove   (sp),tt1
    pmove   tt0,(sp)
    and.l   #0xFFFF7FFF,(sp)                    ;// disable E flag in tt0
    pmove   (sp),tt0
    addq.l  #4,sp
    nop
    pflusha                                     ;// flush atc
    nop
    move.l  #_h68k_mmu,a6                       ;// set all mmu regs
    pmove   0(a6),srp
	pmove	8(a6),crp
	pmove	16(a6),tt0
	pmove	20(a6),tt1
	pmove	24(a6),tc
    nop
    pflusha                                     ;// flush atc
    nop

    ;// reset client regs
    move.w  #0x2000,_client_sr                  ;// sr (supervisor)
    moves.l 0x0,a6
    movec   a6,usp                              ;// ssp (from location 0x0 in client address space)
    move.l  a6,_client_ssp
    move.l  #0,_client_vbr                      ;// vbr (68010+)
    ;// application defined reset callback
    move.l  _h68k_OnResetCpu,a6
    cmpa.l  #0,a6
    beq.b   0f
    movem.l d0-d1/a0-a1,-(sp)                   ;// save gcc scratch regs
    jsr     (a6)                                ;// run callback function
    movem.l (sp)+,d0-d1/a0-a1                   ;// restore gcc scratch regs
    ;// reset cacr and flush
    move.l  _host_cacr,d0
    or.l    #0x0808,d0
    movec   d0,cacr
    ;// go usermode
0:  move.w  #0,-(sp)                            ;// fake stackframe
    moves.l 0x4,a6
    move.l  a6,-(sp)                            ;// real PC (from location 0x4 in client address space)
    move.w  #0x0700,-(sp)                       ;// real SR (usermode)
    rte



;//----------------------------------------------------------------------------------------------
;// Address error
;//
;//----------------------------------------------------------------------------------------------
_vec68000_AddrError:
    move.w  #0x2700,sr
    bra.b   _vec68000_AddrError

;//----------------------------------------------------------------------------------------------
;// Fatal error handler
;//
;// Collects information and calls h68k_FatalError, which may in turn call
;// the OnFatal callback if set up the application.
;//----------------------------------------------------------------------------------------------
	.balign 4
_vec68000_Fatal:
    move.l  a7,-(sp)                            ;// save non-decremented a7 first
    movem.l d0-d7/a0-a6,-(sp)                   ;// all regs
    movec   usp,d0
    move.l  d0,-(sp)                            ;// usp
    move.w  sr,d0
    and.l   #0x0000FFFF,d0
    move.l  d0,-(sp)                            ;// sr
    move.l  72(sp),-(sp)                        ;// pc
    move.l  #0,-(sp)                            ;// error number
    cmp.l   #0xdeadbadd,92(sp)                  ;// compare magic1 from FATAL macro
    bne.b   0f
    cmp.l   #0xabbadabb,84(sp)                  ;// compare magic2 from FATAL macro
    bne.b   0f
    move.l  88(sp),(sp)                         ;// macro was used, copy error argument
    sub.l   #24,4(sp)                           ;// also modify pc with length of macro
0:  move.l  sp,d0                               ;// push dump frame as argument
    move.l  d0,-(sp)
    jsr     _h68k_FatalError                    ;// fatal error
    jsr     _h68k_Terminate                     ;// terminate


;//----------------------------------------------------------------------------------------------
;// Debug trace
;//
;//----------------------------------------------------------------------------------------------
#if H68K_DEBUGTRACE
_vec68000_DebugTrace:
    move.w  #0x2700,sr                          ;// disable interrupts
    movem.l d0-d1/a0-a1,-(sp)
    move.l  18(sp),-(sp)
    jsr     _h68k_debugTrace
    addq.l  #4,sp
    movem.l (sp)+,d0-d1/a0-a1
    rte
#endif

;//----------------------------------------------------------------------------------------------
;// (68000) Group0 exception trampoline
;//
;//     Reset, BusError, AdddrError (68000 cannot RTE from these)
;//
;//     12: PC (Lo)
;//     10: PC (Hi)
;//      8:  Status Register
;//      6:  Instruction Register
;//      4:  Access address (Lo)
;//      2:  Access address (Hi)
;//      0:  Access type and function code
;//          ...........abccc
;//                     a = R/W
;//                     b = Processing an instruction
;//                     c = Function code
;//----------------------------------------------------------------------------------------------
	.balign 4
_vec68000_Group0:
    move.w  #0x2700,sr                          ;// disable interrupts
    movem.l d0-d3/a0/a7,-(sp)                   ;// save regs
    movec   usp,a0                              ;// a0 = client a7
    move.w  _client_sr,d0                       ;// d0 = client sr
    bne.b   0f                                  ;// already super?
    bset.b  #SR_BITB_S,_client_sr
    move.l  a0,_client_usp                      ;// backup client usp
    move.l  _client_ssp,a0                      ;// activate client ssp
0:  ;// build client stackframe
    move.w  24+0(sp),d1                         ;// d1 = stacked SR
    move.l  24+2(sp),d2                         ;// d2 = stacked PC
    moves.l d2,-(a0)                            ;// client frame: PC
    and.w   #SR_MASK_NS,d1                      ;// d1 = stacked SR (without host super bit)
    or.w    d1,d0                               ;// d0 = stacked SR (with client super bit)
    moves.w d0,-(a0)                            ;// client frame: SR
    clr.l   d0
    moves.w d0,-(a0)                            ;// client frame: (fake) Instruction register
    moves.l d0,-(a0)                            ;// client frame: (fake) Access address
    moves.w d0,-(a0)                            ;// client frame: (fake) Access type and FC
    movec   a0,usp                              ;// update client a7
    ;// setup jump
    move.w  24+6(sp),d3                         ;// d3 = exception info
    move.w  d3,d2
    and.w   #0x0FFF,d2                          ;// d2 = vector offset
    moves.l (d2.w),d0                           ;// d0 = vector address
    ;// replace host stackframe
    move.l  sp,a0                               ;// a0 = sp
    add.l   #24,a0                              ;//  + saved regs
    add.l   (_sfs_table+0x8000,d3.w),a0         ;//  + stackframe
    move.w  #0,-(a0)                            ;// RTE: format
    move.l  d0,-(a0)                            ;// RTE: PC
    and.w   #SR_MASK_IC,d1                      ;// d1 = stacked IPL + CCR
    move.w  (_ipl_table,d2.w),d0                ;// get IPL from table
    beq.b   1f                                  ;// zero? just set the stacked IPL + stacked CCR
    and.w   #SR_MASK_C,d1                       ;// else? set table IPL + stacked CCR
    or.w    d0,d1
1:  move.w  d1,-(a0)                            ;// RTE: SR
    move.l  a0,20(sp)                           ;// this will be isp after popping regs
    movem.l (sp),d0-d3/a0/a7                    ;// restore regs and set new sp
#if H68K_DEBUGTRACE
    or.w #0x8000,(sp)                           ;// trace usermode
#endif
    rte                                         ;// continue in usermode


;//----------------------------------------------------------------------------------------------
;// (68010) Group0 exception trampoline
;//
;// 0 - Reset, BusError, AdddrError
;//
;// Stackframe:
;//     56  reserved
;//     ..  ........
;//     24  Instruction Input Buffer
;//     22  reserved
;//     20  Data Input Buffer
;//     18  reserved
;//     16  Data Output buffer
;//     14  reserved
;//     12  Fault Address (Lo)
;//     10  Fault Address (Hi)
;//      8  SSW
;//      6  Format/Vector
;//      4  Program Counter (Hi)
;//      2  Program Counter (Lo)
;//      0  Status Register
;//----------------------------------------------------------------------------------------------
	.balign 4
_vec68010_Group0:
    H68K_FATAL(0xdead0001)
    rte


;//----------------------------------------------------------------------------------------------
;// (68000) Generic Group1/2 exception trampoline
;//
;// 1 - Trace, Illegal, Unimplemented, PrivViol
;// 2 - Trap, TrapV, Chk, DivZ
;//
;// Stackframe:
;//     4: PC (Lo)
;//     2: PC (Hi)
;//     0:  Status Register
;//----------------------------------------------------------------------------------------------
	.balign 4
_vec68000_Group1:
_vec68000_Group2:
    move.w  #0x2700,sr                          ;// disable interrupts
    movem.l d0-d3/a0/a7,-(sp)                   ;// save regs
    movec   usp,a0                              ;// a0 = client a7
    move.w  _client_sr,d0                       ;// d0 = client sr
    bne.b   0f                                  ;// already super?
    bset.b  #SR_BITB_S,_client_sr
    move.l  a0,_client_usp                      ;// backup client usp
    move.l  _client_ssp,a0                      ;// activate client ssp
0:  ;// build client stackframe
    move.w  24+0(sp),d1                         ;// d1 = stacked SR
    move.l  24+2(sp),d2                         ;// get stacked PC
    moves.l d2,-(a0)                            ;// PC -> client stackframe
    btst.l  #SR_BITL_S,d1                       ;// if we came here from supervisor then
    beq.b   1f                                  ;// set bit 0 of PC in client stackframe
    bset.b  #0,3(a0)                            ;
//    bset.b  #7,0(a0)                            ;
1:  and.w   #SR_MASK_NS,d1                      ;// d1 = stacked SR (without host super bit)
    or.w    d1,d0                               ;// d0 = stacked SR (with client super bit)
    moves.w d0,-(a0)                            ;// SR -> client stackframe
    movec   a0,usp                              ;// update client a7
    ;// setup jump
    move.w  24+6(sp),d3                         ;// d3 = exception info
    move.w  d3,d2
    and.w   #0x0FFF,d2                          ;// d2 = vector offset
    moves.l (d2.w),d0                           ;// d0 = vector address
    ;// replace host stackframe
    move.l  sp,a0                               ;// a0 = sp
    add.l   #24,a0                              ;//  + saved regs
    add.l   (_sfs_table+0x8000,d3.w),a0         ;//  + stackframe
    move.w  #0,-(a0)                            ;// RTE: format
    move.l  d0,-(a0)                            ;// RTE: PC
    and.w   #SR_MASK_IC,d1                      ;// d1 = stacked IPL + CCR
    move.w  (_ipl_table,d2.w),d0                ;// get IPL from table
    beq.b   2f                                  ;// zero? just set the stacked IPL + stacked CCR
    and.w   #SR_MASK_C,d1                       ;// else? set table IPL + stacked CCR
    or.w    d0,d1
2:  move.w  d1,-(a0)                            ;// RTE: SR
    move.l  a0,20(sp)                           ;// this will be isp after popping regs
    movem.l (sp),d0-d3/a0/a7                    ;// restore regs and set new sp
#if H68K_DEBUGTRACE
    or.w #0x8000,(sp)                           ;// trace usermode
#endif
    rte                                         ;// continue in usermode



;//----------------------------------------------------------------------------------------------
;// (68010) Generic Group1/2 exception trampoline
;//
;// 1 - Trace, Illegal, Unimplemented, PrivViol
;// 2 - Trap, TrapV, Chk, DivZ
;//
;// StackFrame:
;//     6  Format/Vector
;//     4  Program Counter (Hi)
;//     2  Program Counter (Lo)
;//     0  Status Register
;//----------------------------------------------------------------------------------------------
	.balign 4
_vec68010_Group1:
_vec68010_Group2:
    H68K_FATAL(0xdead0002)
    rte

