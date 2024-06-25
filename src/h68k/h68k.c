//--------------------------------------------------------------------
// Hyper68k : h68k.c
// 68030 Hypervisor for 68000/68010 virtual machines
//
// (c)2023 Anders Granlund
//--------------------------------------------------------------------
#include "h68k.h"
#include "setjmp.h"

extern bool h68k_InitVectors();
extern bool h68k_InitMemoryMap();
extern void h68k_PrepareMemoryMap();
extern void h68k_RestoreMemoryMap();
extern void h68k_FatalError(struct h68kFatalDump* dump);
struct h68kFatalDump h68kFatalDump;
char h68kFatalDumpMsg[1024];

// callbacks
void(*h68k_OnResetCpu)();
void(*h68k_OnResetDevices)();
void(*h68k_OnFatal)();

// host registers
uint16 host_cpu;
uint32 host_ssp;
uint8* host_vbr;
uint32 host_cacr;

// client registers
uint16 client_cpu;
uint16 client_sr;
uint32 client_ssp;
uint32 client_usp;
uint32 client_vbr;  // 68010+
uint32 client_sfc;  // 68010+
uint32 client_dfc;  // 68010+

// backed up host control registers
uint32 old_usp;
uint32 old_vbr;
uint32 old_sfc;
uint32 old_dfc;
uint32 old_cacr;
uint32 old_caar;


//--------------------------------------------------------------------
//
// Initialize hypervisor
//
//--------------------------------------------------------------------
bool h68k_Init()
{
    h68kFatalDump.err = 0;
    h68kFatalDumpMsg[0] = 0;

    client_cpu  = H68K_CPU_68000;
    host_cpu    = H68K_CPU_68030;
    host_ssp    = 0;
    host_vbr    = 0;
    host_cacr   = 0x0000;

    h68k_OnResetCpu = 0;
    h68k_OnResetDevices = 0;
    h68k_OnFatal = 0;

    // create host stack
    if (host_ssp == 0) {
        const uint32 stacksize = 64 * 1024;
        host_ssp = AllocMem(stacksize, 4);
        SetMem((uint8*)host_ssp, 0, stacksize);
        host_ssp = host_ssp + stacksize - 4;
    }

    // init default memorymap
    if (!h68k_InitMemoryMap(256))
        return false;

    // init default vectors
    if (!h68k_InitVectors())
        return false;

    return true;
}


//--------------------------------------------------------------------
//
// Launch virtual machine
//
//--------------------------------------------------------------------
jmp_buf h68k_terminate_jmpbuf;
#define disableirq()    { __asm__ volatile (" move.w #0x2700,sr\n" : : : "cc"); }
#define savecreg(reg)   { __asm__ volatile (" movec " #reg ",d0\n move.l d0,_old_" #reg "\n" : : : "d0", "cc" ); }
#define loadcreg(reg)   { __asm__ volatile (" move.l _old_" #reg ",d0\n movec d0," #reg "\n" : : : "d0", "cc" ); }

void h68k_Run()
{

    // disable all interrupts
    // this has been moved to the top as TOS4 uses shadow registers during interrupts that rely on the MMU being configured
    disableirq();

    // prepare memory map before start
    h68k_PrepareMemoryMap();

    // save host control registers
    savecreg(usp);
    savecreg(vbr);
    savecreg(sfc);
    savecreg(dfc);
    savecreg(caar);
    savecreg(cacr);

    // launch virtual machine
    if (setjmp(h68k_terminate_jmpbuf) == 0)
    {
        // client coldboot regs
        client_sr   = 0x2000;
        client_ssp  = 0;
        client_usp  = 0;
        client_vbr  = 0;
        client_sfc  = 1;
        client_dfc  = 1;

        // reset, and start client
        h68kFatalDump.err = 0;
        __asm__ volatile ( \
            " jmp _vec68000_Reset\n" \
            : : : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "cc", "memory" );
    }
    
    // disable all interrupts
    disableirq();

    // restore host control registers
    loadcreg(vbr);
    loadcreg(usp);
    loadcreg(sfc);
    loadcreg(dfc);
    old_cacr |= 0x0808;    // clear data + instruction caches
    loadcreg(caar);
    loadcreg(cacr);

    // restore mmu
    h68k_RestoreMemoryMap();
}

//--------------------------------------------------------------------
//
// Terminate running vm
//
//--------------------------------------------------------------------
void h68k_Terminate()
{
    longjmp(h68k_terminate_jmpbuf, 1);
}


