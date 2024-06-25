//----------------------------------------------------------------------------------
// Atari ST emulator for Atari 68030
// Example project for Hyper68k
//
// (c)2023 Anders Granlund
//----------------------------------------------------------------------------------
#include "common.h"
#include "h68k/h68k.h"
#include "stdio.h"
#include "string.h"

//----------------------------------------------------------------------------------
uint32 cart_data;
uint32 cart_size;

uint32 rom_data;
uint32 rom_size;
uint32 rom_addr;
uint16 rom_ver;

uint32 ram_data;
uint32 ram_size;
uint8  ram_offs;  // high byte offset

uint32 zero_data;
uint32 zero_size;

#define RAM_TEST 0

//----------------------------------------------------------------------------------
bool InitRam(uint32 kb);
bool InitCart(const char* filename);
bool InitRom(const char* filename);
void PatchTos1(uint8* rom, uint32 size);
void PatchTos2(uint8* rom, uint32 size);

void OnResetCpu();
void OnResetDevices();
void OnFatal(struct h68kFatalDump* dump);

#define RW_OK   0
#define RW_FAIL ~0

uint8 reg_stmmu;


//----------------------------------------------------------------------------------
// st-mmu
//----------------------------------------------------------------------------------
void rb_mmuconf(uint32 addr, uint8* out) {
    *out = reg_stmmu;    
}

void wb_mmuconf(uint32 addr, uint8* in) {
    reg_stmmu = *in;
    uint8 bank_conf = (reg_stmmu >> 2);
    uint32 pagesize = h68k_GetMmuPageSize();
    for (uint32 laddr = 0; laddr < ram_size; laddr += pagesize)
    {
        uint32 paddr;
        switch (bank_conf) {
            case 0: paddr = ((laddr & 0x03fe00)<<1) | (laddr & 0x0003ff); break;
            case 1: paddr = laddr; break;
            default: paddr = ((laddr & 0x0ff800)>>1) | (laddr & 0x0003ff); break;
        }
        paddr += (paddr < zero_size) ? zero_data : ram_data;
        h68k_RemapPage(laddr, paddr);
    }
}

// mega rtc -- silently ignore writes and return FFs
void rb_rtc(uint32 addr, uint8* out) {
    *out = 0xff;    
}

void wb_rtc(uint32 addr, uint8* in) {
	;
}


//----------------------------------------------------------------------------------
// ram address high byte translation (floppy dma / shifter)
//----------------------------------------------------------------------------------
void rb_addrH(uint32 addr, uint8* data) {
    uint8 d = *((volatile uint32*)addr);
    d = (d >= 0x40) ? d : d - ram_offs;
    *data = d;
}

void wb_addrH(uint32 addr, uint8* data) {
    uint8 d = *data;
    d = (d >= 0x40) ? d : d + ram_offs;
    *((uint8*)addr) = d;
}


