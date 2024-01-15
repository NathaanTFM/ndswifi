#ifndef PUBLIC_SDK_H
#define PUBLIC_SDK_H
#define NO_IDLE_TASK 1

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef unsigned long long u64;

typedef signed char s8;
typedef signed short s16;
typedef signed long s32;
typedef signed long long s64;

enum OSArena {
    OS_ARENA_MAIN = 0
};

struct OSMessageQueue {
    u16 index, available, length;
    void **array;
};

struct OSiAlarm {
    void (*handler)(void*); // offset 00
    void* arg; // offset 04
    u64 period;
    u64 start;
    u64 fire;
    struct OSiAlarm *next;
};

struct _OSThread {
    int dummy;
};

// Bunch of functions from an unknown outside API we don't have any access to.
extern void MIi_CpuClear16(u16 data, void* destp, u32 size);
extern void MIi_CpuClear32(u32 data, void* destp, u32 size);
extern void MIi_CpuClearFast(u32 data, void* destp, u32 size);
extern void MIi_CpuCopy16(const void* srcp, void* destp, u32 size);
extern void MIi_CpuCopy32(const void* srcp, void* destp, u32 size);
extern void* OS_AllocFromHeap(enum OSArena id, int heap, u32 size);
extern void OS_FreeToHeap(enum OSArena id, int heap, void* ptr);
extern u32 OS_EnableIrqMask(u32 intr);
extern u32 OS_DisableIrqMask(u32 intr);
extern u32 OS_DisableInterrupts();
extern u32 OS_RestoreInterrupts(u32 state);
extern void OS_SetIrqFunction(u32 intrBit, void (*function)());
extern void SPI_Lock(u32 id);
extern void SPI_Unlock(u32 id);
extern int OS_SendMessage(struct OSMessageQueue* mq, void* msg, s32 flags);
extern int OS_ReceiveMessage(struct OSMessageQueue* mq, void** msg, s32 flags);
extern int OS_IsAlarmAvailable(); 
extern void OS_CreateAlarm(struct OSiAlarm* alarm); 
extern void OS_SetAlarm(struct OSiAlarm* alarm, u64 tick, void (*handler)(void*), void* arg); 
extern void OS_SetPeriodicAlarm(struct OSiAlarm* alarm, u64 start, u64 period, void (*handler)(void*), void* arg); 
extern void OS_CancelAlarm(struct OSiAlarm* alarm); 
extern u64 OS_GetTick();
extern void OS_CreateThread(struct _OSThread* thread, void (*func)(void*), void* arg, void* stack, u32 stackSize, u32 prio);
extern void OS_ExitThread();
extern void OS_WakeupThreadDirect(struct _OSThread* thread);
extern s32 OS_GetLockID();
extern void NVRAM_ReadStatusRegister(u8* buf);
extern void NVRAM_ReadDataBytes(u32 address, u32 size, u8* buf);
extern void NVRAM_SoftwareReset();
extern void MI_WaitDma(u32 dmaNo);

// Home made inlined functions that probably exist somewhere (I think they were in the older version of the driver)
#define REG_IME (*(volatile u16*)0x4000208)

static inline int OS_RestoreIrq(int enable) {
    u16 prep = REG_IME;
    REG_IME = (u16)enable;
    return (int)prep;
}

static inline int OS_DisableIrq() {
    u16 prep = REG_IME;
    REG_IME = 0;
    return (int)prep;
}

// These functions are also home made
extern void InitSdk();
extern void InitMsgQueue(struct OSMessageQueue* mq, void **array, u16 length);

#endif