//--------------------------------------------------------------------
//
// Fatal error handler
//
//--------------------------------------------------------------------
void h68k_FatalError(struct h68kFatalDump* dump)
{
    DCOLOR(0xF00);
    CopyMem((uint8*)&h68kFatalDump, (uint8*)dump, sizeof(struct h68kFatalDump));
    dump = &h68kFatalDump;
    if (dump->err == 0) {
        dump->err = 0xffffffff;
    }
#ifndef NDEBUG
    sprintf(h68kFatalDumpMsg, "Fatal error: $%08x at $%08x", dump->err, dump->pc);
    h68k_debugPrint(h68kFatalDumpMsg);
#endif
#if H68K_DEBUGTRACE
    h68k_debugPrintTrace();
#endif    

    if (h68k_OnFatal) {
        h68k_OnFatal(dump);
    }

    BREAK(dump->err);
    h68k_Terminate();
}

char* h68k_GetLastError() {
    return ((h68kFatalDump.err != 0) && (h68kFatalDumpMsg[0] != 0)) ? h68kFatalDumpMsg : 0;
}

//--------------------------------------------------------------------
//
// Set callback for when CPU was reset
//
//--------------------------------------------------------------------
void h68k_SetCpuResetCallback(void(*func)())
{
    h68k_OnResetCpu = func;
}

//--------------------------------------------------------------------
//
// Set callback for when a reset instruction is executed
//
//--------------------------------------------------------------------
void h68k_SetDeviceResetCallback(void(*func)())
{
    h68k_OnResetDevices = func;
}

//--------------------------------------------------------------------
//
// Set callback for when something terrible has happened
//
//--------------------------------------------------------------------
void h68k_SetFatalCallback(void(*func)(struct h68kFatalDump*))
{
    h68k_OnFatal = func;
}



//--------------------------------------------------------------------
//
// Debugging
//
//--------------------------------------------------------------------
#if H68K_DEBUGTRACE
uint32 traceBuffer[H68K_DEBUGTRACE];
uint16 traceIndex;
void h68k_debugTrace(uint32 pc)
{
    traceIndex = (traceIndex + 1) & (H68K_DEBUGTRACE - 1);
    traceBuffer[traceIndex] = pc;
}
#endif

#if H68K_DEBUGPRINT
char debugBuffer[128];
void h68k_debugOutSerial(char* str)
{
    #define wait() while ((*((volatile uint8*)0xfffa2d) & 0x80) == 0) { }
    #define out(x) wait(); *((volatile char*)0xfffa2f) = x;

    uint8 regs[4];
    regs[0] = *((volatile uint8*)0xfffa2d); regs[1] = *((volatile uint8*)0xfffa1d);
    regs[2] = *((volatile uint8*)0xfffa25); regs[3] = *((volatile uint8*)0xfffa29);

    *((volatile uint8*)0xfffa2d) &= ~0x01;
    *((volatile uint8*)0xFFFA1D) &= 0x70;
    *((volatile uint8*)0xFFFA29) = 0 | 0 | 0x88;
    *((volatile uint8*)0xFFFA25) = 4;   // baud
    *((volatile uint8*)0xFFFA1D) |= 0x01;
    *((volatile uint8*)0xfffa2d) |= 0x01;

    out('\n');
    while (str && *str) {
        out(*str); str++;
    } out(0); out(0); wait();

    *((volatile uint8*)0xfffa2d) &= ~0x01;
    *((volatile uint8*)0xFFFA1D) &= 0x70;
    *((volatile uint8*)0xfffa25) = regs[2];
    *((volatile uint8*)0xfffa29) = regs[3];
    *((volatile uint8*)0xfffa1d) = regs[0];
    *((volatile uint8*)0xfffa2d) = regs[1];    
}

void h68k_debugPrintValue(uint32 ident, uint32 value)
{
    h68k_debugPrint("%08x : %08x", ident, value);
}

#if H68K_DEBUGTRACE
void h68k_debugPrintTrace()
{
    uint16 idx = traceIndex & (H68K_DEBUGTRACE-1);
    h68k_debugPrint("Trace:");
    for (int i=0; i<H68K_DEBUGTRACE; i++)
    {
        h68k_debugPrint("  $%08x",traceBuffer[idx]);
        idx = (idx - 1) & (H68K_DEBUGTRACE - 1);
    }
}
#endif // H68K_DEBUGTRACE
#endif // H68K_DEBUGPRINT

