//--------------------------------------------------------------------
// Hyper68k : vec.c
// Sets up default vectors and privileged instruction handlers,
// and provides an API for the application to customize.
// 
// (c)2023 Anders Granlund
//--------------------------------------------------------------------
#include "h68k.h"

uint32 pviols_table[0x10000];           // 256kb
uint32 pviolu_table[0x10000];           // 256kb
uint32 sfs_table[0x10000 / 4];          //  64kb
uint32 vec_table[256];                  //   1kb
uint32 ipl_table[256];                  //   1kb

//-------------------------------------------------------
//
// Init default vectors + privviol handlers
//
//-------------------------------------------------------
bool h68k_InitVectors()
{
	//-------------------------------------------------------
    // Create lookup table for (host) stackframe sizes
	//-------------------------------------------------------
    const uint32 HostStackFrameSizes[16] = {
         4, // $0 - 4 word
         4, // $1 - 4 word throwaway
         6, // $2 - 6 word
         0,
         0,
         0,
         0,
         0,
         0,
        10, // $9 - coprocessor mid-instruction
        16, // $A - short bus fault
        46, // $B - long bus fault
         0,
         0,
         0,
         0
    };
    for (uint16 i=0; i<16; i++) {
        for (uint16 j=0; j<512; j++) {
            uint16 offs = (i<<10)|j;
            offs = (offs < 0x2000) ? (offs | 0x2000) : (offs & 0x1FFF);
            sfs_table[offs] = (HostStackFrameSizes[i] << 1);
        }
    }

	//-------------------------------------------------------
    // Usermode vectors (when client code is running)
	//-------------------------------------------------------
    host_vbr = (uint8*)AllocMem(256*4, 256);

    switch (client_cpu)
    {
        default:
        case H68K_CPU_68000:
        {
            for (uint16 i=0; i<256*4; i+=4)                         // defaults
                h68k_SetVector(i, 0, vec68000_Group2);
            h68k_SetVector(0x08, 0, vec68000_Group0);                // bus error 
            h68k_SetVector(0x0c, 0, vec68000_Group0);                // address error
            h68k_SetVector(0x10, 0, vec68000_Group1);                // illegal instruction
            h68k_SetVector(0x14, 0, vec68000_Group2);                // divide by zero
            h68k_SetVector(0x18, 0, vec68000_Group2);                // chk, chk2
            h68k_SetVector(0x1c, 0, vec68000_Group2);                // trapv, cptrapcc, trapcc
            h68k_SetVector(0x20, 0, vec68000_Group1);                // privilege violation
            h68k_SetVector(0x24, 0, vec68000_Group1);                // trace
            h68k_SetVector(0x28, 0, vec68000_Group2);                // line-a
            h68k_SetVector(0x2C, 0, vec68000_Group2);                // line-f
            h68k_SetVector(0x60, 1, vec68000_Group1);                // spurious interrupt 
            h68k_SetVector(0x64, 2, vec68000_Group1);                // int 1
            h68k_SetVector(0x68, 3, vec68000_Group1);                // int 2
            h68k_SetVector(0x6c, 4, vec68000_Group1);                // int 3
            h68k_SetVector(0x70, 5, vec68000_Group1);                // int 4
            h68k_SetVector(0x74, 6, vec68000_Group1);                // int 5
            h68k_SetVector(0x78, 7, vec68000_Group1);                // int 6
            h68k_SetVector(0x7c, 7, vec68000_Group1);                // int 7
            h68k_SetVector(0x80, 0, vec68000_Group2);                // trap0
            h68k_SetVector(0x84, 0, vec68000_Group2);                // trap1
            h68k_SetVector(0x88, 0, vec68000_Group2);                // trap2
            h68k_SetVector(0x8c, 0, vec68000_Group2);                // trap3
            h68k_SetVector(0x90, 0, vec68000_Group2);                // trap4
            h68k_SetVector(0x94, 0, vec68000_Group2);                // trap5
            h68k_SetVector(0x98, 0, vec68000_Group2);                // trap6
            h68k_SetVector(0x9c, 0, vec68000_Group2);                // trap7
            h68k_SetVector(0xa0, 0, vec68000_Group2);                // trap8
            h68k_SetVector(0xa4, 0, vec68000_Group2);                // trap9
            h68k_SetVector(0xa8, 0, vec68000_Group2);                // trap10
            h68k_SetVector(0xac, 0, vec68000_Group2);                // trap11
            h68k_SetVector(0xb0, 0, vec68000_Group2);                // trap12
            h68k_SetVector(0xb4, 0, vec68000_Group2);                // trap13
            h68k_SetVector(0xb8, 0, vec68000_Group2);                // trap14
            h68k_SetVector(0xbc, 0, vec68000_Group2);                // trap15
            for (uint32 i=0x100; i<0x400; i+=4)                      // user defined vectors
                h68k_SetVector(i, 0, vec68000_Group2);
        }break;

        case H68K_CPU_68010:
        {
            for (uint16 i=0; i<256*4; i+=4)                          // defaults
                h68k_SetVector(i, 0, vec68010_Group2);
            h68k_SetVector(0x08, 0, vec68010_Group0);                // bus error 
            h68k_SetVector(0x0c, 0, vec68010_Group0);                // address error
            h68k_SetVector(0x10, 0, vec68010_Group1);                // illegal instruction
            h68k_SetVector(0x14, 0, vec68010_Group2);                // divide by zero
            h68k_SetVector(0x18, 0, vec68010_Group2);                // chk, chk2
            h68k_SetVector(0x1c, 0, vec68010_Group2);                // trapv, cptrapcc, trapcc
            h68k_SetVector(0x20, 0, vec68010_Group1);                // privilege violation
            h68k_SetVector(0x24, 0, vec68010_Group1);                // trace
            h68k_SetVector(0x28, 0, vec68010_Group2);                // line-a
            h68k_SetVector(0x2C, 0, vec68010_Group2);                // line-f
            h68k_SetVector(0x60, 1, vec68010_Group1);                // spurious interrupt 
            h68k_SetVector(0x64, 2, vec68010_Group1);                // int 1
            h68k_SetVector(0x68, 3, vec68010_Group1);                // int 2
            h68k_SetVector(0x6c, 4, vec68010_Group1);                // int 3
            h68k_SetVector(0x70, 5, vec68010_Group1);                // int 4
            h68k_SetVector(0x74, 6, vec68010_Group1);                // int 5
            h68k_SetVector(0x78, 7, vec68010_Group1);                // int 6
            h68k_SetVector(0x7c, 7, vec68010_Group1);                // int 7
            h68k_SetVector(0x80, 0, vec68010_Group2);                // trap0
            h68k_SetVector(0x84, 0, vec68010_Group2);                // trap1
            h68k_SetVector(0x88, 0, vec68010_Group2);                // trap2
            h68k_SetVector(0x8c, 0, vec68010_Group2);                // trap3
            h68k_SetVector(0x90, 0, vec68010_Group2);                // trap4
            h68k_SetVector(0x94, 0, vec68010_Group2);                // trap5
            h68k_SetVector(0x98, 0, vec68010_Group2);                // trap6
            h68k_SetVector(0x9c, 0, vec68010_Group2);                // trap7
            h68k_SetVector(0xa0, 0, vec68010_Group2);                // trap8
            h68k_SetVector(0xa4, 0, vec68010_Group2);                // trap9
            h68k_SetVector(0xa8, 0, vec68010_Group2);                // trap10
            h68k_SetVector(0xac, 0, vec68010_Group2);                // trap11
            h68k_SetVector(0xb0, 0, vec68010_Group2);                // trap12
            h68k_SetVector(0xb4, 0, vec68010_Group2);                // trap13
            h68k_SetVector(0xb8, 0, vec68010_Group2);                // trap14
            h68k_SetVector(0xbc, 0, vec68010_Group2);                // trap15
            for (uint32 i=0x100; i<0x400; i+=4)                      // user defined vectors
                h68k_SetVector(i, 0, vec68010_Group2);

        }break;        
    }

    h68k_SetVectorHandler(0x04, vec68000_Reset);
    h68k_SetVectorHandler(0x08, vec68000_BusError); 
    //h68k_SetVectorHandler(0x0c, vec68000_BusError); 
    h68k_SetVectorHandler(0x0c, vec68000_AddrError);
    h68k_SetVectorHandler(0x20, vec68000_PrivilegeViolation);


	//--------------------------------------------------------------------------------------------------------------
	// Privilege violation handlers
    //                                start   end     super                               user
    h68k_SetPrivilegeViolationHandler(0x0000, 0xFFFF, pviol68000_IllegalInstruction,      pviol68000_IllegalInstruction);
    h68k_SetPrivilegeViolationHandler(0xF000, 0xFFFF, pviol68000_LineF,                   pviol68000_LineF);
    h68k_SetPrivilegeViolationHandler(0x4e72, 0x4e72, pviol68000_stop,                    pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x4e70, 0x4e70, pviol68000_reset,                   pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x4e73, 0x4e73, pviol68000_rte,                     pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x4E60, 0x4E60, pviol68000_move_a0_usp,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x4E61, 0x4E61, pviol68000_move_a1_usp,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x4E62, 0x4E62, pviol68000_move_a2_usp,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x4E63, 0x4E63, pviol68000_move_a3_usp,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x4E64, 0x4E64, pviol68000_move_a4_usp,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x4E65, 0x4E65, pviol68000_move_a5_usp,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x4E66, 0x4E66, pviol68000_move_a6_usp,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x4E67, 0x4E67, pviol68000_move_a7_usp,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x4E68, 0x4E68, pviol68000_move_usp_a0,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x4E69, 0x4E69, pviol68000_move_usp_a1,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x4E6a, 0x4E6a, pviol68000_move_usp_a2,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x4E6b, 0x4E6b, pviol68000_move_usp_a3,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x4E6c, 0x4E6c, pviol68000_move_usp_a4,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x4E6d, 0x4E6d, pviol68000_move_usp_a5,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x4E6e, 0x4E6e, pviol68000_move_usp_a6,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x4E6f, 0x4E6f, pviol68000_move_usp_a7,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46f8, 0x46f8, pviol68000_move_absW_sr,            pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46f9, 0x46f9, pviol68000_move_absL_sr,            pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46fc, 0x46fc, pviol68000_move_imm_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x027c, 0x027c, pviol68000_and_imm_sr,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x0a7c, 0x0a7c, pviol68000_eor_imm_sr,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x007c, 0x007c, pviol68000_or_imm_sr,               pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46c0, 0x46c0, pviol68000_move_d0_sr,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46c1, 0x46c1, pviol68000_move_d1_sr,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46c2, 0x46c2, pviol68000_move_d2_sr,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46c3, 0x46c3, pviol68000_move_d3_sr,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46c4, 0x46c4, pviol68000_move_d4_sr,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46c5, 0x46c5, pviol68000_move_d5_sr,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46c6, 0x46c6, pviol68000_move_d6_sr,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46c7, 0x46c7, pviol68000_move_d7_sr,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46d0, 0x46d0, pviol68000_move_a0_sr,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46d1, 0x46d1, pviol68000_move_a1_sr,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46d2, 0x46d2, pviol68000_move_a2_sr,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46d3, 0x46d3, pviol68000_move_a3_sr,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46d4, 0x46d4, pviol68000_move_a4_sr,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46d5, 0x46d5, pviol68000_move_a5_sr,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46d6, 0x46d6, pviol68000_move_a6_sr,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46d7, 0x46d7, pviol68000_move_a7_sr,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46d8, 0x46d8, pviol68000_move_a0a_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46d9, 0x46d9, pviol68000_move_a1a_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46da, 0x46da, pviol68000_move_a2a_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46db, 0x46db, pviol68000_move_a3a_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46dc, 0x46dc, pviol68000_move_a4a_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46dd, 0x46dd, pviol68000_move_a5a_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46de, 0x46de, pviol68000_move_a6a_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46df, 0x46df, pviol68000_move_a7a_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46e0, 0x46e0, pviol68000_move_a0b_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46e1, 0x46e1, pviol68000_move_a1b_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46e2, 0x46e2, pviol68000_move_a2b_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46e3, 0x46e3, pviol68000_move_a3b_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46e4, 0x46e4, pviol68000_move_a4b_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46e5, 0x46e5, pviol68000_move_a5b_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46e6, 0x46e6, pviol68000_move_a6b_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46e7, 0x46e7, pviol68000_move_a7b_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46e8, 0x46e8, pviol68000_move_a0c_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46e9, 0x46e9, pviol68000_move_a1c_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46ea, 0x46ea, pviol68000_move_a2c_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46eb, 0x46eb, pviol68000_move_a3c_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46ec, 0x46ec, pviol68000_move_a4c_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46ed, 0x46ed, pviol68000_move_a5c_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46ee, 0x46ee, pviol68000_move_a6c_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46ef, 0x46ef, pviol68000_move_a7c_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46f0, 0x46f0, pviol68000_move_a0d_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46f1, 0x46f1, pviol68000_move_a1d_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46f2, 0x46f2, pviol68000_move_a2d_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46f3, 0x46f3, pviol68000_move_a3d_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46f4, 0x46f4, pviol68000_move_a4d_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46f5, 0x46f5, pviol68000_move_a5d_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46f6, 0x46f6, pviol68000_move_a6d_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x46f7, 0x46f7, pviol68000_move_a7d_sr,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40f8, 0x40f8, pviol68000_move_sr_absW,            pviol68000_move_sr_absW);
    h68k_SetPrivilegeViolationHandler(0x40f9, 0x40f9, pviol68000_move_sr_absL,            pviol68000_move_sr_absL);
    h68k_SetPrivilegeViolationHandler(0x40c0, 0x40c0, pviol68000_move_sr_d0,              pviol68000_move_sr_d0);
    h68k_SetPrivilegeViolationHandler(0x40c1, 0x40c1, pviol68000_move_sr_d1,              pviol68000_move_sr_d1);
    h68k_SetPrivilegeViolationHandler(0x40c2, 0x40c2, pviol68000_move_sr_d2,              pviol68000_move_sr_d2);
    h68k_SetPrivilegeViolationHandler(0x40c3, 0x40c3, pviol68000_move_sr_d3,              pviol68000_move_sr_d3);
    h68k_SetPrivilegeViolationHandler(0x40c4, 0x40c4, pviol68000_move_sr_d4,              pviol68000_move_sr_d4);
    h68k_SetPrivilegeViolationHandler(0x40c5, 0x40c5, pviol68000_move_sr_d5,              pviol68000_move_sr_d5);
    h68k_SetPrivilegeViolationHandler(0x40c6, 0x40c6, pviol68000_move_sr_d6,              pviol68000_move_sr_d6);
    h68k_SetPrivilegeViolationHandler(0x40c7, 0x40c7, pviol68000_move_sr_d7,              pviol68000_move_sr_d7);
    h68k_SetPrivilegeViolationHandler(0x40d0, 0x40d0, pviol68000_move_sr_a0,              pviol68000_move_sr_a0);
    h68k_SetPrivilegeViolationHandler(0x40d1, 0x40d1, pviol68000_move_sr_a1,              pviol68000_move_sr_a1);
    h68k_SetPrivilegeViolationHandler(0x40d2, 0x40d2, pviol68000_move_sr_a2,              pviol68000_move_sr_a2);
    h68k_SetPrivilegeViolationHandler(0x40d3, 0x40d3, pviol68000_move_sr_a3,              pviol68000_move_sr_a3);
    h68k_SetPrivilegeViolationHandler(0x40d4, 0x40d4, pviol68000_move_sr_a4,              pviol68000_move_sr_a4);
    h68k_SetPrivilegeViolationHandler(0x40d5, 0x40d5, pviol68000_move_sr_a5,              pviol68000_move_sr_a5);
    h68k_SetPrivilegeViolationHandler(0x40d6, 0x40d6, pviol68000_move_sr_a6,              pviol68000_move_sr_a6);
    h68k_SetPrivilegeViolationHandler(0x40d7, 0x40d7, pviol68000_move_sr_a7,              pviol68000_move_sr_a7);
    h68k_SetPrivilegeViolationHandler(0x40d8, 0x40d8, pviol68000_move_sr_a0a,             pviol68000_move_sr_a0a);
    h68k_SetPrivilegeViolationHandler(0x40d9, 0x40d9, pviol68000_move_sr_a1a,             pviol68000_move_sr_a1a);
    h68k_SetPrivilegeViolationHandler(0x40da, 0x40da, pviol68000_move_sr_a2a,             pviol68000_move_sr_a2a);
    h68k_SetPrivilegeViolationHandler(0x40db, 0x40db, pviol68000_move_sr_a3a,             pviol68000_move_sr_a3a);
    h68k_SetPrivilegeViolationHandler(0x40dc, 0x40dc, pviol68000_move_sr_a4a,             pviol68000_move_sr_a4a);
    h68k_SetPrivilegeViolationHandler(0x40dd, 0x40dd, pviol68000_move_sr_a5a,             pviol68000_move_sr_a5a);
    h68k_SetPrivilegeViolationHandler(0x40de, 0x40de, pviol68000_move_sr_a6a,             pviol68000_move_sr_a6a);
    h68k_SetPrivilegeViolationHandler(0x40df, 0x40df, pviol68000_move_sr_a7a,             pviol68000_move_sr_a7a);
    h68k_SetPrivilegeViolationHandler(0x40e0, 0x40e0, pviol68000_move_sr_a0b,             pviol68000_move_sr_a0b);
    h68k_SetPrivilegeViolationHandler(0x40e1, 0x40e1, pviol68000_move_sr_a1b,             pviol68000_move_sr_a1b);
    h68k_SetPrivilegeViolationHandler(0x40e2, 0x40e2, pviol68000_move_sr_a2b,             pviol68000_move_sr_a2b);
    h68k_SetPrivilegeViolationHandler(0x40e3, 0x40e3, pviol68000_move_sr_a3b,             pviol68000_move_sr_a3b);
    h68k_SetPrivilegeViolationHandler(0x40e4, 0x40e4, pviol68000_move_sr_a4b,             pviol68000_move_sr_a4b);
    h68k_SetPrivilegeViolationHandler(0x40e5, 0x40e5, pviol68000_move_sr_a5b,             pviol68000_move_sr_a5b);
    h68k_SetPrivilegeViolationHandler(0x40e6, 0x40e6, pviol68000_move_sr_a6b,             pviol68000_move_sr_a6b);
    h68k_SetPrivilegeViolationHandler(0x40e7, 0x40e7, pviol68000_move_sr_a7b,             pviol68000_move_sr_a7b);
    h68k_SetPrivilegeViolationHandler(0x40e8, 0x40e8, pviol68000_move_sr_a0c,             pviol68000_move_sr_a0c);
    h68k_SetPrivilegeViolationHandler(0x40e9, 0x40e9, pviol68000_move_sr_a1c,             pviol68000_move_sr_a1c);
    h68k_SetPrivilegeViolationHandler(0x40ea, 0x40ea, pviol68000_move_sr_a2c,             pviol68000_move_sr_a2c);
    h68k_SetPrivilegeViolationHandler(0x40eb, 0x40eb, pviol68000_move_sr_a3c,             pviol68000_move_sr_a3c);
    h68k_SetPrivilegeViolationHandler(0x40ec, 0x40ec, pviol68000_move_sr_a4c,             pviol68000_move_sr_a4c);
    h68k_SetPrivilegeViolationHandler(0x40ed, 0x40ed, pviol68000_move_sr_a5c,             pviol68000_move_sr_a5c);
    h68k_SetPrivilegeViolationHandler(0x40ee, 0x40ee, pviol68000_move_sr_a6c,             pviol68000_move_sr_a6c);
    h68k_SetPrivilegeViolationHandler(0x40ef, 0x40ef, pviol68000_move_sr_a7c,             pviol68000_move_sr_a7c);
    h68k_SetPrivilegeViolationHandler(0x40f0, 0x40f0, pviol68000_move_sr_a0d,             pviol68000_move_sr_a0d);
    h68k_SetPrivilegeViolationHandler(0x40f1, 0x40f1, pviol68000_move_sr_a1d,             pviol68000_move_sr_a1d);
    h68k_SetPrivilegeViolationHandler(0x40f2, 0x40f2, pviol68000_move_sr_a2d,             pviol68000_move_sr_a2d);
    h68k_SetPrivilegeViolationHandler(0x40f3, 0x40f3, pviol68000_move_sr_a3d,             pviol68000_move_sr_a3d);
    h68k_SetPrivilegeViolationHandler(0x40f4, 0x40f4, pviol68000_move_sr_a4d,             pviol68000_move_sr_a4d);
    h68k_SetPrivilegeViolationHandler(0x40f5, 0x40f5, pviol68000_move_sr_a5d,             pviol68000_move_sr_a5d);
    h68k_SetPrivilegeViolationHandler(0x40f6, 0x40f6, pviol68000_move_sr_a6d,             pviol68000_move_sr_a6d);
    h68k_SetPrivilegeViolationHandler(0x40f7, 0x40f7, pviol68000_move_sr_a7d,             pviol68000_move_sr_a7d);

    if (client_cpu >= 68010) {
    h68k_SetPrivilegeViolationHandler(0x4e73, 0x4e73, pviol68010_rte,                     pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40f8, 0x40f8, pviol68000_move_sr_absW,            pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40f9, 0x40f9, pviol68000_move_sr_absL,            pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40c0, 0x40c0, pviol68000_move_sr_d0,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40c1, 0x40c1, pviol68000_move_sr_d1,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40c2, 0x40c2, pviol68000_move_sr_d2,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40c3, 0x40c3, pviol68000_move_sr_d3,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40c4, 0x40c4, pviol68000_move_sr_d4,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40c5, 0x40c5, pviol68000_move_sr_d5,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40c6, 0x40c6, pviol68000_move_sr_d6,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40c7, 0x40c7, pviol68000_move_sr_d7,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40d0, 0x40d0, pviol68000_move_sr_a0,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40d1, 0x40d1, pviol68000_move_sr_a1,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40d2, 0x40d2, pviol68000_move_sr_a2,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40d3, 0x40d3, pviol68000_move_sr_a3,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40d4, 0x40d4, pviol68000_move_sr_a4,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40d5, 0x40d5, pviol68000_move_sr_a5,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40d6, 0x40d6, pviol68000_move_sr_a6,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40d7, 0x40d7, pviol68000_move_sr_a7,              pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40d8, 0x40d8, pviol68000_move_sr_a0a,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40d9, 0x40d9, pviol68000_move_sr_a1a,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40da, 0x40da, pviol68000_move_sr_a2a,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40db, 0x40db, pviol68000_move_sr_a3a,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40dc, 0x40dc, pviol68000_move_sr_a4a,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40dd, 0x40dd, pviol68000_move_sr_a5a,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40de, 0x40de, pviol68000_move_sr_a6a,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40df, 0x40df, pviol68000_move_sr_a7a,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40e0, 0x40e0, pviol68000_move_sr_a0b,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40e1, 0x40e1, pviol68000_move_sr_a1b,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40e2, 0x40e2, pviol68000_move_sr_a2b,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40e3, 0x40e3, pviol68000_move_sr_a3b,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40e4, 0x40e4, pviol68000_move_sr_a4b,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40e5, 0x40e5, pviol68000_move_sr_a5b,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40e6, 0x40e6, pviol68000_move_sr_a6b,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40e7, 0x40e7, pviol68000_move_sr_a7b,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40e8, 0x40e8, pviol68000_move_sr_a0c,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40e9, 0x40e9, pviol68000_move_sr_a1c,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40ea, 0x40ea, pviol68000_move_sr_a2c,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40eb, 0x40eb, pviol68000_move_sr_a3c,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40ec, 0x40ec, pviol68000_move_sr_a4c,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40ed, 0x40ed, pviol68000_move_sr_a5c,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40ee, 0x40ee, pviol68000_move_sr_a6c,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40ef, 0x40ef, pviol68000_move_sr_a7c,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40f0, 0x40f0, pviol68000_move_sr_a0d,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40f1, 0x40f1, pviol68000_move_sr_a1d,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40f2, 0x40f2, pviol68000_move_sr_a2d,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40f3, 0x40f3, pviol68000_move_sr_a3d,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40f4, 0x40f4, pviol68000_move_sr_a4d,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40f5, 0x40f5, pviol68000_move_sr_a5d,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40f6, 0x40f6, pviol68000_move_sr_a6d,             pviol68000_PrivilegeViolation);
    h68k_SetPrivilegeViolationHandler(0x40f7, 0x40f7, pviol68000_move_sr_a7d,             pviol68000_PrivilegeViolation);    
    }
    return true;
}


