//--------------------------------------------------------------------
// Hyper68k : mmu.c
// Sets up default memorymapping and provides an API
// for the application to customize (userspace) mappings
//
// (c)2023 Anders Granlund
//--------------------------------------------------------------------

//--------------------------------------------------------------------
// We have different tables for supervisor vs. usermode
// This allows us to remap the client addresspace in whatever way we
// want without wreaking havoc or colliding with the hosts.
//
//      Supervisor table:
//          A standard Falcon/TT setup.
//          32bit range is transparently translated
//
//      Usermode table:
//          Sets up a virtual 24bit bus, ignoring the upper 8bits
//          TID tables are created for entire range
//
//  We're making it easy on ourselves and create tid tables for
//  all tic entries. if memory is a concern then there's potentially
//  quite a bit of memory to reclaim by allocating as needed
// 
//  pagesize     tid_size    x16     short   long
//   4096        256         4k      16k     32k
//   2048        512         8k      32k     64k
//   1024        1024        16k     64k     128k
//   512         2048        32k     128k    256k
//   256         4096        64k     256k    512k
//
//--------------------------------------------------------------------
//
// * Directly mapped to host memory
//      Valid long-format page descriptor
//
// * Directly mapped to host memory (write protected)
//      Valid long-format page descriptor with W bit set
//
// * Invalid address region
//      Valid long-format page descriptor with S bit set
//
// * Custom access handler
//     Invalid long-format page descriptor.
//     Userdata2 contains pointer to table of r/w callbacks
//
//--------------------------------------------------------------------
//
// todo:
//  - we may want to reuse any existing mmu table for supervisor
//    instead of creating our own. Even though we make a standard one.
//
//--------------------------------------------------------------------
#include "h68k.h"


typedef struct
{
    uint32  srp[2];
    uint32  crp[2];
    uint32  ttr0;
    uint32  ttr1;
    uint32  tc;
} MMURegs;

#define MMU_INVALID         0x00000000UL
#define MMU_PAGE            0x00000001UL
#define MMU_SHORT_TABLE     0x00000002UL
#define MMU_LONG_TABLE      0x00000003UL
#define MMU_WP              0x00000004UL
#define MMU_CI              0x00000040UL
#define MMU_S               0x00000100UL
#define MMU_DT              0x00000003UL

MMURegs h68k_mmu_old;
MMURegs h68k_mmu;
uint32* h68k_mmu_table;
uint16  h68k_mmu_pagesize;

uint32 h68k_GetMmuPageSize();
void h68k_PrepareMemoryMap();
void h68k_RestoreMemoryMap();
void h68k_GetMMU(MMURegs* regs);
void h68k_SetMMU(MMURegs* regs);

void ShortDescriptor(uint32* table, uint32 idx, uint32 addr, uint32 flag);
void LongDescriptor(uint32* table, uint32 idx, uint32 addr, uint32 flag);
void ShortInvalidDescriptor(uint32* table, uint32 idx, uint32 userdata);
void LongInvalidDescriptor(uint32* table, uint32 idx, uint32 userdata, uint32 userdata2);
void h68k_PrepareFtables();

void h68k_MapAddressRangeEx(uint32 start, uint32 end, uint32 dest, uint32 flag);
void h68k_MapAccessHandlerEx(uint32 start, uint32 end, uint32 userdata, h68kRWHandler readByte, h68kRWHandler writeByte,
    h68kRWHandler readWord, h68kRWHandler writeWord, h68kRWHandler readLong, h68kRWHandler writeLong,
    h68kRWHandler readThree, h68kRWHandler writeThree, h68kRWHandler readModifyWrite);


