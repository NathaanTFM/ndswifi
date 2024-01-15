#include <nds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef REG_IE
#undef REG_IF
#undef REG_IME

#include "PublicSdk.h"


#define REG_IE (*(volatile u32*)0x04000210)
#define REG_IF (*(volatile u32*)0x04000214)

static volatile u64 tickCount = 0;
static struct OSiAlarm *gAlarm = 0;

void MIi_CpuClear16(u16 data, void* destp, u32 size) {
    for (u32 i = 0; i < size/2; i++) {
        ((u16*)destp)[i] = data;
    }
}
void MIi_CpuClear32(u32 data, void* destp, u32 size) {
    for (u32 i = 0; i < size/4; i++) {
        ((u32*)destp)[i] = data;
    }
}
void MIi_CpuClearFast(u32 data, void* destp, u32 size) {
    for (u32 i = 0; i < size/4; i++) {
        ((u32*)destp)[i] = data;
    }
}
void MIi_CpuCopy16(const void* srcp, void* destp, u32 size) {
    for (u32 i = 0; i < size/2; i++) {
        ((u16*)destp)[i] = ((u16*)srcp)[i];
    }
}
void MIi_CpuCopy32(const void* srcp, void* destp, u32 size) {
    for (u32 i = 0; i < size/4; i++) {
        ((u32*)destp)[i] = ((u32*)srcp)[i];
    }
}
void* OS_AllocFromHeap(enum OSArena id, int heap, u32 size) {
    myprintf("alloc from heap?");
    return malloc(size);
}
void OS_FreeToHeap(enum OSArena id, int heap, void* ptr) {
    free(ptr);
}
u32 OS_EnableIrqMask(u32 intr) {
    int ime = OS_DisableIrq();     // IME disable
    u32 prep = REG_IE;
    REG_IE = prep | intr;
    OS_RestoreIrq(ime);
    return prep;
}
u32 OS_DisableIrqMask(u32 intr) {
    int ime = OS_DisableIrq();     // IME disable
    u32 prep = REG_IE;
    REG_IE = prep & ~intr;
    OS_RestoreIrq(ime);
    return prep;
}

void OS_SetIrqFunction(u32 intrBit, void (*function)()) {
    irqSet(intrBit, function);
}

void SPI_Lock(u32 id) {
    // We probably don't need this?
}

void SPI_Unlock(u32 id) {
    // We probably don't need this?
}

int OS_SendMessage(struct OSMessageQueue* mq, void* msg, s32 flags) {
    // We need to send pMsg
    int prev = OS_DisableInterrupts();
    
    if (mq->length == mq->available) {
        // we're full        
        if (flags & 1) {
            // blocking
            while (mq->length == mq->available) {
                // restore interrupts, then wait for an interrupt
                // (there's nothing left that can make us leave this function)
                OS_RestoreInterrupts(prev);
                swiHalt();
                prev = OS_DisableInterrupts();
            }
        } else {
            // non blocking
            OS_RestoreInterrupts(prev);
            return 0;
        }
    }
    
    // push to the end the value
    mq->array[(mq->index + mq->available) % mq->length] = msg;
    mq->available++;
    
    OS_RestoreInterrupts(prev);
    return 1;
}

int OS_ReceiveMessage(struct OSMessageQueue* mq, void** msg, s32 flags) {
    int prev = OS_DisableInterrupts();
    
    if (mq->available == 0) {
        // we're empty
        
        if (flags & 1) {
            // blocking
            while (mq->available == 0) {
                // restore interrupts, then wait for an interrupt
                // (there's nothing left that can make us leave this function)
                OS_RestoreInterrupts(prev);
                swiHalt();
                prev = OS_DisableInterrupts();
            }
        } else {
            // non blocking
            OS_RestoreInterrupts(prev);
            return 0;
        }
    }
    
    // push to the end the value
    *msg = mq->array[mq->index];
    mq->index = (mq->index + 1) % mq->length;
    mq->available--;
    
    OS_RestoreInterrupts(prev);
    return 1;
}

int OS_IsAlarmAvailable() {
    return 1;
}

void OS_CreateAlarm(struct OSiAlarm* alarm) {
    alarm->handler = 0;
    alarm->period = alarm->start = alarm->fire = 0;
    
    alarm->next = gAlarm;
    gAlarm = alarm;
}

void OS_SetAlarm(struct OSiAlarm* alarm, u64 tick, void (*handler)(void*), void* arg) {
    alarm->fire = OS_GetTick() + tick;
    alarm->handler = handler;
    alarm->arg = arg;
    alarm->start = alarm->period = 0;
}

