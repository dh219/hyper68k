//--------------------------------------------------------------------
// Hyper68k : h68k.h
// 68030 Hypervisor for 68000/68010 virtual machines
//
// (c)2023 Anders Granlund
//--------------------------------------------------------------------
#ifndef _H68K_H_
#define _H68K_H_


#define H68K_PAGESIZE       256
#define H68K_DEBUGTRACE     0
#define H68K_DEBUGPRINT     1

#ifndef __asm_inc__
    #include "common.h"
    #define extvar(t,x) extern t x;
    #define extfunc(x)  extern void x(void);

    #define extiofb(x)   extern void x(uint32, uint8*);
    #define extiofw(x)   extern void x(uint32, uint16*);
    #define extiofl(x)   extern void x(uint32, uint32*);

    typedef void(*h68kIOF)(uint32 addr, void*);
    typedef void(*h68kIOFB)(uint32 addr, uint8*);
    typedef void(*h68kIOFW)(uint32 addr, uint16*);
    typedef void(*h68kIOFL)(uint32 addr, uint32*);

    #define extrwh(x)   extern uint8 x(uint32, void*);
    typedef uint8(*h68kRWHandler)(uint32,void*);

    #define h68k_IoReadLongAsWords      h68k_IoReadLongWW
    #define h68k_IoReadLongAsBytes      h68k_IoReadLongBBBB
    #define h68k_IoReadShortAsBytes     h68k_IoReadShortBB
    #define h68k_IoWriteLongAsWords     h68k_IoWriteLongWW
    #define h68k_IoWriteLongAsBytes     h68k_IoWriteLongBBBB
    #define h68k_IoWriteShortAsBytes    h68k_IoWriteShortBB


    //----------------------------------------------------------------
    // debug
    //----------------------------------------------------------------
    #if H68K_DEBUGPRINT    
        #define h68k_debugPrint(...)    DPRINT(__VA_ARGS__)
        void h68k_debugPrintValue(uint32 ident, uint32 value);
    #else
        #define h68k_debugPrint(...)
        #define h68k_debugPrintValue(...)
    #endif

    #if H68K_DEBUGPRINT && H68K_DEBUGTRACE
        void h68k_debugPrintTrace();
    #else
        #define h68k_debugPrintTrace()
    #endif

    struct h68kFatalDump
    {
        uint32 err; uint32 pc; uint32 sr; uint32 usp;
        uint32 d0; uint32 d1; uint32 d2; uint32 d3; uint32 d4; uint32 d5; uint32 d6; uint32 d7;
        uint32 a0; uint32 a1; uint32 a2; uint32 a3; uint32 a4; uint32 a5; uint32 a6; uint32 a7;
    };

    //----------------------------------------------------------------
    // public interface
    //----------------------------------------------------------------
    bool    h68k_Init();
    void    h68k_Run();
    void    h68k_Terminate();
    char*   h68k_GetLastError();

    void    h68k_SetCpuResetCallback(void(*func)());                            // when cpu is reset
    void    h68k_SetDeviceResetCallback(void(*func)());                         // when executing reset instruction
    void    h68k_SetFatalCallback(void(*func)(struct h68kFatalDump* dump));     // when something terrible has happened

    void    h68k_SetVector(uint32 vec, uint32 ipl, void(*func)());
    void    h68k_SetVectorIpl(uint32 vec, uint32 ipl);
    void    h68k_SetVectorHandler(uint32 vec, void(*func)());
    void    h68k_SetPrivilegeViolationHandler(uint32 start, uint32 end, void(*fsuper)(), void(*fuser)());

    uint32  h68k_GetMmuPageSize();

    void    h68k_MapMemory(uint32 start, uint32 end, uint32 dest);              // client space -> host space
    void    h68k_MapReadOnly(uint32 start, uint32 end, uint32 dest);            // client space -> host space (writes trigger bus error on client)
    void    h68k_RemapPage(uint32 laddr, uint32 paddr);                         // remap page

    void    h68k_MapFatal(uint32 start, uint32 end);                            // trigger fatal error on host
    void    h68k_MapInvalid(uint32 start, uint32 end);                          // trigger bus error on client
    void    h68k_MapDisconnected(uint32 start, uint32 end);                     // read ff, ignore write
    void    h68k_MapPassThrough(uint32 start, uint32 end);                      // untranslated access
    void    h68k_MapPassThroughSafe(uint32 start, uint32 end);                  // untranslated access (catches bus error and passes to client)

    void    h68k_MapIoRange(
                uint32 start,                       // client space start address
                uint32 end,                         // client space end address
                h68kIOFB readB, h68kIOFB writeB,    // default handler for byte access
                h68kIOFW readW, h68kIOFW writeW);   // default handler for word access

    void    h68k_MapIoRangeEx(
                uint32 start,                       // client space start address
                uint32 end,                         // client space end address
                h68kIOFB readB, h68kIOFB writeB,    // default handler for byte access
                h68kIOFW readW, h68kIOFW writeW,    // default handler for word access
                h68kIOFL readL, h68kIOFL writeL);   // default handler for long access

    void    h68k_MapIoByte(uint32 addr, h68kIOFB readFunc, h68kIOFB writeFunc);
    void    h68k_MapIoWord(uint32 addr, h68kIOFW readFunc, h68kIOFW writeFunc);
    void    h68k_MapIoLong(uint32 addr, h68kIOFL readFunc, h68kIOFL writeFunc);