//--------------------------------------------------------------------
// Init and Restore
//--------------------------------------------------------------------
bool h68k_InitMemoryMap(uint32 pagesize)
{
    h68k_mmu_table = 0;
    h68k_mmu_pagesize = 0;

    // Backup existing mmu registers.
    // If SRP was never set, as is the case with the default TOS setup, then we will need to
    // put valid data there to avoid MMU exceptions trying to restore invalid settings.
    // (SRP will still be disabled, we are not changing the behavior)
    h68k_GetMMU(&h68k_mmu_old);
    if (h68k_mmu_old.srp[0] == 0) {
        h68k_mmu_old.srp[0] = 0x00000002;           // valid srp flags, still disabled though
        h68k_mmu_old.srp[1] = h68k_mmu_old.crp[1];  // valid address for good measure
    }

    // work out the table sizes based on the requested pagesize
    uint32 tid_bits = 0;
    const uint32 validPageSizes[] = { 256, 12, 512, 11, 1024, 10, 2048, 9, 4096, 8, 8192, 7, 16384, 6, 32768, 5, 0xFFFFFFFF, 0};
    for (int i=0; i<sizeof(validPageSizes)/sizeof(validPageSizes[0]); i+=2)
    {
        if (pagesize <= validPageSizes[i + 0]) {
            pagesize = validPageSizes[i + 1] ? validPageSizes[i + 0] : validPageSizes[i - 2];
            tid_bits = validPageSizes[i + 1] ? validPageSizes[i + 1] : validPageSizes[i - 1];
            break;
        }
    }
	DPRINT("Initializing MMU with pagesize %d", pagesize);
    ASSERT(tid_bits, "Invalid pagesize %d", pagesize);

	const uint32 tic_bits = 4;
	const uint32 tib_bits = 4;
	const uint32 tia_bits = 4;
	const uint32 is_bits  = 0;
	const uint32 ps_bits = 32 - is_bits - tia_bits - tib_bits - tic_bits - tid_bits;

	const uint32 tia_count = 1 + 1;
	const uint32 tib_count = 2 + 1;
	const uint32 tic_count = 1 + 1;
	const uint32 tid_count = 0 + 16;

    const uint32 tia_size = 4 * (1 << tia_bits);    // short descriptors
    const uint32 tib_size = 4 * (1 << tib_bits);    // short descriptors
    const uint32 tic_size = 4 * (1 << tic_bits);    // short descriptors
    const uint32 tid_size = 8 * (1 << tid_bits);    // long descriptors

    const uint32 size = (tia_size * tia_count) + (tib_size * tib_count) + (tic_size * tic_count) + (tid_size * tid_count);

	uint32* tia0s = (uint32*) AllocMem(size, 4096);
    uint32* tib0s = (uint32*) (tia_size + (uint32)tia0s);
    uint32* tib1s = (uint32*) (tib_size + (uint32)tib0s);
    uint32* tic0s = (uint32*) (tib_size + (uint32)tib1s);
    uint32* tia0u = (uint32*) (tic_size + (uint32)tic0s);
    uint32* tib0u = (uint32*) (tia_size + (uint32)tia0u);
    uint32* tic0u = (uint32*) (tib_size + (uint32)tib0u);
    uint32* tid0u = (uint32*) (tic_size + (uint32)tic0u);

    h68k_mmu_table      = tid0u;
    h68k_mmu_pagesize   = pagesize;

    // create supervisor table
    ShortDescriptor(tia0s,  0, (uint32)tib0s,MMU_SHORT_TABLE);
    ShortDescriptor(tia0s,  1, 0x10000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tia0s,  2, 0x20000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tia0s,  3, 0x30000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tia0s,  4, 0x40000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tia0s,  5, 0x50000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tia0s,  6, 0x60000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tia0s,  7, 0x70000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tia0s,  8, 0x80000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tia0s,  9, 0x90000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tia0s, 10, 0xA0000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tia0s, 11, 0xB0000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tia0s, 12, 0xC0000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tia0s, 13, 0xD0000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tia0s, 14, 0xE0000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tia0s, 15, (uint32)tib1s,MMU_SHORT_TABLE);

    ShortDescriptor(tib0s,  0, (uint32)tic0s,MMU_SHORT_TABLE);
    ShortDescriptor(tib0s,  1, 0x01000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib0s,  2, 0x02000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib0s,  3, 0x03000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib0s,  4, 0x04000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib0s,  5, 0x05000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib0s,  6, 0x06000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib0s,  7, 0x07000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib0s,  8, 0x08000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib0s,  9, 0x09000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib0s, 10, 0x0A000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib0s, 11, 0x0B000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib0s, 12, 0x0C000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib0s, 13, 0x0D000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib0s, 14, 0x0E000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib0s, 15, 0x0F000000,   MMU_PAGE | MMU_CI);

    ShortDescriptor(tib1s,  0, 0xF0000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib1s,  1, 0xF1000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib1s,  2, 0xF2000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib1s,  3, 0xF3000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib1s,  4, 0xF4000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib1s,  5, 0xF5000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib1s,  6, 0xF6000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib1s,  7, 0xF7000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib1s,  8, 0xF8000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib1s,  9, 0xF9000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib1s, 10, 0xFA000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib1s, 11, 0xFB000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib1s, 12, 0xFC000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib1s, 13, 0xFD000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib1s, 14, 0xFE000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tib1s, 15, (uint32)tic0s,MMU_SHORT_TABLE);

    ShortDescriptor(tic0s,  0, 0x00000000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tic0s,  1, 0x00100000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tic0s,  2, 0x00200000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tic0s,  3, 0x00300000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tic0s,  4, 0x00400000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tic0s,  5, 0x00500000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tic0s,  6, 0x00600000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tic0s,  7, 0x00700000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tic0s,  8, 0x00800000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tic0s,  9, 0x00900000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tic0s, 10, 0x00A00000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tic0s, 11, 0x00B00000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tic0s, 12, 0x00C00000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tic0s, 13, 0x00D00000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tic0s, 14, 0x00E00000,   MMU_PAGE | MMU_CI);
    ShortDescriptor(tic0s, 15, 0x00F00000,   MMU_PAGE | MMU_CI);

    // create usermode table
    for (int i=0; i<16; i++) {
        ShortDescriptor(tia0u, i, (uint32)tib0u, MMU_SHORT_TABLE);
    }
    for (int i=0; i<16; i++) {
        ShortDescriptor(tib0u, i, (uint32)tic0u, MMU_SHORT_TABLE);
    }
    for (int i=0; i<16; i++) {
        ShortDescriptor(tic0u, i, ((i * tid_size) + (uint32)tid0u), MMU_LONG_TABLE);
    }

    // default map entire client space to fatal error
    h68k_MapFatal(0x00000000, 0x01000000);

    // init MMU registers

    // transparently translate 32bit ranges in supervisor mode
    // (not strictly needed but avoids unnecessary table lookups)

    // TT0/1 : llllllll pppppppp a....bcd .eee.fff
    //  l = logical address base
    //  p = physical address base
    //  a = enable
    //  b = ci
    //  c = r/w
    //  d = rwm
    //  e = fc base
    //  f = fc mask
    
	h68k_mmu.ttr0 = 0x017E8573;	// 0x01000000-0x7FFFFFFF CI
	h68k_mmu.ttr1 = 0x807E8573;	// 0x08000000-0xFEFFFFFF CI
    // supervisor root
	h68k_mmu.srp[0] = 0x80000002;       // enabled
	h68k_mmu.srp[1] = (uint32)tia0s;    // rootpointer = tia0s
    // usermode root
	h68k_mmu.crp[0] = 0x80000002;       // enabled
	h68k_mmu.crp[1] = (uint32)tia0u;    // rootpointer = tia0u
    // and the main settings
	h68k_mmu.tc     =	(ps_bits  << 20) |
			            (is_bits  << 16) |
			            (tia_bits << 12) |
			            (tib_bits <<  8) |
			            (tic_bits <<  4) |
			            (tid_bits <<  0)
                        | 0x02000000            // using srp
			            | 0x80000000;           // enabled

	DPRINT(" tc    = %08x", h68k_mmu.tc);
	DPRINT(" crp   = %08x %08x", h68k_mmu.crp[0], h68k_mmu.crp[1]);
	DPRINT(" srp   = %08x %08x", h68k_mmu.srp[0], h68k_mmu.srp[1]);
	DPRINT(" ttr   = %08x %08x", h68k_mmu.ttr0, h68k_mmu.ttr1);
	DPRINT(" tia0u = %08x", (uint32)tia0u);
	DPRINT(" tib0u = %08x", (uint32)tib0u);
	DPRINT(" tic0u = %08x", (uint32)tic0u);
	DPRINT(" tid0u = %08x", (uint32)tid0u);
	return true;
}