//----------------------------------------------------------------------------------
//
// Atari ST emulator for 68030 based Atari host
//
//----------------------------------------------------------------------------------
int appmain(int args, char** argv)
{
    DPRINT("Hyper68");
    client_cpu = H68K_CPU_68000;
    host_cpu = H68K_CPU_68030;

    char* fname_rom = "tos.rom";
    char* fname_cart = "cart.stc";
    //char* fname_floppy = 0;

    // drag-and-dropped something onto us?
    if (args == 2 && argv[1] != 0 && *argv[1] != 0) {
        char* fname = argv[1];
        int len = strlen(fname);
        if (len > 0) {
            for (int i=len-1; i>0; i--) {
                if (fname[i] == '.') {
                    // was it a rom, cartridge or floppy image?
                    if (stricmp(&fname[i+1], "stc") == 0) {
                        fname_cart = fname;
                    }
                    else if (stricmp(&fname[i+1], "img") == 0) {
                        fname_rom = fname;
                    }
                }
            }
        }
    }

    //
    // todo: detect cpu, machine type, memory and so on
    //

    InitMem(2 * 1024 * 1024);

    // Init hypervisor and setup callbacks
    h68k_Init();
    h68k_SetCpuResetCallback(OnResetCpu);
    h68k_SetDeviceResetCallback(OnResetDevices);
    h68k_SetFatalCallback(OnFatal);

    // Default entire memory map as passthrough with bus-error detect
    // todo: use fast passthrough and set up relevant addresses as berr triggers
    h68k_MapPassThroughSafe(0x00000000, 0x01000000);

    // Setup ROMs and RAM
    InitCart(fname_cart);
    InitRom(fname_rom);
    InitRam(512);

    // Setup IO
    h68k_MapInvalid(0x400000, 0xE00000);    // altram

//    h68k_MapPassThroughSafe(0x00FF8000, 0x01000000);
    h68k_MapPassThrough(0x00FF8000, 0x01000000);
    h68k_MapInvalid(0xF00000, 0xFA0000);    // reserved io space
    h68k_MapInvalid(0xFF0000, 0xFF8000);    // reserved io space

    h68k_MapInvalid(0xF00000, 0xF00100);    // ide
    h68k_MapInvalid(0xFF8700, 0xFF8800);    // tt scsi
    h68k_MapInvalid(0xFF8900, 0xFF8A00);    // dma sound
    h68k_MapInvalid(0xFF8A00, 0xFF8B00);    // blitter
    h68k_MapInvalid(0xFF8C00, 0xFF8F00);    // TT/MSTe
    h68k_MapInvalid(0xFF9200, 0xFF9300);    // extended joyport

    h68k_MapInvalid(0xFF9800, 0xFF9900);    // falcon palette
    h68k_MapInvalid(0xFFA200, 0xFFA300);    // falcon dsp

    // fffa00-fffa3f ; ST mfp
    // fffa40-fffa5c : MSTe FPU (berr)
    // fffa81-fffaaf : TT mfp   (berr)
#if 1
    //h68k_MapIoRangeEx(0xfffa00, 0xfffb00, h68k_IoBerrByte, h68k_IoBerrByte, h68k_IoBerrWord, h68k_IoBerrWord, h68k_IoBerrLong, h68k_IoBerrLong);
    //h68k_MapIoRangeEx(0xfffa00, 0xfffb00, h68k_IoIgnoreByte, h68k_IoIgnoreByte, h68k_IoIgnoreWord, h68k_IoIgnoreWord, h68k_IoIgnoreLong, h68k_IoIgnoreLong);
    h68k_MapIoRangeEx(0xfffa00, 0xfffb00, h68k_IoReadBytePT, h68k_IoWriteBytePT, h68k_IoReadWordPT, h68k_IoWriteWordPT, h68k_IoReadLongPT, h68k_IoWriteLongPT);
    for (uint32 i=0xfffa00; i<0xfffa40; i+=2) {
        h68k_MapIoByte(i, h68k_IoIgnoreByte, h68k_IoIgnoreByte);
        h68k_MapIoWord(i, h68k_IoReadWordPT, h68k_IoWriteWordPT);
        h68k_MapIoLong(i, h68k_IoBerrLong, h68k_IoBerrLong);
    }
    for (uint32 i=0xfffa01; i<0xfffa40; i+=2) {
        h68k_MapIoByte(i, h68k_IoReadBytePT, h68k_IoWriteBytePT);
        h68k_MapIoWord(i, h68k_IoBerrWord, h68k_IoBerrWord);
        h68k_MapIoLong(i, h68k_IoBerrLong, h68k_IoBerrLong);
    }
#else
    h68k_MapPassThroughSafe(0xfffa00, 0xfffb00);
#endif

    //h68k_MapIoRangeEx(0xff8000, 0xff8100, h68k_IoBerrByte, h68k_IoBerrByte, h68k_IoBerrWord, h68k_IoBerrWord, h68k_IoBerrLong, h68k_IoBerrLong);
    h68k_MapIoRangeEx(0xff8000, 0xff8100, h68k_IoReadBytePT, h68k_IoWriteBytePT, h68k_IoReadWordPT, h68k_IoWriteWordPT, h68k_IoReadLongPT, h68k_IoWriteLongPT);
    h68k_MapIoByte(0xff8001, rb_mmuconf, wb_mmuconf);       // emulated memory config

    // set up register intercepts for when emulated ram isn't sharing same address as real ram
    if (ram_data != 0)
    {
        ASSERT((ram_data & 0xFFFF) == 0, "Emulated RAM unaligned");

        // dma
        h68k_MapIoRangeEx(0xff8600, 0xff8700, h68k_IoReadBytePT, h68k_IoWriteBytePT, h68k_IoReadWordPT, h68k_IoWriteWordPT, h68k_IoReadLongPT, h68k_IoWriteLongPT);
        h68k_MapIoByte(0xff8609, rb_addrH, wb_addrH);   // DMA address

        // shifter
        h68k_MapIoRangeEx(0xff8200, 0xff8300, h68k_IoReadBytePT, h68k_IoWriteBytePT, h68k_IoReadWordPT, h68k_IoWriteWordPT, h68k_IoReadLongPT, h68k_IoWriteLongPT);
        h68k_MapIoByte(0xff8201, rb_addrH, wb_addrH);   // screen position
        h68k_MapIoByte(0xff8205, rb_addrH, wb_addrH);   // video address pointer
    }
    // RTC
    h68k_MapIoRangeEx(0xfffc00, 0xfffd00, h68k_IoReadBytePT, h68k_IoWriteBytePT, h68k_IoReadWordPT, h68k_IoWriteWordPT, h68k_IoReadLongPT, h68k_IoWriteLongPT);
    h68k_MapIoByte(0xfffc3b, rb_rtc, wb_rtc);       // emulated rtc conf
    h68k_MapIoByte(0xfffc25, rb_rtc, wb_rtc);       // emulated rtc conf
    h68k_MapIoByte(0xfffc27, rb_rtc, wb_rtc);       // emulated rtc conf

    for (uint32 i=0x00; i<0x60; i+=4) {
        h68k_SetVectorIpl(i, 7);
    }

    // MFP interrupt levels
    for (uint32 i=0x100; i<0x400; i+=4) {
        h68k_SetVectorIpl(i, 6);
    }

#if 0
    // reset vector
    *((uint32*)0x426) = (uint32) 0x31415926;
    *((uint32*)0x42a) = (uint32) vec68000_Reset;
#endif

#if 0
    // hard reset host
    *((uint32*)0x420) = 0;  // memvalid
    *((uint32*)0x43a) = 0;  // memval2
    *((uint32*)0x51a) = 0;  // memval3
    *((uint32*)0x5a8) = 0;  // ramvalid
    *((uint32*)0x426) = 0;  // resvalid
#endif    

    // todo: hard reset behavior and host machine specific init
    reg_stmmu = 0xA;

/*   
    *((volatile uint8*)0xfffa01) = 0;       // lpt : data register
    *((volatile uint8*)0xfffa03) = 0;       // lpt : active edge
    *((volatile uint8*)0xfffa05) = 0;       // lpt : direction
*/
    *((volatile uint8*)0xfffa07) = 0;       // interrupt enable
    *((volatile uint8*)0xfffa09) = 0;
    *((volatile uint8*)0xfffa0b) = 0;       // interrupt pending
    *((volatile uint8*)0xfffa0d) = 0;
    *((volatile uint8*)0xfffa0f) = 0;       // interrupt in-service
    *((volatile uint8*)0xfffa11) = 0;
    *((volatile uint8*)0xfffa13) = 0;       // interrupt mask
    *((volatile uint8*)0xfffa15) = 0;
    *((volatile uint8*)0xfffa17) = 0x48;    // vector base + software end of interrupt

    *((volatile uint8*)0xfffa19) = 0;       // timerA stop
    *((volatile uint8*)0xfffa1b) = 0;       // timerB stop
    *((volatile uint8*)0xfffa1d) = 0;       // timerC+D stop
    *((volatile uint8*)0xfffa1f) = 0;       // timerA data
    *((volatile uint8*)0xfffa21) = 0;       // timerB data
    *((volatile uint8*)0xfffa23) = 0;       // timerC data
    *((volatile uint8*)0xfffa25) = 0;       // timerD data

    *((volatile uint8*)0xfffa27) = 0;       // usart : sync character
    *((volatile uint8*)0xfffa29) = 0;       // usart : control
    *((volatile uint8*)0xfffa2b) = 0;       // usart : rx status
    *((volatile uint8*)0xfffa2d) = 0;       // usart : tx status
    *((volatile uint8*)0xfffa2f) = 0;       // usart : data

    DbgInit(DBG_NONE);

    // Back in time we go!
    h68k_Run();

    // todo: host machine specific exit

    if (h68k_GetLastError()) {
        DPRINT(h68k_GetLastError());
    }

    return 0;
}


