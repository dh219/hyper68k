//----------------------------------------------------------------------------------
// common.c
//
// (c)2023 Anders Granlund
//----------------------------------------------------------------------------------
#include "common.h"
#include "stdlib.h"
#include "mint/osbind.h"
#include "setjmp.h"
#include "string.h"

//----------------------------------------------------------
// Atari startup
//----------------------------------------------------------
int _main_argc; char** _main_argv; char** _main_env;
jmp_buf _main_jmpbuf;

int supermain()
{
    int ret = setjmp(_main_jmpbuf);
    if (ret == 0) {
        #ifndef NDEBUG
        DbgInit(DBG_SCREEN);
        #endif
        ret = appmain(_main_argc, _main_argv);
    }
    return ret;
}

#define ssp_size ((1024*32)>>2)
uint32 s_new_ssp[ssp_size];
uint32 s_old_ssp;
uint32 s_old_usp;

void start_super() {
    __asm__ __volatile__ ( \
        " \
        move.l  %0,a0; \
        move.l  a0,d0; \
        subq.l  #4,d0; \
        and.w   #-16,d0; \
        move.l  d0,a0; \
        move.l  sp,-(a0); \
        move.l  usp, a1; \
        move.l  a1,-(a0); \
        move.l  a0, sp; \
        movem.l d1-d7/a2-a6,-(sp); \
        jsr     (%1); \
        movem.l (sp)+,d1-d7/a2-a6; \
        move.l  (sp)+,a0; \
        move.l  a0,usp; \
        move.l  (sp)+,sp; \
        " \
      : : "p"(&s_new_ssp[ssp_size-16]), "a"(&supermain) \
      : "d0", "a0", "a1", "cc" \
   );        
}

int main(int argc, char** argv, char** env) {
    _main_argc = argc; _main_argv = argv; _main_env = env;
	int ret = Supexec(&start_super);
	return ret;
}

void __main() {
}

void fatal(int arg) {
    longjmp(_main_jmpbuf, arg);
}



//----------------------------------------------------------
// Simple memory helpers
//----------------------------------------------------------
uint32 mem_ptr, mem_top;

// todo: maybe just use Mxalloc instead of fixed pool

uint32 InitMem(uint32 size) {
    uint32 mem_size = size;
    mem_ptr = Mxalloc(mem_size, 1);
    ASSERT(mem_ptr != 0, "Failed to allocate %d", mem_size);
    mem_top = mem_ptr + mem_size;
    memset((void*)mem_ptr, 0, mem_size);
    DPRINT(" Mem: 0x%08x : %dKb", (uint32)mem_ptr, (uint32)mem_top - (uint32)mem_ptr);
    return mem_size;
}

uint32 AllocMem(uint32 size, uint32 alignment) {
    uint32 a = (alignment - 1);
    uint32 m = (mem_ptr + a) & ~a;
    ASSERT((m+size)<=mem_top, "Failed alloc %d:%d", size, alignment);
    if ((m + size) <= mem_top) {
        DPRINT("alloc: %d, free: %d", ((m + size) - mem_ptr), (mem_top - (m + size)));
        mem_ptr = m + size;
        return m;
    }

    return 0;
}

void CopyMem(uint8* dst, uint8* src, uint32 cnt) {
    memcpy(dst, src, cnt);
}

void SetMem(uint8* dst, uint8 val, uint32 cnt) {
    memset(dst, val, cnt);
}

uint16* FindMem(uint8* mem, uint32 size, const uint16* pattern)
{
    uint16 s = pattern[0];
    uint16* r = (uint16*)mem;
    for (uint32 i=0; i<size/2; i++) {
        bool found = 1;
        for (uint16 j=0; j<s; j++) {
            if (r[i+j] != pattern[j+1]) {
                found = 0;
            }
        }
        if (found) {
            return &r[i];
        }
    }
    return 0;
}