//--------------------------------------------------------------------
void h68k_PrepareMemoryMap()
{
    h68k_PrepareFtables();
}

//--------------------------------------------------------------------
void h68k_RestoreMemoryMap()
{
    h68k_SetMMU(&h68k_mmu_old);
}

//--------------------------------------------------------------------
uint32 h68k_GetMmuPageSize()
{
    return h68k_mmu_pagesize;
}

//--------------------------------------------------------------------
uint32* h68k_GetMmuDescriptor(uint32 addr)
{
    uint32 idx = addr / h68k_mmu_pagesize;
    return &h68k_mmu_table[idx<<1];
}

//--------------------------------------------------------------------
void h68k_GetMMU(MMURegs* regs)
{
	__asm__ volatile (			        \
		"\n pflusha"			        \
		"\n nop"				        \
		"\n pmove	srp,0(%0)"	        \
		"\n pmove	crp,8(%0)"	        \
		"\n pmove	tt0,16(%0)"	        \
		"\n pmove	tt1,20(%0)"	        \
		"\n pmove	tc,24(%0)"	        \
		: : "a"(regs)			        \
		: "cc", "memory"                \
	);    
}

//--------------------------------------------------------------------
void h68k_SetMMU(MMURegs* regs)
{
	__asm__ volatile (			        \
		"\n nop"				        \
		"\n pflusha"			        \
		"\n nop"				        \
        /* disable tc,tt0,tt1        */ \
		"\n subq.l	#4,sp"				\
		"\n	pmove	tc,(sp)"			\
		"\n and.l	#0x7FFFFFFF,(sp)"   \
		"\n	pmove	(sp),tc"			\
		"\n	pmove	tt0,(sp)"			\
		"\n and.l	#0xFFFF7FFF,(sp)"   \
		"\n	pmove	(sp),tt0"			\
		"\n	pmove	tt1,(sp)"			\
		"\n and.l	#0xFFFF7FFF,(sp)"   \
		"\n	pmove	(sp),tt1"			\
		"\n addq.l	#4,sp"			    \
		"\n nop"				        \
		"\n pflusha"			        \
		"\n nop"				        \
        /* set all mmu registers     */ \
		"\n pmove	0(%0),srp"	        \
		"\n pmove	8(%0),crp"	        \
		"\n pmove	16(%0),tt0"	        \
		"\n pmove	20(%0),tt1"	        \
		"\n pmove	24(%0),tc"	        \
		"\n nop"				        \
		"\n pflusha"			        \
		"\n nop"				        \
		: : "a"(regs)			        \
		: "cc", "memory"                \
	);
}