//----------------------------------------------------------------------------------
//
// Callbacks from h68k
//
//----------------------------------------------------------------------------------

void OnResetCpu()
{
    DPRINT("OnResetCpu");
/*    
    static uint16 counter = 0;
    if (counter++ > 0) {
        h68k_Terminate();
    }
*/    
}

void OnResetDevices()
{
    DPRINT("OnResetDevices");
}

void OnFatal(struct h68kFatalDump* dump)
{
    DPRINT("OnFatal");
}


//----------------------------------------------------------------------------------
//
// RAM Init
//
//----------------------------------------------------------------------------------
bool InitRam(uint32 kb)
{
    const uint32 ram_addr = 0;
    ram_data = ram_addr;
    ram_size = kb * 1024;
    ram_offs = (ram_data >> 16) & 0xFF;

    // put the emulated zeropage somewhere fast
    if (h68k_GetMmuPageSize() <= 2048) {
        zero_size = 2048;
        zero_data = AllocMem(zero_size, 4096);
    } else {
        zero_size = 2048;
        zero_data = ram_data;
    }

    // first 8 bytes of memory mirrors rom
    ASSERT(zero_data && zero_size, "Zeropage init fail");
    SetMem((uint8*)zero_data, 0, zero_size);
    CopyMem((uint8*)zero_data, (uint8*)rom_data, 8);

    // memorymap
    h68k_MapMemory(ram_addr, ram_addr + ram_size, ram_data);
    h68k_MapMemory(ram_addr, ram_addr + zero_size, zero_data);
    //h68k_MapDisconnected(ram_addr + ram_size, 0x00400000);

    //h68k_MapDisconnected(ram_addr + ram_size, 0x00400000);
    h68k_MapIoRangeEx(ram_addr + ram_size, 0x00400000, h68k_IoIgnoreByte, h68k_IoIgnoreByte, h68k_IoReadWordBB, h68k_IoReadWordBB, h68k_IoReadLongBBBB, h68k_IoReadLongBBBB);


    return true;
}