#else

    #define extvar(t,x) .global _##x;
    #define extfunc(x)  .global _##x;
    #define extrwh(x)   .global _##x;

    #define extiofb(x)   .global _##x;
    #define extiofw(x)   .global _##x;
    #define extiofl(x)   .global _##x;

    .extern h68k_OnResetCpu;
    .extern h68k_OnResetDevices;
    .extern h68k_OnFatal;

#endif

//----------------------------------------------------------------
// some built-in helper access handlers
//----------------------------------------------------------------
extfunc(h68kIO_TriggerBerr);        // trigger client berr from io handler
extfunc(h68kIO_TriggerFatal);       // trigger fatal error from io handler

extiofb(h68k_IoIgnoreByte);         // ignore access
extiofw(h68k_IoIgnoreWord);
extiofl(h68k_IoIgnoreLong);

extiofb(h68k_IoBerrByte);           // trigger berr on access
extiofw(h68k_IoBerrWord);
extiofl(h68k_IoBerrLong);

extiofb(h68k_IoFatalByte);           // trigger fatal error on access
extiofw(h68k_IoFatalWord);
extiofl(h68k_IoFatalLong);

extiofb(h68k_IoReadByte00);         // read contant 0
extiofw(h68k_IoReadWord00);
extiofl(h68k_IoReadLong00);

extiofb(h68k_IoReadByteFF);         // read constant ff
extiofw(h68k_IoReadWordFF);
extiofl(h68k_IoReadLongFF);

extiofb(h68k_IoReadBytePT);         // pass through read
extiofw(h68k_IoReadWordPT);
extiofl(h68k_IoReadLongPT);
extiofb(h68k_IoWriteBytePT);        // pass through write
extiofw(h68k_IoWriteWordPT);
extiofl(h68k_IoWriteLongPT);

extrwh(h68k_mmuf_Fatal);            // trigger fatal error
extrwh(h68k_mmuf_Berr);             // trigger client bus error
extrwh(h68k_mmuf_Ignore);           // ignore access
extrwh(h68k_mmuf_ReadByteFF);       // read constant FF
extrwh(h68k_mmuf_ReadWordFF);       // read constant FFFF
extrwh(h68k_mmuf_ReadLongFF);       // read constant FFFFFFFF

//----------------------------------------------------------------
// constants
//----------------------------------------------------------------
#define H68K_CPU_68000          0x0000
#define H68K_CPU_68010          0x0010
#define H68K_CPU_68020          0x0020
#define H68K_CPU_68030          0x0030
#define H68K_CPU_68040          0x0040
#define H68K_CPU_68060          0x0060
#define H68K_CPU_68080          0x0080

#define H68K_MAP_WP             0x00000004
#define H68K_MAP_CI             0x00000040
#define H68K_MAP_S              0x00000100