//--------------------------------------------------------------------
// public helper functions
//--------------------------------------------------------------------

void h68k_MapMemory(uint32 start, uint32 end, uint32 dest) {
    h68k_MapAddressRangeEx(start, end, dest, MMU_PAGE | MMU_CI);
}

void h68k_MapReadOnly(uint32 start, uint32 end, uint32 dest) {
    h68k_MapAddressRangeEx(start, end, dest, MMU_PAGE | MMU_CI | MMU_WP);
}

void h68k_MapInvalid(uint32 start, uint32 end) {
    h68k_MapAddressRangeEx(start, end, start, MMU_PAGE | MMU_CI | MMU_S);
}

void h68k_MapFatal(uint32 start, uint32 end) {
    h68k_MapAccessHandlerEx(start, end, 0,
        h68k_mmuf_Fatal, h68k_mmuf_Fatal, h68k_mmuf_Fatal, h68k_mmuf_Fatal,
        h68k_mmuf_Fatal, h68k_mmuf_Fatal, h68k_mmuf_Fatal, h68k_mmuf_Fatal,
        h68k_mmuf_Fatal
    );
}

void h68k_MapPassThrough(uint32 start, uint32 end) {
    h68k_MapAccessHandlerEx(start, end, 0,
        h68k_mmuf_rb, h68k_mmuf_wb, h68k_mmuf_rw, h68k_mmuf_ww,
        h68k_mmuf_rl, h68k_mmuf_wl, h68k_mmuf_r3, h68k_mmuf_w3,
        h68k_mmuf_rm
    );
}