//----------------------------------------------------------------------------------
//
// CART Init
//
//----------------------------------------------------------------------------------
bool InitCart(const char* filename)
{
    const uint32 cart_addr = 0x00FA0000;
    cart_data = cart_addr;
    cart_size = 128 * 1024;

    if (filename && *filename)
    {
        FILE* f = fopen(filename, "r");
        if (f)
        {
            DPRINT("Loading '%s'", filename);

            fseek(f, 0, SEEK_END);
            uint32 filesize = ftell(f);
            uint32 headersize = 4;
            filesize -= headersize;
            fseek(f, headersize, SEEK_SET);

            filesize = filesize > cart_size ? cart_size : filesize;
            cart_data = AllocMem(cart_size, 4096);
            SetMem((uint8*)cart_data, 0xFF, cart_size);
            fread((uint8*)cart_data, 1, filesize, f);
            fclose(f);

            h68k_MapReadOnly(cart_addr, cart_addr + cart_size, cart_data);
            return true;
        }
    }

    h68k_MapDisconnected(cart_addr, cart_addr + cart_size);
    if (strcmp((char*)(cart_addr+0x18), "HATARI.TOS") == 0)
    {
        DPRINT("Disabled Hatari cartridge");
        return true;
    }

    //h68k_MapReadOnly(cart_addr, cart_addr + cart_size, cart_data);
    return true;
}