//----------------------------------------------------------------
// variables
//----------------------------------------------------------------
extvar(uint16, client_cpu);
extvar(uint16, client_sr);      // only S bit
extvar(uint32, client_ssp);
extvar(uint32, client_usp);
extvar(uint32, client_vbr);     // 68010+
extvar(uint32, client_sfc);     // 68010+
extvar(uint32, client_dfc);     // 68010+

extvar(uint16, host_cpu);
extvar(uint32, host_ssp);
extvar(uint8*, host_vbr);
extvar(uint32, host_cacr);



//----------------------------------------------------------------
//
// 68000
//
//----------------------------------------------------------------
extfunc(vec68000_Group0);
extfunc(vec68000_Group1);
extfunc(vec68000_Group2);

extfunc(vec68000_Reset);
extfunc(vec68000_Fatal);
extfunc(vec68000_DebugTrace);
extfunc(vec68000_BusError);
extfunc(vec68000_AddrError);
extfunc(vec68000_PrivilegeViolation);

extfunc(pviol68000_PrivilegeViolation);
extfunc(pviol68000_IllegalInstruction);
extfunc(pviol68000_LineF);

extfunc(pviol68000_stop);           // stop
extfunc(pviol68000_reset);          // reset
extfunc(pviol68000_rte);            // rte
extfunc(pviol68000_move_usp_a0);    // movec usp,ax
extfunc(pviol68000_move_usp_a1);
extfunc(pviol68000_move_usp_a2);
extfunc(pviol68000_move_usp_a3);
extfunc(pviol68000_move_usp_a4);
extfunc(pviol68000_move_usp_a5);
extfunc(pviol68000_move_usp_a6);
extfunc(pviol68000_move_usp_a7);
extfunc(pviol68000_move_a0_usp);    // movec ax,usp
extfunc(pviol68000_move_a1_usp);
extfunc(pviol68000_move_a2_usp);
extfunc(pviol68000_move_a3_usp);
extfunc(pviol68000_move_a4_usp);
extfunc(pviol68000_move_a5_usp);
extfunc(pviol68000_move_a6_usp);
extfunc(pviol68000_move_a7_usp);
extfunc(pviol68000_move_sr_d0);     // move sr,dx
extfunc(pviol68000_move_sr_d1);
extfunc(pviol68000_move_sr_d2);
extfunc(pviol68000_move_sr_d3);
extfunc(pviol68000_move_sr_d4);
extfunc(pviol68000_move_sr_d5);
extfunc(pviol68000_move_sr_d6);
extfunc(pviol68000_move_sr_d7);
extfunc(pviol68000_move_sr_a0);     // move sr,(ax)
extfunc(pviol68000_move_sr_a1);
extfunc(pviol68000_move_sr_a2);
extfunc(pviol68000_move_sr_a3);
extfunc(pviol68000_move_sr_a4);
extfunc(pviol68000_move_sr_a5);
extfunc(pviol68000_move_sr_a6);
extfunc(pviol68000_move_sr_a7);
extfunc(pviol68000_move_sr_a0a);    // move sr,(ax)+
extfunc(pviol68000_move_sr_a1a);
extfunc(pviol68000_move_sr_a2a);
extfunc(pviol68000_move_sr_a3a);
extfunc(pviol68000_move_sr_a4a);
extfunc(pviol68000_move_sr_a5a);
extfunc(pviol68000_move_sr_a6a);
extfunc(pviol68000_move_sr_a7a);
extfunc(pviol68000_move_sr_a0b);    // move sr,-(ax)
extfunc(pviol68000_move_sr_a1b);
extfunc(pviol68000_move_sr_a2b);
extfunc(pviol68000_move_sr_a3b);
extfunc(pviol68000_move_sr_a4b);
extfunc(pviol68000_move_sr_a5b);
extfunc(pviol68000_move_sr_a6b);
extfunc(pviol68000_move_sr_a7b);
extfunc(pviol68000_move_sr_a0c);    // move sr,x(ax)
extfunc(pviol68000_move_sr_a1c);
extfunc(pviol68000_move_sr_a2c);
extfunc(pviol68000_move_sr_a3c);
extfunc(pviol68000_move_sr_a4c);
extfunc(pviol68000_move_sr_a5c);
extfunc(pviol68000_move_sr_a6c);
extfunc(pviol68000_move_sr_a7c);
extfunc(pviol68000_move_sr_a0d);    // move sr,x(ax,dx)
extfunc(pviol68000_move_sr_a1d);
extfunc(pviol68000_move_sr_a2d);
extfunc(pviol68000_move_sr_a3d);
extfunc(pviol68000_move_sr_a4d);
extfunc(pviol68000_move_sr_a5d);
extfunc(pviol68000_move_sr_a6d);
extfunc(pviol68000_move_sr_a7d);
extfunc(pviol68000_move_sr_absW);   // move sr,<address>
extfunc(pviol68000_move_sr_absL);
extfunc(pviol68000_move_d0_sr);     // move.w dx,sr
extfunc(pviol68000_move_d1_sr);
extfunc(pviol68000_move_d2_sr);
extfunc(pviol68000_move_d3_sr);
extfunc(pviol68000_move_d4_sr);
extfunc(pviol68000_move_d5_sr);
extfunc(pviol68000_move_d6_sr);
extfunc(pviol68000_move_d7_sr);
extfunc(pviol68000_move_a0_sr);     // move.w (ax),sr
extfunc(pviol68000_move_a1_sr);
extfunc(pviol68000_move_a2_sr);
extfunc(pviol68000_move_a3_sr);
extfunc(pviol68000_move_a4_sr);
extfunc(pviol68000_move_a5_sr);
extfunc(pviol68000_move_a6_sr);
extfunc(pviol68000_move_a7_sr);
extfunc(pviol68000_move_a0a_sr);    // move.w (ax)+,sr
extfunc(pviol68000_move_a1a_sr);
extfunc(pviol68000_move_a2a_sr);
extfunc(pviol68000_move_a3a_sr);
extfunc(pviol68000_move_a4a_sr);
extfunc(pviol68000_move_a5a_sr);
extfunc(pviol68000_move_a6a_sr);
extfunc(pviol68000_move_a7a_sr);
extfunc(pviol68000_move_a0b_sr);    // move.w -(ax),sr
extfunc(pviol68000_move_a1b_sr);
extfunc(pviol68000_move_a2b_sr);
extfunc(pviol68000_move_a3b_sr);
extfunc(pviol68000_move_a4b_sr);
extfunc(pviol68000_move_a5b_sr);
extfunc(pviol68000_move_a6b_sr);
extfunc(pviol68000_move_a7b_sr);
extfunc(pviol68000_move_a0c_sr);    // move.w x(ax),sr
extfunc(pviol68000_move_a1c_sr);
extfunc(pviol68000_move_a2c_sr);
extfunc(pviol68000_move_a3c_sr);
extfunc(pviol68000_move_a4c_sr);
extfunc(pviol68000_move_a5c_sr);
extfunc(pviol68000_move_a6c_sr);
extfunc(pviol68000_move_a7c_sr);
extfunc(pviol68000_move_a0d_sr);    // move.w x(ax,dx),sr
extfunc(pviol68000_move_a1d_sr);
extfunc(pviol68000_move_a2d_sr);
extfunc(pviol68000_move_a3d_sr);
extfunc(pviol68000_move_a4d_sr);
extfunc(pviol68000_move_a5d_sr);
extfunc(pviol68000_move_a6d_sr);
extfunc(pviol68000_move_a7d_sr);
extfunc(pviol68000_move_absW_sr);   // move.w <address>>,sr
extfunc(pviol68000_move_absL_sr);
extfunc(pviol68000_move_imm_sr);    // move.w #imm,sr
extfunc(pviol68000_and_imm_sr);     // and.w #imm,sr
extfunc(pviol68000_eor_imm_sr);     // eor.w #imm,sr
extfunc(pviol68000_or_imm_sr);      // or.w #imm,sr