void h68k_MapPassThroughSafe(uint32 start, uint32 end) {
    h68k_MapAccessHandlerEx(start, end, 0,
        h68k_mmuf_rbs, h68k_mmuf_wbs, h68k_mmuf_rws, h68k_mmuf_wws,
        h68k_mmuf_rls, h68k_mmuf_wls, h68k_mmuf_r3s, h68k_mmuf_w3s,
        h68k_mmuf_rms
    );
}

void h68k_MapDisconnected(uint32 start, uint32 end) {
    h68k_MapIoRangeEx(start, end,
        h68k_IoReadByteFF, h68k_IoIgnoreByte,
        h68k_IoReadWordFF, h68k_IoIgnoreWord,
        h68k_IoReadLongFF, h68k_IoIgnoreLong);
}

//--------------------------------------------------------------------
// io callbacks
//--------------------------------------------------------------------

struct h68kFtable
{
    h68kIOFB readB;
    h68kIOFW readW;
    h68kIOFL readL;
    h68kIOFB writeB;
    h68kIOFW writeW;
    h68kIOFL writeL;
    uint32   reserved;
    uint32   len;
};

void h68k_MapIoRangeEx(uint32 start, uint32 end, h68kIOFB readByte, h68kIOFB writeByte, h68kIOFW readWord, h68kIOFW writeWord, h68kIOFL readLong, h68kIOFL writeLong)
{
    struct h68kFtable* ftable = (struct h68kFtable*)AllocMem(4*32, 256);
    ftable->readB = readByte;
    ftable->readW = readWord;
    ftable->readL = readLong;
    ftable->writeB = writeByte;
    ftable->writeW = writeWord;
    ftable->writeL = writeLong;

    h68k_MapAccessHandlerEx(start, end, (uint32)ftable,
        h68k_mmuf_rbc, h68k_mmuf_wbc, h68k_mmuf_rwc, h68k_mmuf_wwc,
        h68k_mmuf_rlc, h68k_mmuf_wlc, h68k_mmuf_r3c, h68k_mmuf_w3c, h68k_mmuf_rmc
    );
}


struct h68kFtable* h68k_GetExpandedFtable(uint32 addr)
{
    DPRINT("GetExpTable %08x", addr);

    uint32* atc = h68k_GetMmuDescriptor(addr);
    struct h68kFtable* oldftable = (struct h68kFtable*) atc[0];
    if (oldftable == 0)
        return 0;

    uint32 base = addr & ~(h68k_mmu_pagesize - 1);
    uint32 offs = addr - base;

    if (oldftable->len != 0)
        return &oldftable[offs];

    struct h68kFtable* newftable = (struct h68kFtable*) AllocMem(h68k_mmu_pagesize * sizeof(struct h68kFtable), 256);
    newftable[0].len = h68k_mmu_pagesize;
    newftable[0].reserved = oldftable->reserved;
    newftable[0].readB = (h68kIOFB)((uint32)oldftable->readB | 0x80000000);
    newftable[0].readW = (h68kIOFW)((uint32)oldftable->readW | 0x80000000);
    newftable[0].readL = (h68kIOFL)((uint32)oldftable->readL | 0x80000000);
    newftable[0].writeB = (h68kIOFB)((uint32)oldftable->writeB | 0x80000000);
    newftable[0].writeW = (h68kIOFW)((uint32)oldftable->writeW | 0x80000000);
    newftable[0].writeL = (h68kIOFL)((uint32)oldftable->writeL | 0x80000000);
    for (uint16 i=1; i<h68k_mmu_pagesize; i++) {
        CopyMem((uint8*)&newftable[i], (uint8*)&newftable[0], sizeof(struct h68kFtable));
    }

    atc[0] = (uint32)newftable;
    return &newftable[offs];
}