//----------------------------------------------------------------------------------
//
// ROM Init
//
//----------------------------------------------------------------------------------
bool InitRom(const char* filename)
{
    rom_data = 0; rom_size = 0; rom_addr = 0; rom_ver = 0;
    DPRINT("Loading '%s'", filename);
    FILE* f = fopen(filename, "r");
    ASSERT(f, "Failed opening '%s'", filename);

    fseek(f, 0, SEEK_END);
    uint32 filesize = ftell(f);
    fseek(f, 0, SEEK_SET);

    rom_data = AllocMem(filesize, 4096);
    rom_size = fread((uint8*)rom_data, 1, filesize, f);
    rom_addr = (0x00FF0000 & *(uint32*)(rom_data+4));
    rom_ver = *((uint16*)(rom_data+2));
    fclose(f);

    ASSERT(rom_size == filesize, "Failed reading '%s'", filename);
    DPRINT(" Rom: 0x%08x : 0x%08x ver:0x%04x (%dKb)", (uint32)rom_data, rom_addr, rom_ver, rom_size);

    if (rom_ver < 0x0200) {
        PatchTos1((uint8*)rom_data, rom_size);
    } else {
        PatchTos2((uint8*)rom_data, rom_size);
    }

    h68k_MapReadOnly(rom_addr, rom_addr + rom_size, (uint32)rom_data);
    return true;
}


//----------------------------------------------------------------------------------
// TOS 1.x patches
//----------------------------------------------------------------------------------
void PatchTos1(uint8* rom, uint32 size)
{
    const uint16 p1_startup_waitvbl[] = { 30,
        0x41f9, 0xffff, 0xfa21, 0x43f9, 0xffff, 0xfa1b, 0x12bc, 0x0010, 0x7801, 0x12bc,
        0x0000, 0x10bc, 0x00f0, 0x13fc, 0x0008, 0xffff, 0xfa1b, 0x1010, 0xb004, 0x66fa,
        0x1810, 0x363c, 0x0267, 0xb810, 0x66f6, 0x51cb, 0xfffa, 0x12bc, 0x0010, 0x4ed6 };

    uint16* p = FindMem(rom, size, p1_startup_waitvbl);
    if (p) {
        DPRINT("  Patching wait at 0x%08x", (uint32)p);
        p[20] = 0x4e71;
        p[22] = 0x0010; // move.w #16,d3    (was 615)
        p[24] = 0x4e71;

    }
}

//----------------------------------------------------------------------------------
// TOS 2.x patches
//----------------------------------------------------------------------------------
void PatchTos2(uint8* rom, uint32 size)
{
    const uint16 p1_startup_waitvbl[] = { 20,
        0x41f8, 0xfa21, 0x43f8, 0xfa1b, 0x08b8, 0x0000, 0xfa07, 0x7801, 0x4211, 0x10bc,
        0x00f0, 0x12bc, 0x0008, 0xb810, 0x66fc, 0x1810, 0x363c, 0x0267, 0xb810, 0x66f6 };
    const uint16 p2_cpu_detect[] = { 12, 0x42c0, 0x720a, 0x49c0, 0x7214, 0x4e7a, 0x0002, 0x08c0, 0x0009, 0x4e7b, 0x0002, 0x4e7a, 0x0002 };
    const uint16 p2_rom_crc[] = { 14, 0x5741, 0x524e, 0x494e, 0x473a, 0x2042, 0x4144, 0x2052, 0x4f4d, 0x2043, 0x5243, 0x2049, 0x4e20, 0x4348, 0x4950 };


    uint16* p = FindMem(rom, size, p1_startup_waitvbl);
    if (p) {
        DPRINT("  Patching wait at 0x%08x", (uint32)p);
        p[14] = 0x4e71; // nop
        p[17] = 0x0010; // move.w #16,d0    (was 615)
        p[19] = 0x4e71; // nop
    }

    p = FindMem(rom, size, p2_cpu_detect);
    if (p) {
        DPRINT("  Patching cpu detect at 0x%08x", (uint32)p);
        p[1] = 0x7200; // moveq.l #0,d0     (was moveq.l #10,d0)
        p[3] = 0x7200; // moveq.l #0,d0     (was moveq.l #20,d0)
    }

    p = FindMem(rom, size, p2_rom_crc);
    if (p) {
        DPRINT("  Patching rom crc at 0x%08x", (uint32)p);
        p[-5] = 0x4e71; // nop              (was bne.s fail)
    }
}