//---------------------------------------------------------------------
//
// 68010
//
//---------------------------------------------------------------------
extfunc(vec68010_Group0);
extfunc(vec68010_Group1);
extfunc(vec68010_Group2);
extfunc(pviol68010_rte);
// todo: movec vbr,sfc,dfc
// todo: moves




//---------------------------------------------------------------------
// internal stuff
//---------------------------------------------------------------------
#ifdef __asm_inc__

    //----------------------------------------------------------------------------------------------
    // Status Register
    // 68030: TTSM.III...XNZVC
    // 68010: T.S..III...XNZVC
    // 68000: T.S..III...XNZVC
    //----------------------------------------------------------------------------------------------
    #define SR_BITL_T       15
    #define SR_BITL_S       13
    #define SR_BITB_T       7
    #define SR_BITB_S       5

    #define SR_MASK         0xA71F
    #define SR_MASK_T       0x8000
    #define SR_MASK_S       0x2000
    #define SR_MASK_I       0x0700
    #define SR_MASK_C       0x001F
    #define SR_MASK_IC      0x071F

    #define SR_MASK_NT      0x271F
    #define SR_MASK_NS      0x871F
    #define SR_MASK_NI      0x401F
    #define SR_MASK_NC      0xA700

#if H68K_DEBUGPRINT
    #define H68K_PRINTVALUE(_id,_val) \
        move.l  _val,-(sp); \
        move.l  _id,-(sp); \
        jsr     h68k_debugPrintValue; \
        addq.l  #8,sp;