void h68k_PrepareFtables()
{
    DPRINT(" Prepare Ftables");
    for (uint32 base = 0; base < 0x01000000; base += h68k_mmu_pagesize)
    {
        uint32* atc = h68k_GetMmuDescriptor(base);
        struct h68kFtable* root = (struct h68kFtable*)atc[0];
        if ((atc[0] & 0xFF) != 0)
            continue;

        if ((root == 0) || (root->len == 0))
            continue;

        DPRINT(" %08x : %d", base, root->len);

        // change stage1 functions
        uint32* ud1 = (uint32*)atc[1];
        ud1[0] = (uint32)h68k_mmuf_wlcc;
        ud1[1] = (uint32)h68k_mmuf_wbcc;
        ud1[2] = (uint32)h68k_mmuf_wwcc;
        ud1[3] = (uint32)h68k_mmuf_w3cc;
        ud1[4] = (uint32)h68k_mmuf_rlcc;
        ud1[5] = (uint32)h68k_mmuf_rbcc;
        ud1[6] = (uint32)h68k_mmuf_rwcc;
        ud1[7] = (uint32)h68k_mmuf_r3cc;
        for (uint16 i=8; i<16; i++) {
            ud1[i] = (uint32)h68k_mmuf_rmcc;
        }

        // words
        for (uint16 idx = 0; idx < h68k_mmu_pagesize; idx+=2) {
            struct h68kFtable* e0 = &root[idx+0];
            struct h68kFtable* e1 = &root[idx+1];
            if ((uint32)e0->readW & 0x80000000)
                e0->readW = (e0->readB == e1->readB) ? (h68kIOFW)((uint32)e0->readW & 0x7FFFFFFF) : h68k_IoReadWordBB;
            if ((uint32)e0->writeW & 0x80000000) {
                e0->writeW = (e0->writeB == e1->writeB) ? (h68kIOFW)((uint32)e0->writeW & 0x7FFFFFFF) : h68k_IoWriteWordBB;
            }
        }

        // longs
        for (uint16 idx = 0; idx < h68k_mmu_pagesize; idx+=2) {
            struct h68kFtable* e0 = &root[idx+0];
            struct h68kFtable* e1 = &root[idx+2];
            if ((uint32)e0->readL & 0x80000000) {
                if (e0->readW == e1->readW)
                    e0->readL = (h68kIOFL)((uint32)e0->readL & 0x7FFFFFFF);
                else if ((e0->readW == h68k_IoReadWordBB) && (e1->readW == h68k_IoReadWordBB))
                    e0->readL = h68k_IoReadLongBBBB;
                else if (e0->readW == h68k_IoReadWordBB)
                    e0->readL = h68k_IoReadLongBBW;
                else if (e1->readW == h68k_IoReadWordBB)
                    e0->readL = h68k_IoReadLongWBB;
                else
                    e0->readL = h68k_IoReadLongWW;
            }
            if ((uint32)e0->writeL & 0x80000000) {
                if (e0->writeW == e1->writeW)
                    e0->writeL = (h68kIOFL)((uint32)e0->writeL & 0x7FFFFFFF);
                else if ((e0->writeW == h68k_IoWriteWordBB) && (e1->writeW == h68k_IoWriteWordBB))
                    e0->writeL = h68k_IoWriteLongBBBB;
                else if (e0->writeW == h68k_IoWriteWordBB)
                    e0->writeL = h68k_IoWriteLongBBW;
                else if (e1->writeW == h68k_IoWriteWordBB)
                    e0->writeL = h68k_IoWriteLongWBB;
                else
                    e0->writeL = h68k_IoWriteLongWW;
            }
        }

        // bytes
        for (uint16 idx = 0; idx < h68k_mmu_pagesize; idx++)
        {
            struct h68kFtable* e0 = &root[idx+0];
            if(idx & 1) {
                e0->readW = h68k_IoBerrWord;
                e0->readL = h68k_IoBerrLong;
                e0->writeW = h68k_IoBerrWord;
                e0->writeL = h68k_IoBerrLong;
            }
            if ((uint32)e0->readB & 0x80000000) {
                e0->readB = (h68kIOFB)((uint32)e0->readB & 0x7FFFFFFF);
            } else {
                DPRINT("       %02x : rb : %08x", idx, (uint32)e0->readB);
            }
            if ((uint32)e0->writeB & 0x80000000) {
                e0->writeB = (h68kIOFB)((uint32)e0->writeB & 0x7FFFFFFF);
            } else {
                DPRINT("       %02x : wb : %08x", idx, (uint32)e0->writeB);
            }
        }
    }
    DPRINT(" done.");
}