void OS_SetPeriodicAlarm(struct OSiAlarm* alarm, u64 start, u64 period, void (*handler)(void*), void* arg) {
    alarm->handler = handler;
    alarm->arg = arg;
    alarm->start = start;
    alarm->period = period;
    alarm->fire = alarm->start;
}

void OS_CancelAlarm(struct OSiAlarm* alarm) {
    alarm->handler = 0;
    alarm->arg = 0;
}

u64 OS_GetTick() {
    u16 low;
    u64 high;

    int prev = OS_DisableInterrupts();

    low = TIMER_DATA(0);
    high = tickCount & 0xFFFFFFFFFFFFULL;

    if ((REG_IF & (1<<3)) != 0 && (low & 0x8000) == 0) {
        high++;
    }

    OS_RestoreInterrupts(prev);
    return (high << 16) | low;
}

void OS_CreateThread(struct _OSThread* thread, void (*func)(void*), void* arg, void* stack, u32 stackSize, u32 prio) {
    // We are running threadless,
    // we have a custom wrapper around the message getter which is not inside a thread
}

void OS_ExitThread() {
    // Same here, no thread
}

void OS_WakeupThreadDirect(struct _OSThread* thread) {
    // Same here, no thread
}

s32 OS_GetLockID() {
    // Used for SPI lock, we don't care that much
    return 0;
}

void NVRAM_ReadStatusRegister(u8* buf) {
    while (REG_SPICNT & 0x80);
    
    REG_SPICNT = 0x8900;
    REG_SPIDATA = 5;
    
    while (REG_SPICNT & 0x80);
    
    REG_SPICNT = 0x8100;
    REG_SPIDATA = 0;
    
    while (REG_SPICNT & 0x80);
    
    *buf = REG_SPIDATA;
}

void NVRAM_ReadDataBytes(u32 address, u32 size, u8* buf) {
    while (REG_SPICNT & 0x80);
    
    REG_SPICNT = 0x8900;
    REG_SPIDATA = 3;
    
    u16 addr[3];
    addr[0] = (address & 0xFF0000) >> 16;
    addr[1] = (address & 0xFF00) >> 8;
    addr[2] = address & 0xFF;
    
    for (int i = 0; i < 3; i++) {
        while (REG_SPICNT & 0x80);
        REG_SPIDATA = addr[i];
    }
    
    while (REG_SPICNT & 0x80);
    
    for (u32 i = 0; i < size-1; i++) {
        REG_SPIDATA = 0;
        while (REG_SPICNT & 0x80);
        buf[i] = REG_SPIDATA;
    }
    
    REG_SPICNT = 0x8100;
    REG_SPIDATA = 0;
    while (REG_SPICNT & 0x80);
    buf[size-1] = REG_SPIDATA;
    
}

void NVRAM_SoftwareReset() {
    while (REG_SPICNT & 0x80);
    REG_SPICNT = 0x8100;
    REG_SPIDATA = 0xFF;
    while (REG_SPICNT & 0x80);
}

void MI_WaitDma(u32 dmaNo) {
    // dma is actually unused, so we don't care
}

/*
u32 OS_DisableInterrupts() {
    uint32_t regval;
    asm volatile("mrs %0, CPSR" : "=r"(regval));
    regval |= 0x80;
    asm volatile("msr CPSR_c, %0" : "=r"(regval));
    return regval & 0x80;
}

u32 OS_RestoreInterrupts(u32 state) {
    uint32_t regval;
    asm volatile("mrs %0, CPSR" : "=r"(regval));
    regval &= ~0x80;
    regval |= state;
    asm volatile("msr CPSR_c, %0" : "=r"(regval));
    return regval & 0x80;
}*/

u32 OS_DisableInterrupts() {
    return OS_DisableIrq();
}

u32 OS_RestoreInterrupts(u32 state) {
    return OS_RestoreIrq(state);
}

static void IncreaseTickCount() {
    tickCount++;
    
    u32 x = OS_DisableInterrupts();
    u64 tick = OS_GetTick();
    
    struct OSiAlarm *alarm = gAlarm;
    while (alarm) {
        while (alarm->handler != 0 && alarm->fire < tick) {
            void (*fn)(void *arg) = alarm->handler;            
            if (alarm->period != 0) {
                alarm->fire += alarm->period;
            } else {
                alarm->handler = 0;
            }
            
            fn(alarm->arg);
            tick = OS_GetTick();
        }
        alarm = alarm->next;
    }
    
    OS_RestoreInterrupts(x);
 }

void InitSdk() {
    timerStart(
        0,
        ClockDivider_64 ,
        0,
        &IncreaseTickCount
    );
}

void InitMsgQueue(struct OSMessageQueue* mq, void **array, u16 length) {
    mq->length = length;
    mq->array = array;
    mq->index = mq->available = 0;
}