#else
    #define H68K_PRINTVALUE(_id,_val)
#endif

    #define H68K_FATAL(_id) \
        move.l  #0xdeadbadd,-(sp); \
        move.l  _id,-(sp); \
        move.l  #0xabbadabb,-(sp); \
        jsr     _vec68000_Fatal.l

    // error codes:
    //     0xdeadbe01 : berr   : not data fault
    //     0xdeadbe02 : berr   : not invalid long-format page descriptor
    //     0xdeadbe04 : berr   : (unimplemented) rmw
    //     0xdeadbe05 : berr   : (unimplemented) rmw
    //     0xdeadbe06 : berr   : invalid callback
    //     0xdeadbeff : berr   : fatal error access handler

#endif

extrwh(h68k_mmuf_rb);   // direct access
extrwh(h68k_mmuf_rw);
extrwh(h68k_mmuf_rl);
extrwh(h68k_mmuf_wb);
extrwh(h68k_mmuf_ww);
extrwh(h68k_mmuf_wl);
extrwh(h68k_mmuf_r3);
extrwh(h68k_mmuf_w3);
extrwh(h68k_mmuf_rm);

extrwh(h68k_mmuf_rbs);  // direct access (safe, berr -> client)
extrwh(h68k_mmuf_rws);
extrwh(h68k_mmuf_rls);
extrwh(h68k_mmuf_wbs);
extrwh(h68k_mmuf_wws);
extrwh(h68k_mmuf_wls);
extrwh(h68k_mmuf_r3s);
extrwh(h68k_mmuf_w3s);
extrwh(h68k_mmuf_rms);

extrwh(h68k_mmuf_rbc);  // one callback per hardware page
extrwh(h68k_mmuf_rwc);
extrwh(h68k_mmuf_rlc);
extrwh(h68k_mmuf_wbc);
extrwh(h68k_mmuf_wwc);
extrwh(h68k_mmuf_wlc);
extrwh(h68k_mmuf_r3c);
extrwh(h68k_mmuf_w3c);
extrwh(h68k_mmuf_rmc);

extrwh(h68k_mmuf_rbcc);  // software pagetable
extrwh(h68k_mmuf_rwcc);
extrwh(h68k_mmuf_rlcc);
extrwh(h68k_mmuf_wbcc);
extrwh(h68k_mmuf_wwcc);
extrwh(h68k_mmuf_wlcc);
extrwh(h68k_mmuf_r3cc);
extrwh(h68k_mmuf_w3cc);
extrwh(h68k_mmuf_rmcc);

extiofw(h68k_IoReadWordBB);
extiofl(h68k_IoReadLongWW);
extiofl(h68k_IoReadLongBBBB);
extiofl(h68k_IoReadLongWBB);
extiofl(h68k_IoReadLongBBW);

extiofw(h68k_IoWriteWordBB);
extiofl(h68k_IoWriteLongWW);
extiofl(h68k_IoWriteLongBBBB);
extiofl(h68k_IoWriteLongWBB);
extiofl(h68k_IoWriteLongBBW);


#endif // _H68K_H_