void h68k_MapIoRange(uint32 start, uint32 end, h68kIOFB readByte, h68kIOFB writeByte, h68kIOFW readWord, h68kIOFW writeWord) {
    h68k_MapIoRangeEx(start, end, readByte, writeByte, readWord, writeWord, h68k_IoReadLongAsWords, h68k_IoWriteLongAsWords);
}

void h68k_MapIoByte(uint32 addr, h68kIOFB readFunc, h68kIOFB writeFunc) {
    struct h68kFtable* ftable = h68k_GetExpandedFtable(addr);
    ASSERT(ftable, "h68k_MapIoByte %08x", addr);
    ftable->readB = readFunc;
    ftable->writeB = writeFunc;
}

void h68k_MapIoWord(uint32 addr, h68kIOFW readFunc, h68kIOFW writeFunc) {
    struct h68kFtable* ftable = h68k_GetExpandedFtable(addr);
    ASSERT(ftable, "h68k_MapIoWord %08x", addr);
    ftable->readW = readFunc;
    ftable->writeW = writeFunc;
}
void h68k_MapIoLong(uint32 addr, h68kIOFL readFunc, h68kIOFL writeFunc) {
    struct h68kFtable* ftable = h68k_GetExpandedFtable(addr);
    ASSERT(ftable, "h68k_MapIoLong %08x", addr);
    ftable->readL = readFunc;
    ftable->writeL = writeFunc;
}




//--------------------------------------------------------------------
// map client -> host memory
//--------------------------------------------------------------------
void h68k_MapAddressRangeEx(uint32 start, uint32 end, uint32 dest, uint32 flag)
{
    #ifndef NDEBUG
    {
        uint32 align = (h68k_mmu_pagesize - 1);
        ASSERT(!(start & align), "h68k_MapAdressRange: unaligned 0x%08x", start);
        ASSERT(!(end & align), "h68k_MapAdressRange: unaligned 0x%08x", end);
        ASSERT(!(dest & align), "h68k_MapAdressRange: unaligned 0x%08x", dest);
        start = start & ~align;
        dest = dest & ~align;
        end = (end + align) & ~align;
    }
    #endif
    flag |= MMU_PAGE;
    uint32 i = start / h68k_mmu_pagesize;
    DPRINT("Map: [%02x] 0x%08x-0x%08x -> 0x%08x", flag, start, end, dest);
    while (start < end) {
        LongDescriptor(h68k_mmu_table, i, dest, flag);
        i += 1; start += h68k_mmu_pagesize; dest += h68k_mmu_pagesize;
    }
}

//--------------------------------------------------------------------
// remap a page, safe to call when client is running
//--------------------------------------------------------------------
void h68k_RemapPage(uint32 laddr, uint32 paddr)
{
    uint32 i = laddr / h68k_mmu_pagesize;
    uint32* atc = &h68k_mmu_table[i<<1];
    if ((atc[0] & 3) != 0)
    {
        atc[1] = (atc[1] & 7) | (paddr & 0xFFFFFFF8);
        __asm__ volatile (			\
            "\n pflusha"			\
            "\n nop"			    \
            "\n move.l d0,-(sp)"    \
            "\n move.l #0x0808,d0"  \
            "\n movec d0,cacr"      \
            "\n move.l (sp)+,d0"    \
            "\n nop"                \
            : : : "cc", "memory"    \
        );      
    }
}

