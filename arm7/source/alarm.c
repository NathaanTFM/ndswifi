#include "alarm.h"
#include "timer.h"
#include <nds.h>
#include <stdbool.h>

static Alarm *volatile alarmList = NULL;

void alarmIrqAligned();
extern void alarmIrq();
static void setAlarmTimer();
static bool removeAlarm(Alarm *alarm);
static bool insertAlarm(Alarm *alarm);

void alarmIrqAligned() {
    int x = enterCriticalSection();
    
    for (;;) {
        u64 tick = getTick();
        Alarm *alarm = alarmList;
        if (!alarm || tick < alarm->fire)
            break;
        
        // quick remove
        alarmList = alarm->next;
        alarm->prev = alarm->next = NULL;
        
        if (alarm->period != 0) {
            alarm->fire += alarm->period;
            
            // slow insert
            insertAlarm(alarm);   
        }
        
        alarm->handler(alarm->arg);
    }
    
    setAlarmTimer();
    leaveCriticalSection(x);
}

__asm__(
"alarmIrq:\n"
"  push {lr}\n"
"  bl alarmIrqAligned\n"
"  pop {lr}\n"
"  bx lr\n"
);

static void setAlarmTimer() {
    if (alarmList) {
        u64 tick = getTick();
        s64 delta = alarmList->fire - tick;
        u16 timer = 0;
        
        if (delta < 0) {
            // negative, start asap
            timer = 0xFFFF;
            
        } else if (delta < 0x10000) {
            // in [0 ; 0xFFFF] range
            timer = 0xFFFF - delta;
            
        } else {
            // above max
            timer = 0;
        }
        
        timerStart(1, ClockDivider_64, timer, &alarmIrq);
        
    } else {
        // no timer
        timerStop(1);
    }
}

static bool removeAlarm(Alarm *alarm) {
    bool firstChanged = false;
    
    if (alarm->prev == NULL) {
        // first element, or not in
        if (alarmList == alarm) {
            // first element (or only one)
            alarmList = alarm->next;
            firstChanged = true;
        }
        
        if (alarm->next) {
            alarm->next->prev = NULL;
            alarm->next = NULL;
        }
        
    } else {
        alarm->prev->next = alarm->next;
        if (alarm->next) {
            alarm->next->prev = alarm->prev;
            alarm->next = NULL;
        }
        
        alarm->prev = NULL;
    }
    
    return firstChanged;
}

static bool insertAlarm(Alarm *alarm) {
    bool firstChanged = false;
    
    Alarm *prev = NULL;
    Alarm *cur = alarmList;
    
    while (cur && alarm->fire >= cur->fire) {
        prev = cur;
        cur = cur->next;
    }
    
    // alarm->fire < cur->fire
    // so we need to insert before it
    
    if (cur == NULL) {
        if (prev == NULL) {
            // the array was empty
            alarmList = alarm;
            alarm->next = alarm->prev = NULL;
            firstChanged = true;
            
        } else {
            // we reached the end of the array
            prev->next = alarm;
            alarm->prev = prev;
            alarm->next = NULL;
        }
        
    } else {
        if (prev == NULL) {
            // we are the first element
            alarmList = alarm;
            alarm->next = cur;
            alarm->prev = NULL;
            firstChanged = true;
            
        } else {
            // we are between two of them
            prev->next = alarm;
            alarm->next = cur;
            alarm->prev = prev;
            cur->prev = alarm;
        }
    }
    
    return firstChanged;
}

void createAlarm(Alarm *alarm) {
    alarm->handler = alarm->arg = NULL;
    alarm->period = alarm->start = alarm->fire = 0;
    alarm->next = alarm->prev = NULL;
}

void setAlarm(Alarm *alarm, u64 tick, void (*handler)(void *), void *arg) {
    int x = enterCriticalSection();
    
    alarm->fire = getTick() + tick;
    alarm->handler = handler;
    alarm->arg = arg;
    alarm->start = alarm->period = 0;
    
    bool flag = removeAlarm(alarm);
    flag |= insertAlarm(alarm);
    if (flag)
        setAlarmTimer();
    
    leaveCriticalSection(x);
}

void setPeriodicAlarm(Alarm *alarm, u64 start, u64 period, void (*handler)(void *), void *arg) {
    int x = enterCriticalSection();
    
    alarm->handler = handler;
    alarm->arg = arg;
    alarm->start = start;
    alarm->period = period;
    alarm->fire = alarm->start;
    
    bool flag = removeAlarm(alarm);
    flag |= insertAlarm(alarm);
    if (flag)
        setAlarmTimer();
    
    leaveCriticalSection(x);
}

void cancelAlarm(Alarm *alarm) {
    int x = enterCriticalSection();
    
    if (removeAlarm(alarm))
        setAlarmTimer();
    
    leaveCriticalSection(x);
}