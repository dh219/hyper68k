//----------------------------------------------------------------------------------
// common.h
//
// (c)2023 Anders Granlund
//----------------------------------------------------------------------------------
#ifndef _COMMON_H_
#define _COMMON_H_

#ifndef sint8
typedef signed char		sint8;
#endif
#ifndef sint16
typedef signed short	sint16;
#endif
#ifndef sint32
typedef signed int		sint32;
#endif
#ifndef uint8
typedef unsigned char	uint8;
#endif
#ifndef uint16
typedef unsigned short	uint16;
#endif
#ifndef uint32
typedef unsigned int	uint32;
#endif
#ifndef bool
typedef short           bool;
#endif
#ifndef true
#define true            1
#endif
#ifndef false
#define false           0
#endif

#define DBG_NONE        0
#define DBG_SCREEN      1
#define DBG_SERIAL      2

#ifndef NDEBUG
    #include "stdio.h"

    extern void(*DbgPrintFunc)(char* str);
    extern void(*DbgBreakFunc)(uint32 id);
    extern void DbgPrintf(char* fmt, ...);
    extern bool DbgIsUsingNatFeats();
    extern void DbgInit(uint16 mode);

    #define DCOLOR(_x)      { for (int i=0; i<100000; i++) { *((volatile uint16*)0xff8240) = _x; } }
    #define DPRINT(...)     { DbgPrintf(__VA_ARGS__); DbgPrintf("\r\n"); }
    #define ASSERT(x, ...)  { if(!(x)) { DbgPrintf("assert("); DbgPrintf(#x); DbgPrintf(") :\r\n "); DbgPrintf(__VA_ARGS__); DbgPrintf("\r\n"); fatal(-1); } }
    #define BREAK(_x)      { DbgBreakFunc(_x); }
#else
    #define DCOLOR(x)
    #define DPRINT(...)
    #define ASSERT(x, ...)  { if(!(x)) { fatal(-1); } }
    #define BREAK(...)

    static inline bool DbgIsUsingNatFeats() { return false; }
    static inline void DbgInit(uint16 mode) { }
#endif

extern int appmain(int args, char** argv);
extern void fatal(int arg);

extern uint32 InitMem(uint32 size);
extern uint32 AllocMem(uint32 size, uint32 alignment);
extern void CopyMem(uint8* dst, uint8* src, uint32 cnt);
extern void SetMem(uint8* dst, uint8 val, uint32 cnt);
extern uint16* FindMem(uint8* mem, uint32 size, const uint16* pattern);


#endif // _COMMON_H_