//----------------------------------------------------------
// Debug
//----------------------------------------------------------
#ifndef NDEBUG
void(*DbgPrintFunc)(char*);
void(*DbgBreakFunc)(uint32);
char dbgBuffer[128];

uint32 nf_old_sp;
uint32 nf_old_int;
uint32 nf_id_print;
uint32 nf_id_break;
const char* nf_name_break = "NF_DEBUGGER";
const char* nf_name_print = "NF_STDERR";

void DbgPrintf(char* fmt, ...)
{
    if (DbgPrintFunc)
    {
        va_list args;
        va_start(args, fmt);
        vsprintf(dbgBuffer, fmt, args);
        va_end(args);
        DbgPrintFunc(dbgBuffer);
    }
}

void DbgPrintNF(char* str) {
    __asm__ __volatile__ (          \
        "                           \
        move.l  %0,-(sp);           \
        move.l  _nf_id_print,-(sp); \
        clr.l   -(sp);              \
        dc.w    0x7301;             \
        lea     12(sp),sp;          \
        "                           \
      : : "p"(str) : "cc" );
}

void DbgBreakNF(uint32 id) {
    __asm__ __volatile__ (          \
        "                           \
        move.l  _nf_id_break,-(sp); \
        move.l  %0,-(sp);           \
        dc.w    0x7301;             \
        addq.l  #8,sp;              \
        "                           \
      : : "d"(id) : "cc" );
}

void DbgPrintScreen(char* str) {
    printf(str);
}

void DbgBreakScreen(uint32 id) {
    __asm__ __volatile__ (              \
        "                               \
            move.w  0x8240.w,-(sp);     \
            cmp.l   #0x00FFFFFF,6(sp);  \
            bhi.b   0f;                 \
            move.w  #0xF0F,0x8240.w;    \
        0:  bra.b 0b;                   \
            move.w  (sp)+,0x8240.w;     \
        "                               \
      : : : "d0", "cc" );
}

void DbgPrintDummy(char* str) {
}

void DbgBreakDummy(uint32 id) {
}

void DbgInit(uint16 mode)
{
    // test for natfeats
    static bool firstTime = true;
    if (firstTime)
    {
        firstTime = false;
        nf_id_break = nf_id_print = 0;
        __asm__ __volatile__ ( \
            "   \
                movem.l d0-d7/a0-a6,-(sp);      \
                move.l  sp,_nf_old_sp;          \
                movec   vbr,a0;                 \
                move.l  0x10(a0),_nf_old_int;   \
                move.l  #1f,0x10(a0);            \
                \
                move.l  _nf_name_print,-(sp);   \
                pea     0;                      \
                dc.w    0x7300;                 \
                addq.l  #8,sp;                  \
                move.l  d0,_nf_id_print;        \
                \
                move.l  _nf_name_break,-(sp);   \
                pea     0;                      \
                dc.w    0x7300;                 \
                addq.l  #8,sp;                  \
                move.l  d0,_nf_id_break;        \
                \
            1:  move.l  _nf_old_sp,sp;          \
                movec   vbr,a0;                 \
                move.l  _nf_old_int,0x10(a0);   \
                movem.l (sp)+,d0-d7/a0-a6;      \
            "   \
            : : : "cc", "memory" ); 
    }

    // set print handler
    if (nf_id_print) {
        DbgPrintFunc = DbgPrintNF;
    } else {
        switch (mode) {
            case DBG_SCREEN:    DbgPrintFunc = DbgPrintScreen; break;
            default:            DbgPrintFunc = DbgPrintDummy; break;
        }
    }

    // set break handler
    if (nf_id_break) {
        DbgBreakFunc = DbgBreakNF;
    } else {
        switch (mode) {
            default:            DbgBreakFunc = DbgBreakScreen; break;            
        }
    }
}

bool DbgIsUsingNatFeats()
{
    return (DbgPrintFunc == DbgPrintNF) ? true : false;
}

#endif