//-------------------------------------------------------
//
// Vector assignment
//
//-------------------------------------------------------
void h68k_SetVectorHandler(uint32 vec, void(*func)()) {
	*((uint32*)(host_vbr + vec)) = (uint32)func;
}
void h68k_SetVectorIpl(uint32 vec, uint32 ipl) {
    const uint32 idx = vec >> 2;
    ipl_table[idx] = (ipl << 24);
}
void h68k_SetVector(uint32 vec, uint32 ipl, void(*func)()) {
    const uint32 idx = vec >> 2;
    vec_table[idx] = (uint32)func;
    ipl_table[idx] = (ipl << 24);
    h68k_SetVectorHandler(vec, func);
}

//-------------------------------------------------------
//
// Privileged violation assignment
//
//-------------------------------------------------------
void h68k_SetPrivilegeViolationHandler(uint32 start, uint32 end, void(*fsuper)(), void(*fuser)()) {
    if (fsuper) {
        for (uint32 i = start; i <= end; i++) {
            uint16 idx = (i < 0x8000) ? (i | 0x8000) : (i & 0x7FFF);
            pviols_table[idx] = (uint32)fsuper;
        }
    }
    if (fuser) {
        for (uint32 i = start; i <= end; i++) {
            uint16 idx = (i < 0x8000) ? (i | 0x8000) : (i & 0x7FFF);
            pviolu_table[idx] = (uint32)fuser;
        }
    }
}