//--------------------------------------------------------------------
// map read/write access handlers
//--------------------------------------------------------------------
void h68k_MapAccessHandlerEx(
    uint32 start, uint32 end, uint32 userdata,
    h68kRWHandler readByte, h68kRWHandler writeByte,
    h68kRWHandler readWord, h68kRWHandler writeWord,
    h68kRWHandler readLong, h68kRWHandler writeLong,
    h68kRWHandler readThree, h68kRWHandler writeThree,
    h68kRWHandler readModifyWrite)
{
    uint32* mem = (uint32*)AllocMem(16*4, 4);
    SetMem((uint8*)mem, 0, 16 * 4);
    mem[0] = (uint32)writeLong;
    mem[1] = (uint32)writeByte;
    mem[2] = (uint32)writeWord;
    mem[3] = (uint32)writeThree;
    mem[4] = (uint32)readLong;
    mem[5] = (uint32)readByte;
    mem[6] = (uint32)readWord;
    mem[7] = (uint32)readThree;
    for (uint16 i=8; i<16; i++) {
        mem[i] = (uint32)readModifyWrite;
    }

    #ifndef NDEBUG
    {
        for (uint16 i=0; i<16; i++) {
            ASSERT(mem[i] != 0, "h68k_MapAccessHandlerEx %08x-%08x", start, end);
        }
        uint32 align = (h68k_mmu_pagesize - 1);
        ASSERT(!(start & align), "h68k_MapAccessHandler: unaligned 0x%08x", start);
        ASSERT(!(end & align), "h68k_MapAccessHandler: unaligned 0x%08x", end);
        ASSERT((userdata & 0xFF) == 0, "h68k_MapAccessHandler: unaligned 0x%08x", userdata);
        start = start & ~align;
        end = (end + align) & ~align;
    }
    #endif

    uint32 is = start / h68k_mmu_pagesize;
    uint32 ie = end / h68k_mmu_pagesize;

    DPRINT("Map: [%02x] 0x%08x-0x%08x", userdata, start, end);
    for (;is < ie; is++) {
        LongInvalidDescriptor(h68k_mmu_table, is, userdata, (uint32)mem);
    }
}

//--------------------------------------------------------------------
// mmmu table helpers
//--------------------------------------------------------------------
void ShortDescriptor(uint32* table, uint32 idx, uint32 addr, uint32 flag)
{
    #ifndef NDEBUG
    {
        uint32 dt = flag & MMU_DT;
        uint32 amask = (dt == MMU_PAGE) ? 0xFFFFFF00 : ((dt == MMU_SHORT_TABLE) || (dt == MMU_LONG_TABLE)) ? 0xFFFFFFF0 : 0xFFFFFFFC;
        uint32 fmask = ~amask;
        ASSERT(((addr & ~amask) == 0), "ShortDesc: %d : $%08x : $%08x", idx, addr, flag);
        ASSERT(((flag & ~fmask) == 0), "ShortDesc: %d : $%08x : $%08x", idx, addr, flag);
    }
    #endif
    table[idx] = addr | flag;
}

void ShortInvalidDescriptor(uint32* table, uint32 idx, uint32 userdata)
{
    ASSERT((0 == (userdata & 3)), "ShortInvalidDesc: %d : $%08x", idx, userdata);
    table[idx] = userdata;
}

void LongDescriptor(uint32* table, uint32 idx, uint32 addr, uint32 flag)
{
    #ifndef DEBUG
    {
        uint32 dt = flag & MMU_DT;
        uint32 amask = (dt == MMU_PAGE) ? 0xFFFFFF00 : ((dt == MMU_SHORT_TABLE) || (dt == MMU_LONG_TABLE)) ? 0xFFFFFFF0 : 0xFFFFFFFC;
        uint32 fmask = 0x0000FFFF;
        ASSERT(((addr & ~amask) == 0), "LongDesc: %d : $%08x : $%08x", idx, addr, flag);
        ASSERT(((flag & ~fmask) == 0), "LongDesc: %d : $%08x : $%08x", idx, addr, flag);
    }
    #endif    
    table[(idx<<1) + 0] = 0x0000FC00 | flag;
    table[(idx<<1) + 1] = addr;
}

void LongInvalidDescriptor(uint32* table, uint32 idx, uint32 userdata, uint32 userdata2)
{
    ASSERT((0 == (userdata & 3)), "LongInvalidDesc: %d : $%08x : $%08x", idx, userdata, userdata2);
    table[(idx<<1) + 0] = userdata;
    table[(idx<<1) + 1] = userdata2;
}



