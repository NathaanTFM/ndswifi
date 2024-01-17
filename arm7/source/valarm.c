#include "valarm.h"
#include <nds.h>

static volatile u32 gFrame = 0;
static volatile u16 gLast = 0; // last value of VCOUNT

static VAlarm *volatile valarmList = NULL;

void valarmIrqAligned();
static u32 getFrame(u16 req);
extern void valarmIrq();
static void setVAlarmIrq();
static bool removeVAlarm(VAlarm *valarm);
static bool insertVAlarm(VAlarm *valarm);

static u32 getFrame(u16 reg) {
    u32 frame = gFrame;
    u16 last = gLast;
    
    if (reg < last) {
        frame++;
        gFrame = frame;
    }
    
    gLast = reg;
    return frame;
}

void valarmIrqAligned() {
    int x = enterCriticalSection();
    
    for (;;) {
        u16 reg = REG_VCOUNT;
        u32 frame = getFrame(reg);
        
        VAlarm *valarm = valarmList;
        if (!valarm || frame < valarm->frame || reg < valarm->count)
            break;
        
        // quick remove
        valarmList = valarm->next;
        valarm->prev = valarm->next = NULL;
        
        if (valarm->periodic) {
            // slow insert
            valarm->frame = frame + 1;
            insertVAlarm(valarm);
        }
        
        // how many lines are we past it?
        s32 delay = reg - valarm->count;
        if (delay < 0) delay += 263;
        
        if (delay <= valarm->delay) {
            valarm->handler(valarm->arg);
        }
    }
    
    setVAlarmIrq();
    leaveCriticalSection(x);
}

__asm__(
"valarmIrq:\n"
"  push {lr}\n"
"  bl valarmIrqAligned\n"
"  pop {lr}\n"
"  bx lr\n"
);

static void setVAlarmIrq() {
    VAlarm *valarm = valarmList;
    
    if (valarm) {
        REG_DISPSTAT = (REG_DISPSTAT & 0x3F) | ((valarm->count & 0xFF) << 8) | ((valarm->count & 0x100) >> 1);
        irqSet(IRQ_VCOUNT, valarmIrq);
        irqEnable(IRQ_VCOUNT);
        
    } else {
        irqDisable(IRQ_VCOUNT);
    }
}

static bool removeVAlarm(VAlarm *valarm) {
    bool firstChanged = false;
    
    if (valarm->prev == NULL) {
        // first element, or not in
        if (valarmList == valarm) {
            // first element (or only one)
            valarmList = valarm->next;
            firstChanged = true;
        }
        
        if (valarm->next) {
            valarm->next->prev = NULL;
            valarm->next = NULL;
        }
        
    } else {
        valarm->prev->next = valarm->next;
        if (valarm->next) {
            valarm->next->prev = valarm->prev;
            valarm->next = NULL;
        }
        
        valarm->prev = NULL;
    }
    
    return firstChanged;
}

static bool insertVAlarm(VAlarm *valarm) {
    // so it's sorted by frame, then count
    bool firstChanged = false;
    
    VAlarm *prev = NULL;
    VAlarm *cur = valarmList;
    
    while (cur && (valarm->frame > cur->frame || (valarm->frame == cur->frame && valarm->count >= cur->count))) {
        prev = cur;
        cur = cur->next;
    }
    
    if (cur == NULL) {
        if (prev == NULL) {
            // the array was empty
            valarmList = valarm;
            valarm->next = valarm->prev = NULL;
            firstChanged = true;
            
        } else {
            // we reached the end of the array
            prev->next = valarm;
            valarm->prev = prev;
            valarm->next = NULL;
        }
        
    } else {
        if (prev == NULL) {
            // we are the first element
            valarmList = valarm;
            valarm->next = cur;
            valarm->prev = NULL;
            firstChanged = true;
            
        } else {
            // we are between two of them
            prev->next = valarm;
            valarm->next = cur;
            valarm->prev = prev;
            cur->prev = valarm;
        }
    }
    
    return firstChanged;
}

void createVAlarm(VAlarm *valarm) {
    valarm->handler = valarm->arg = NULL;
    valarm->count = valarm->delay = 0;
    valarm->frame = valarm->periodic = false;
    valarm->next = valarm->prev = NULL;
}

void setVAlarm(VAlarm *valarm, u16 count, u16 delay, void (*handler)(void *), void *arg) {
    int x = enterCriticalSection();
    
    u16 reg = REG_VCOUNT;
    u32 frame = getFrame(reg);
    
    valarm->handler = handler;
    valarm->arg = arg;
    valarm->count = count;
    valarm->delay = delay;
    valarm->frame = (count > reg) ? frame : (frame+1);
    valarm->periodic = false;
    
    bool flag = removeVAlarm(valarm);
    flag |= insertVAlarm(valarm);
    
    if (flag)
        setVAlarmIrq();
    
    leaveCriticalSection(x);
}

void setPeriodicVAlarm(VAlarm *valarm, u16 count, u16 delay, void (*handler)(void *), void *arg) {
    int x = enterCriticalSection();
    
    u16 reg = REG_VCOUNT;
    u32 frame = getFrame(reg);
    
    valarm->handler = handler;
    valarm->arg = arg;
    valarm->count = count;
    valarm->delay = delay;
    valarm->frame = (count > reg) ? frame : (frame+1);
    valarm->periodic = true;
    
    bool flag = removeVAlarm(valarm);
    flag |= insertVAlarm(valarm);
    
    if (flag)
        setVAlarmIrq(valarm);
    
    leaveCriticalSection(x);
}

void cancelVAlarm(VAlarm *valarm) {
    int x = enterCriticalSection();
    
    if (removeVAlarm(valarm))
        setVAlarmIrq(valarm);
    
    leaveCriticalSection(x);
}